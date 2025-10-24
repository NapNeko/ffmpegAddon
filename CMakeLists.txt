cmake_minimum_required(VERSION 3.15)
project(ffmpegAddon)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# FFmpeg 自动检测
set(FFMPEG_OUT_DIR "${CMAKE_SOURCE_DIR}/ffmpeg_out")

if(NOT EXISTS "${FFMPEG_OUT_DIR}")
    message(FATAL_ERROR "ffmpeg_out/ not found — please build FFmpeg there first.")
endif()

set(FFMPEG_INCLUDE_DIR "${FFMPEG_OUT_DIR}/include")
set(FFMPEG_LIB_DIR "${FFMPEG_OUT_DIR}/lib")
include_directories(${FFMPEG_INCLUDE_DIR})
link_directories(${FFMPEG_LIB_DIR})
message(STATUS "Using FFmpeg from: ${FFMPEG_OUT_DIR}")

# Napi自动检测
execute_process(
        COMMAND node -p "require('node-addon-api').include"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE NODE_ADDON_API_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
string(REGEX REPLACE "^\"(.*)\"$" "\\1" NODE_ADDON_API_DIR "${NODE_ADDON_API_DIR}")
if(EXISTS "${NODE_ADDON_API_DIR}/napi.h")
    message(STATUS "Found node-addon-api include dir: ${NODE_ADDON_API_DIR}")
    include_directories(${NODE_ADDON_API_DIR})
else()
    message(FATAL_ERROR "Cannot find napi.h — please run: npm install node-addon-api")
endif()

# 源文件与 Node Addon 定义
# 显式列出 FFmpeg Addon 源文件（模块化结构）
set(ADDON_SOURCES
    src/ffmpegAddon.cpp
    src/getDuration.cpp
    src/decodeAudio.cpp
    src/videoInfo.cpp
    src/convertNTSilk.cpp
)

# 添加 silk-v3-decoder silk/interface 和 silk/src 源文件
set(SILK_INTERFACE_DIR "${CMAKE_SOURCE_DIR}/silk-v3-decoder/silk/interface")
set(SILK_SRC_DIR "${CMAKE_SOURCE_DIR}/silk-v3-decoder/silk/src")
file(GLOB SILK_SRC_FILES
    "${SILK_INTERFACE_DIR}/*.c" "${SILK_INTERFACE_DIR}/*.S"
    "${SILK_SRC_DIR}/*.c" "${SILK_SRC_DIR}/*.S"
)

# 合并所有源文件
set(SRC ${ADDON_SOURCES} ${SILK_SRC_FILES})
add_library(${PROJECT_NAME} SHARED ${SRC} ${CMAKE_JS_SRC})
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX ".node")


target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_JS_INC}  # cmake-js 自动提供 Node/N-API 头文件路径
    ${SILK_INTERFACE_DIR}
    ${SILK_SRC_DIR}
)

# 自动检测 FFmpeg 库 (.a / .lib)
set(_ffmpeg_libs avformat avcodec avutil swresample swscale)
set(_found_libs "")
foreach(lib IN LISTS _ffmpeg_libs)
    set(_cand_a "${FFMPEG_LIB_DIR}/lib${lib}.a")
    set(_cand_lib "${FFMPEG_LIB_DIR}/${lib}.lib")
    if(EXISTS "${_cand_a}")
        list(APPEND _found_libs "${_cand_a}")
        message(STATUS "Found FFmpeg static lib: ${_cand_a}")
    elseif(EXISTS "${_cand_lib}")
        list(APPEND _found_libs "${_cand_lib}")
        message(STATUS "Found FFmpeg import lib: ${_cand_lib}")
    else()
        message(WARNING "FFmpeg lib missing: ${lib}")
    endif()
endforeach()

# 链接 FFmpeg
if(_found_libs)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${_found_libs})
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_FFMPEG=1)
else()
    message(WARNING "No FFmpeg libraries found in ${FFMPEG_LIB_DIR}")
endif()

# 链接 Node.js 运行时库（由 cmake-js 提供）
if(CMAKE_JS_LIB)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_JS_LIB})
endif()

# 编译定义
target_compile_definitions(${PROJECT_NAME} PRIVATE NAPI_VERSION=8)

# Windows 平台处理
if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _WIN32 WIN32_LEAN_AND_MEAN)
    set(_syslibs Mfuuid Strmiids Mfplat Bcrypt Secur32)
    if(MSVC)
        target_link_libraries(${PROJECT_NAME} PRIVATE ${_syslibs})
    else()
        foreach(l IN LISTS _syslibs)
            target_link_options(${PROJECT_NAME} PRIVATE "-l${l}")
        endforeach()
    endif()
endif()

# MSVC 编译警告屏蔽
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /wd4251 /wd4275)
endif()
