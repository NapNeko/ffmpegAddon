cmake_minimum_required(VERSION 3.15)
project(NapCatAudioCodecs LANGUAGES C CXX)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
include(FetchContent)

FetchContent_Declare(opencore
    URL https://deb.debian.org/debian/pool/main/o/opencore-amr/opencore-amr_0.1.6.orig.tar.gz
    URL_HASH SHA256=483eb4061088e2b34b358e47540b5d495a96cd468e361050fae615b1809dc4a1
)
FetchContent_Declare(speex_source
    URL https://downloads.xiph.org/releases/speex/speex-1.2.1.tar.gz
    URL_HASH SHA256=4b44d4f2b38a370a2d98a78329fefc56a0cf93d1c1be70029217baae6628feea
)
FetchContent_MakeAvailable(opencore speex_source)

set(amr_base "${opencore_SOURCE_DIR}/opencore/codecs_v2/audio/gsm_amr")
set(amr_nb "${amr_base}/amr_nb")
file(GLOB amr_decoder "${amr_nb}/dec/src/*.cpp")
file(GLOB amr_encoder "${amr_nb}/enc/src/*.cpp")
file(GLOB amr_common "${amr_nb}/common/src/*.cpp")
list(FILTER amr_decoder EXCLUDE REGEX "/(decoder_gsm_amr|pvgsmamrdecoder)\\.cpp$")
list(FILTER amr_encoder EXCLUDE REGEX "/gsmamr_encoder_wrapper\\.cpp$")
list(FILTER amr_common EXCLUDE REGEX "/(bits2prm|copy|div_32|l_abs|r_fft|vad1|vad2)\\.cpp$")
add_library(opencore-amrnb STATIC "${opencore_SOURCE_DIR}/amrnb/wrapper.cpp" ${amr_decoder} ${amr_encoder} ${amr_common})
target_include_directories(opencore-amrnb PRIVATE
    "${opencore_SOURCE_DIR}/oscl" "${amr_nb}/dec/src" "${amr_nb}/enc/src"
    "${amr_nb}/common/include" "${amr_nb}/dec/include" "${amr_base}/common/dec/include"
)
install(FILES "${opencore_SOURCE_DIR}/amrnb/interf_dec.h" "${opencore_SOURCE_DIR}/amrnb/interf_enc.h"
        DESTINATION include/opencore-amrnb)

set(speex_files
    cb_search.c exc_10_32_table.c exc_8_128_table.c filters.c gain_table.c hexc_table.c
    high_lsp_tables.c lsp.c ltp.c speex.c stereo.c vbr.c vq.c bits.c exc_10_16_table.c
    exc_20_32_table.c exc_5_256_table.c exc_5_64_table.c gain_table_lbr.c hexc_10_32_table.c
    lpc.c lsp_tables_nb.c modes.c modes_wb.c nb_celp.c quant_lsp.c sb_celp.c
    speex_callbacks.c speex_header.c window.c
)
list(TRANSFORM speex_files PREPEND "${speex_source_SOURCE_DIR}/libspeex/")
add_library(speex STATIC ${speex_files})
target_compile_definitions(speex PRIVATE FLOATING_POINT USE_ALLOCA EXPORT=)
if(MSVC)
    target_compile_definitions(speex PRIVATE alloca=_alloca)
else()
    target_compile_definitions(speex PRIVATE HAVE_ALLOCA_H)
endif()
set(INCLUDE_STDINT "#include <stdint.h>")
set(SIZE16 int16_t)
set(USIZE16 uint16_t)
set(SIZE32 int32_t)
set(USIZE32 uint32_t)
configure_file("${speex_source_SOURCE_DIR}/include/speex/speex_config_types.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/speex_config_types.h" @ONLY)
target_include_directories(speex PRIVATE "${speex_source_SOURCE_DIR}/include" "${CMAKE_CURRENT_BINARY_DIR}")
install(DIRECTORY "${speex_source_SOURCE_DIR}/include/speex/" DESTINATION include/speex FILES_MATCHING PATTERN "*.h")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/speex_config_types.h" DESTINATION include/speex)
configure_file(speex.pc.in speex.pc @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/speex.pc" DESTINATION lib/pkgconfig)
install(TARGETS opencore-amrnb speex ARCHIVE DESTINATION lib)
install(FILES "${opencore_SOURCE_DIR}/LICENSE" DESTINATION share/licenses/opencore-amr)
install(FILES "${speex_source_SOURCE_DIR}/COPYING" DESTINATION share/licenses/speex)
