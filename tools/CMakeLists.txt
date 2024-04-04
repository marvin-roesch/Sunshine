cmake_minimum_required(VERSION 3.0)

project(sunshine_tools)

include_directories("${CMAKE_SOURCE_DIR}")

add_executable(dxgi-info dxgi.cpp)
set_target_properties(dxgi-info PROPERTIES CXX_STANDARD 17)
target_link_libraries(dxgi-info
        ${CMAKE_THREAD_LIBS_INIT}
        dxgi
        ${PLATFORM_LIBRARIES})
target_compile_options(dxgi-info PRIVATE ${SUNSHINE_COMPILE_OPTIONS})

add_executable(audio-info audio.cpp)
set_target_properties(audio-info PROPERTIES CXX_STANDARD 17)
target_link_libraries(audio-info
        ${CMAKE_THREAD_LIBS_INIT}
        ksuser
        ${PLATFORM_LIBRARIES})
target_compile_options(audio-info PRIVATE ${SUNSHINE_COMPILE_OPTIONS})

add_executable(aac-info aac.cpp)
set_target_properties(aac-info PROPERTIES CXX_STANDARD 17)
target_link_libraries(aac-info
        ${CMAKE_THREAD_LIBS_INIT}
        fdk-aac
        ${PLATFORM_LIBRARIES})
target_sources(aac-info PRIVATE fdkaac_dec.cpp fdkaac_dec.h wavwriter.c wavwriter.h)
target_compile_options(aac-info PRIVATE ${SUNSHINE_COMPILE_OPTIONS})

add_executable(aac-enc enc_test.cpp)
set_target_properties(aac-enc PROPERTIES CXX_STANDARD 17)
target_link_libraries(aac-enc
        ${CMAKE_THREAD_LIBS_INIT}
        fdk-aac
        ${PLATFORM_LIBRARIES})
target_sources(aac-enc PRIVATE fdkaac_enc.cpp fdkaac_enc.h wavreader.c wavwriter.h)
target_compile_options(aac-enc PRIVATE ${SUNSHINE_COMPILE_OPTIONS})

add_executable(sunshinesvc sunshinesvc.cpp)
set_target_properties(sunshinesvc PROPERTIES CXX_STANDARD 17)
target_link_libraries(sunshinesvc
        ${CMAKE_THREAD_LIBS_INIT}
        wtsapi32
        ${PLATFORM_LIBRARIES})
target_compile_options(sunshinesvc PRIVATE ${SUNSHINE_COMPILE_OPTIONS})

add_executable(ddprobe ddprobe.cpp)
set_target_properties(ddprobe PROPERTIES CXX_STANDARD 17)
target_link_libraries(ddprobe
        ${CMAKE_THREAD_LIBS_INIT}
        dxgi
        d3d11
        ${PLATFORM_LIBRARIES})
target_compile_options(ddprobe PRIVATE ${SUNSHINE_COMPILE_OPTIONS})
