/**
 * @file src/audio.cpp
 * @brief todo
 */
#include <thread>
#include <iostream>
#include <fstream>

#include <opus/opus_multistream.h>
#include <fdk-aac/aacenc_lib.h>

#include "platform/common.h"

#include "audio.h"
#include "config.h"
#include "globals.h"
#include "logging.h"
#include "thread_safe.h"
#include "utility.h"

namespace audio {
  using namespace std::literals;
  using opus_t = util::safe_ptr<OpusMSEncoder, opus_multistream_encoder_destroy>;
  using sample_queue_t = std::shared_ptr<safe::queue_t<std::vector<std::int16_t>>>;
  using aac_t = util::safe_ptr<AACENCODER, destroy_aac_encoder>;

  void destroy_aac_encoder(HANDLE_AACENCODER phAacEncoder) {
    aacEncClose(&phAacEncoder);
  }

  struct audio_ctx_t {
    // We want to change the sink for the first stream only
    std::unique_ptr<std::atomic_bool> sink_flag;

    std::unique_ptr<platf::audio_control_t> control;

    bool restore_sink;
    platf::sink_t sink;
  };

  static int
  start_audio_control(audio_ctx_t &ctx);
  static void
  stop_audio_control(audio_ctx_t &);

  int
  map_stream(int channels, bool quality);

  constexpr auto SAMPLE_RATE = 48000;

  // NOTE: If you adjust the bitrates listed here, make sure to update the
  // corresponding bitrate adjustment logic in rtsp_stream::cmd_announce()
  opus_stream_config_t stream_configs[MAX_STREAM_CONFIG] {
    {
      SAMPLE_RATE,
      2,
      1,
      1,
      platf::speaker::map_stereo,
      96000,
    },
    {
      SAMPLE_RATE,
      2,
      1,
      1,
      platf::speaker::map_stereo,
      512000,
    },
    {
      SAMPLE_RATE,
      6,
      4,
      2,
      platf::speaker::map_surround51,
      256000,
    },
    {
      SAMPLE_RATE,
      6,
      6,
      0,
      platf::speaker::map_surround51,
      1536000,
    },
    {
      SAMPLE_RATE,
      8,
      5,
      3,
      platf::speaker::map_surround71,
      450000,
    },
    {
      SAMPLE_RATE,
      8,
      8,
      0,
      platf::speaker::map_surround71,
      2048000,
    },
  };

  auto control_shared = safe::make_shared<audio_ctx_t>(start_audio_control, stop_audio_control);

  void
  encodeThread(sample_queue_t samples, config_t config, void *channel_data) {
    auto packets = mail::man->queue<packet_t>(mail::audio_packets);
    auto stream = &stream_configs[map_stream(config.channels, config.flags[config_t::HIGH_QUALITY])];

    // Encoding takes place on this thread
    platf::adjust_thread_priority(platf::thread_priority_e::high);

    aac_t aac;
    aacEncOpen(&aac, 0, stream->channelCount);
    aacEncoder_SetParam(aac.get(), AACENC_TRANSMUX, 2 /* ADTS format */);
    aacEncoder_SetParam(aac.get(), AACENC_AOT, 2 /*MPEG-4 AAC LC*/);
    aacEncoder_SetParam(aac.get(), AACENC_SAMPLERATE, stream->sampleRate);
    aacEncoder_SetParam(aac.get(), AACENC_BITRATE, stream->bitrate);
    aacEncoder_SetParam(aac.get(), AACENC_AFTERBURNER, 1);
    aacEncoder_SetParam(aac.get(), AACENC_CHANNELORDER, 1);
    aacEncoder_SetParam(aac.get(), AACENC_SIGNALING_MODE, 0);

    CHANNEL_MODE chMode =
      MODE_INVALID;
    switch (stream->channelCount) {
    case 1:  chMode = MODE_1;          break;
    case 2:  chMode = MODE_2;          break;
    case 3:  chMode = MODE_1_2;        break;
    case 4:  chMode = MODE_1_2_1;      break;
    case 5:  chMode = MODE_1_2_2;      break;
    case 6:  chMode = MODE_1_2_2_1;    break;
    case 7:  chMode = MODE_6_1;        break;
    case 8:  chMode = MODE_7_1_BACK;   break;
    default:
      chMode = MODE_INVALID;
    }

    aacEncoder_SetParam(aac.get(), AACENC_CHANNELMODE, chMode);

    aacEncEncode(aac.get(), nullptr, nullptr, nullptr, nullptr);

    AACENC_InfoStruct encInfo;
    aacEncInfo(aac.get(), &encInfo);
    BOOST_LOG(info) << "Channel count: " << stream->channelCount << " Sample rate: " << stream->sampleRate << "Hz Bitrate: " << stream->bitrate << "bps";
    BOOST_LOG(info) << "Max output buffer size: " << encInfo.maxOutBufBytes << " Frame length: " << encInfo.frameLength;

    opus_t opus { opus_multistream_encoder_create(
      stream->sampleRate,
      stream->channelCount,
      stream->streams,
      stream->coupledStreams,
      stream->mapping,
      OPUS_APPLICATION_RESTRICTED_LOWDELAY,
      nullptr) };

    opus_multistream_encoder_ctl(opus.get(), OPUS_SET_BITRATE(stream->bitrate));
    opus_multistream_encoder_ctl(opus.get(), OPUS_SET_VBR(0));

    char aacBuffer[1024 * 8];
    INT aacInBufferIds[1] = {IN_AUDIO_DATA}, aacInBufSizes[1], aacInBufferElSizes[1] = {sizeof(short)};
    INT aacOutBufferIds[1] = {OUT_BITSTREAM_DATA}, aacOutBufSizes[1] = {sizeof(aacBuffer)}, aacOutBufferElSizes[1] = {1};

    void *pcmBufs[1], *aacBufs[1];
    aacBufs[0] = aacBuffer;

    AACENC_BufDesc inBufDesc = {
      .numBufs = 1,
      .bufs = pcmBufs,
      .bufferIdentifiers = aacInBufferIds,
      .bufSizes = aacInBufSizes,
      .bufElSizes = aacInBufferElSizes,
    };
    AACENC_BufDesc outBufDesc = {
      .numBufs = 1,
      .bufs = aacBufs,
      .bufferIdentifiers = aacOutBufferIds,
      .bufSizes = aacOutBufSizes,
      .bufElSizes = aacOutBufferElSizes,
    };

    AACENC_InArgs inargs = {0, 0};
    AACENC_OutArgs outargs = {0};

    std::ofstream out_aac("hello.aac", std::ios::binary);

    auto frame_size = config.packetDuration * stream->sampleRate / 1000;
    while (auto sample = samples->pop()) {
      buffer_t packet { encInfo.maxOutBufBytes };

      inargs.numInSamples = frame_size * stream->channelCount;
      aacInBufSizes[0] = sample->size();
      pcmBufs[0] = sample->data();

      aacBufs[0] = std::begin(packet);
      aacOutBufSizes[0] = packet.size();

      // FFF14C00D16210A099
      /*
      * 111111111111
      * 0
      * 00
      * 1
      * 01
      * 0011
      * 0
      * 000
      * 0
      * 000001101000101100010000100001010000010011001
      */

      AACENC_ERROR err = aacEncEncode(aac.get(), &inBufDesc, &outBufDesc, &inargs, &outargs);
      if (err != AACENC_OK) {
        BOOST_LOG(error) << "Couldn't encode AAC audio: 0x"sv << std::hex << err;
        packets->stop();

        return;
      }
      int bytes = outargs.numOutBytes;

      out_aac.write(reinterpret_cast<const char *>(std::begin(packet)), bytes);

  //    int bytes = opus_multistream_encode(opus.get(), sample->data(), frame_size, std::begin(packet), packet.size());
  //    if(bytes < 0) {
  //      BOOST_LOG(error) << "Couldn't encode audio: "sv << opus_strerror(bytes);
  //      packets->stop();
  //
  //      return;
  //    }

      packet.fake_resize(bytes);
      packets->raise(channel_data, std::move(packet));
    }
    out_aac.close();
}

  void
  capture(safe::mail_t mail, config_t config, void *channel_data) {
    auto shutdown_event = mail->event<bool>(mail::shutdown);
    auto stream = &stream_configs[map_stream(config.channels, config.flags[config_t::HIGH_QUALITY])];

    auto ref = control_shared.ref();
    if (!ref) {
      return;
    }

    auto init_failure_fg = util::fail_guard([&shutdown_event]() {
      BOOST_LOG(error) << "Unable to initialize audio capture. The stream will not have audio."sv;

      // Wait for shutdown to be signalled if we fail init.
      // This allows streaming to continue without audio.
      shutdown_event->view();
    });

    auto &control = ref->control;
    if (!control) {
      return;
    }

    // Order of priority:
    // 1. Virtual sink
    // 2. Audio sink
    // 3. Host
    std::string *sink = &ref->sink.host;
    if (!config::audio.sink.empty()) {
      sink = &config::audio.sink;
    }

    // Prefer the virtual sink if host playback is disabled or there's no other sink
    if (ref->sink.null && (!config.flags[config_t::HOST_AUDIO] || sink->empty())) {
      auto &null = *ref->sink.null;
      switch (stream->channelCount) {
        case 2:
          sink = &null.stereo;
          break;
        case 6:
          sink = &null.surround51;
          break;
        case 8:
          sink = &null.surround71;
          break;
      }
    }

    // Only the first to start a session may change the default sink
    if (!ref->sink_flag->exchange(true, std::memory_order_acquire)) {
      // If the selected sink is different than the current one, change sinks.
      ref->restore_sink = ref->sink.host != *sink;
      if (ref->restore_sink) {
        if (control->set_sink(*sink)) {
          return;
        }
      }
    }

    auto frame_size = config.packetDuration * stream->sampleRate / 1000;
    auto mic = control->microphone(stream->mapping, stream->channelCount, stream->sampleRate, frame_size);
    if (!mic) {
      return;
    }

    // Audio is initialized, so we don't want to print the failure message
    init_failure_fg.disable();

    // Capture takes place on this thread
    platf::adjust_thread_priority(platf::thread_priority_e::critical);

    auto samples = std::make_shared<sample_queue_t::element_type>(30);
    std::thread thread { encodeThread, samples, config, channel_data };

    auto fg = util::fail_guard([&]() {
      samples->stop();
      thread.join();

      shutdown_event->view();
    });

    int samples_per_frame = frame_size * stream->channelCount;

    while (!shutdown_event->peek()) {
      std::vector<std::int16_t> sample_buffer;
      sample_buffer.resize(samples_per_frame);

      auto status = mic->sample(sample_buffer);
      switch (status) {
        case platf::capture_e::ok:
          break;
        case platf::capture_e::timeout:
          continue;
        case platf::capture_e::reinit:
          BOOST_LOG(info) << "Reinitializing audio capture"sv;
          mic.reset();
          do {
            mic = control->microphone(stream->mapping, stream->channelCount, stream->sampleRate, frame_size);
            if (!mic) {
              BOOST_LOG(warning) << "Couldn't re-initialize audio input"sv;
            }
          } while (!mic && !shutdown_event->view(5s));
          continue;
        default:
          return;
      }

      samples->raise(std::move(sample_buffer));
    }
  }

  int
  map_stream(int channels, bool quality) {
    int shift = quality ? 1 : 0;
    switch (channels) {
      case 2:
        return STEREO + shift;
      case 6:
        return SURROUND51 + shift;
      case 8:
        return SURROUND71 + shift;
    }
    return STEREO;
  }

  int
  start_audio_control(audio_ctx_t &ctx) {
    auto fg = util::fail_guard([]() {
      BOOST_LOG(warning) << "There will be no audio"sv;
    });

    ctx.sink_flag = std::make_unique<std::atomic_bool>(false);

    // The default sink has not been replaced yet.
    ctx.restore_sink = false;

    if (!(ctx.control = platf::audio_control())) {
      return 0;
    }

    auto sink = ctx.control->sink_info();
    if (!sink) {
      // Let the calling code know it failed
      ctx.control.reset();
      return 0;
    }

    ctx.sink = std::move(*sink);

    fg.disable();
    return 0;
  }

  void
  stop_audio_control(audio_ctx_t &ctx) {
    // restore audio-sink if applicable
    if (!ctx.restore_sink) {
      return;
    }

    // Change back to the host sink, unless there was none
    const std::string &sink = ctx.sink.host.empty() ? config::audio.sink : ctx.sink.host;
    if (!sink.empty()) {
      // Best effort, it's allowed to fail
      ctx.control->set_sink(sink);
    }
  }
}  // namespace audio
