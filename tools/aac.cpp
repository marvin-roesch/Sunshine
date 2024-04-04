#include <iostream>
#include <fstream>
#include <vector>
using namespace std;

#include "fdkaac_dec.h"
#include "wavwriter.h"

/**
* @see ISO/IEC 13818-7 Information technology — Generic coding of moving pictures and associated audio information
		— Part 7:Advanced Audio Coding (AAC)
6.2.1
6.2.2
*/
class adts_header_t
{
public:
  unsigned char syncword_0_to_8						: 	8;

  unsigned char protection_absent						:	1;
  unsigned char layer									: 	2;
  unsigned char ID 									:	1;
  unsigned char syncword_9_to_12						:	4;

  unsigned char channel_configuration_0_bit			:	1;
  unsigned char private_bit							:	1;
  unsigned char sampling_frequency_index				:	4;
  unsigned char profile								:	2;

  unsigned char frame_length_0_to_1 					: 	2;
  unsigned char copyrignt_identification_start		: 	1;
  unsigned char copyright_identification_bit 			: 	1;
  unsigned char home 									: 	1;
  unsigned char original_or_copy 						: 	1;
  unsigned char channel_configuration_1_to_2 			: 	2;

  unsigned char frame_length_2_to_9					:	8;

  unsigned char adts_buffer_fullness_0_to_4 			: 	5;
  unsigned char frame_length_10_to_12 				: 	3;

  unsigned char number_of_raw_data_blocks_in_frame 	: 	2;
  unsigned char adts_buffer_fullness_5_to_10 			: 	6;
};

int main(int argc, char const *argv[])
{
  cout << "start" << endl;
  ifstream in_aac("surround71.aac", ios::binary);
  void *wav = NULL;
  AacDecoder fdkaac_dec;
  fdkaac_dec.aacdec_init_adts();

  int nbPcm = fdkaac_dec.aacdec_pcm_size();
  if (nbPcm == 0) {
    nbPcm = 50 * 1024;
  }

  std::vector<char> pcm_buf(nbPcm, 0);


  in_aac.seekg(0, ios::end);
  int nbAacSize = in_aac.tellg();
  in_aac.seekg (0, ios::beg);

  std::vector<char> aac_buf(nbAacSize, 0);
  in_aac.read(&aac_buf[0], nbAacSize);
  int pos = 0;

  while (1) {
    if (nbAacSize - pos < 7) {
      break;
    }

    adts_header_t *adts = (adts_header_t *)(&aac_buf[0] + pos);

    if (adts->syncword_0_to_8 != 0xff || adts->syncword_9_to_12 != 0xf) {
      break;
    }

    int aac_frame_size = adts->frame_length_0_to_1 << 11 | adts->frame_length_2_to_9 << 3 | adts->frame_length_10_to_12;

    if (pos + aac_frame_size > nbAacSize) {
      break;
    }

    int leftSize = aac_frame_size;
    int ret = fdkaac_dec.aacdec_fill(&aac_buf[0] + pos, aac_frame_size, &leftSize);
    pos += aac_frame_size;

    if (ret != 0) {
      continue;
    }

    if (leftSize > 0) {
      continue;
    }

    int validSize = 0;
    ret = fdkaac_dec.aacdec_decode_frame(&pcm_buf[0], pcm_buf.size(), &validSize);

    if (ret == AAC_DEC_NOT_ENOUGH_BITS) {
      continue;
    }

    if (ret != 0) {
      continue;
    }

    if (!wav) {
      if (fdkaac_dec.aacdec_sample_rate() <= 0) {
        break;
      }

      wav = wav_write_open("surround71.wav", fdkaac_dec.aacdec_sample_rate(), 16, fdkaac_dec.aacdec_num_channels());
      if (!wav) {
        break;
      }
    }

    wav_write_data(wav, (unsigned char*)&pcm_buf[0], validSize);
  }

  in_aac.close();
  if (wav) {
    wav_write_close(wav);
  }

  cout << "end" << endl;
  return 0;
}