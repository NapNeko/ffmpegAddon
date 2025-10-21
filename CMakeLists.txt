cmake_minimum_required(VERSION 3.15)
project(ffmpegAddon)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optionally build/consume FFmpeg sources from a provided path.
option(BUILD_FFMPEG_FROM_SOURCE "Run an external build of FFmpeg from source (requires MSYS2 / bash on Windows)" OFF)
set(FFMPEG_SOURCE_DIR "" CACHE PATH "Path to local FFmpeg source or built include/lib folder")

# If the user didn't pass a FFMPEG_SOURCE_DIR but a local buildout exists in the project,
# prefer that as a sensible default so both MSVC and MinGW builds find the trimmed FFmpeg.
if(NOT FFMPEG_SOURCE_DIR OR FFMPEG_SOURCE_DIR STREQUAL "")
    if(EXISTS "${CMAKE_SOURCE_DIR}/buildout")
        set(FFMPEG_SOURCE_DIR "${CMAKE_SOURCE_DIR}/buildout" CACHE PATH "Path to local FFmpeg source or built include/lib folder" FORCE)
        message(STATUS "Auto-detected local ffmpeg install: ${FFMPEG_SOURCE_DIR}")
    endif()
endif()

# Get Node.js headers from node-api-headers package
execute_process(
    COMMAND node -p "require('node-api-headers').include_dir"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE NODE_API_HEADERS_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NODE_API_HEADERS_DIR)
    # remove surrounding double quotes if node printed them for any reason
    string(REGEX REPLACE "^\"(.*)\"$" "\\1" NODE_API_HEADERS_DIR "${NODE_API_HEADERS_DIR}")
endif()

# Get Node-API C++ wrapper from node-addon-api package
execute_process(
    COMMAND node -p "require('node-addon-api').include"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE NODE_ADDON_API_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NODE_ADDON_API_DIR)
    string(REGEX REPLACE "^\"(.*)\"$" "\\1" NODE_ADDON_API_DIR "${NODE_ADDON_API_DIR}")
endif()

# Include cmake-js headers (if available)
if(CMAKE_JS_INC)
    include_directories(${CMAKE_JS_INC})
endif()

# Source files
file(GLOB SOURCE_FILES "src/*.cpp" "src/*.h")

# Create the addon
add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES} ${CMAKE_JS_SRC})
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX ".node")

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${NODE_API_HEADERS_DIR}
    ${NODE_ADDON_API_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

# If a FFMPEG_SOURCE_DIR was provided, add its include/lib dirs to help find headers/libs
if(FFMPEG_SOURCE_DIR)
    message(STATUS "FFMPEG_SOURCE_DIR provided: ${FFMPEG_SOURCE_DIR}")
    # prefer include/ and lib/ under the provided dir
    target_include_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_SOURCE_DIR}/include)
    link_directories(${FFMPEG_SOURCE_DIR}/lib)
endif()

# Try to find FFmpeg headers (avformat/avcodec/avutil) in provided source dir or system paths
set(_ffmpeg_include_candidates
    "${FFMPEG_SOURCE_DIR}/include"
    "${FFMPEG_SOURCE_DIR}/include/libavformat"
    "${CMAKE_SOURCE_DIR}/buildout/include"
    "C:/ffmpeg/include"
    "/usr/include"
    "/usr/local/include"
)

find_path(AVFORMAT_INCLUDE_DIR
    NAMES avformat/avformat.h
    PATHS ${_ffmpeg_include_candidates}
    NO_DEFAULT_PATH
)

# If find_path failed, try a manual existence check in common include subfolders
if(NOT AVFORMAT_INCLUDE_DIR)
    foreach(_cand IN LISTS _ffmpeg_include_candidates)
        if(EXISTS "${_cand}/avformat/avformat.h")
            set(AVFORMAT_INCLUDE_DIR "${_cand}" CACHE PATH "FFmpeg include (manual)" FORCE)
            message(STATUS "Found FFmpeg headers (manual): ${AVFORMAT_INCLUDE_DIR}")
            break()
        elseif(EXISTS "${_cand}/libavformat/avformat.h")
            set(AVFORMAT_INCLUDE_DIR "${_cand}" CACHE PATH "FFmpeg include (manual)" FORCE)
            message(STATUS "Found FFmpeg headers (manual): ${AVFORMAT_INCLUDE_DIR}")
            break()
        endif()
    endforeach()
endif()

if(AVFORMAT_INCLUDE_DIR)
    message(STATUS "Found FFmpeg headers: ${AVFORMAT_INCLUDE_DIR}")
    target_include_directories(${PROJECT_NAME} PRIVATE ${AVFORMAT_INCLUDE_DIR})
    # Try to find libraries (static or shared)
    # prepare likely library directories
    set(_ffmpeg_lib_candidates
        "${FFMPEG_SOURCE_DIR}/lib"
        "${FFMPEG_SOURCE_DIR}/lib64"
        "${CMAKE_SOURCE_DIR}/buildout/lib"
        "C:/ffmpeg/lib"
        "/usr/lib"
        "/usr/local/lib"
    )

    find_library(AVFORMAT_LIB avformat PATHS ${_ffmpeg_lib_candidates})
    find_library(AVCODEC_LIB avcodec PATHS ${_ffmpeg_lib_candidates})
    find_library(AVUTIL_LIB avutil PATHS ${_ffmpeg_lib_candidates})
    find_library(SWRESAMPLE_LIB swresample PATHS ${_ffmpeg_lib_candidates})
    find_library(SWSCALE_LIB swscale PATHS ${_ffmpeg_lib_candidates})

    # If find_library failed (common with plain .a files), try manual file checks
    if(NOT AVFORMAT_LIB)
        foreach(_cand IN LISTS _ffmpeg_lib_candidates)
            if(EXISTS "${_cand}/libavformat.a")
                set(AVFORMAT_LIB "${_cand}/libavformat.a")
                break()
            elseif(EXISTS "${_cand}/avformat.lib")
                set(AVFORMAT_LIB "${_cand}/avformat.lib")
                break()
            endif()
        endforeach()
    endif()
    if(NOT AVCODEC_LIB)
        foreach(_cand IN LISTS _ffmpeg_lib_candidates)
            if(EXISTS "${_cand}/libavcodec.a")
                set(AVCODEC_LIB "${_cand}/libavcodec.a")
                break()
            elseif(EXISTS "${_cand}/avcodec.lib")
                set(AVCODEC_LIB "${_cand}/avcodec.lib")
                break()
            endif()
        endforeach()
    endif()
    if(NOT AVUTIL_LIB)
        foreach(_cand IN LISTS _ffmpeg_lib_candidates)
            if(EXISTS "${_cand}/libavutil.a")
                set(AVUTIL_LIB "${_cand}/libavutil.a")
                break()
            elseif(EXISTS "${_cand}/avutil.lib")
                set(AVUTIL_LIB "${_cand}/avutil.lib")
                break()
            endif()
        endforeach()
    endif()
    if(NOT SWRESAMPLE_LIB)
        foreach(_cand IN LISTS _ffmpeg_lib_candidates)
            if(EXISTS "${_cand}/libswresample.a")
                set(SWRESAMPLE_LIB "${_cand}/libswresample.a")
                break()
            elseif(EXISTS "${_cand}/swresample.lib")
                set(SWRESAMPLE_LIB "${_cand}/swresample.lib")
                break()
            endif()
        endforeach()
    endif()
    if(NOT SWSCALE_LIB)
        foreach(_cand IN LISTS _ffmpeg_lib_candidates)
            if(EXISTS "${_cand}/libswscale.a")
                set(SWSCALE_LIB "${_cand}/libswscale.a")
                break()
            elseif(EXISTS "${_cand}/swscale.lib")
                set(SWSCALE_LIB "${_cand}/swscale.lib")
                break()
            endif()
        endforeach()
    endif()

    if(AVFORMAT_LIB AND AVCODEC_LIB AND AVUTIL_LIB)
        message(STATUS "Found FFmpeg libraries: ${AVFORMAT_LIB}, ${AVCODEC_LIB}, ${AVUTIL_LIB}")
        # If the found libraries are MinGW-style (.a) but we're building with MSVC, linking will fail.
        get_filename_component(_avf_ext ${AVFORMAT_LIB} EXT)
        if(MSVC AND _avf_ext STREQUAL "a")
            message(FATAL_ERROR "Found FFmpeg libraries in .a format (MinGW). MSVC cannot link .a archives.\nEither: (A) build the addon using MinGW (recommended when buildout was built with MSYS2/Mingw), or (B) provide MSVC-compatible FFmpeg dev libs (.lib).\nSet -DFFMPEG_SOURCE_DIR to a folder containing MSVC .lib files, or run the build in a MinGW environment using cmake-js in MSYS2/ MINGW64.")
        endif()
        # Link FFmpeg libraries. When using MSVC we keep the normal linking
        # behavior (MSVC cannot link .a archives and we already error out earlier
        # in that case). When using the GNU toolchain (MinGW) static .a
        # libraries sometimes require --whole-archive so that the linker
        # pulls all object files from the archive into the shared module.
        # Build an explicit list of found libs (filter out empty results)
        set(_ffmpeg_libs_all ${AVFORMAT_LIB} ${AVCODEC_LIB} ${AVUTIL_LIB} ${SWRESAMPLE_LIB} ${SWSCALE_LIB})
        set(_ffmpeg_libs "")
        foreach(_l IN LISTS _ffmpeg_libs_all)
            if(_l)
                list(APPEND _ffmpeg_libs ${_l})
            endif()
        endforeach()

        # Diagnostic output: show exactly which library files we resolved
        if(_ffmpeg_libs)
            message(STATUS "Resolved FFmpeg library paths:")
            foreach(_l IN LISTS _ffmpeg_libs)
                get_filename_component(_lname ${_l} NAME)
                get_filename_component(_lext ${_l} EXT)
                message(STATUS "  - ${_l} (name=${_lname}, ext=${_lext})")
            endforeach()
        else()
            message(WARNING "No FFmpeg libraries were found by find_library().")
        endif()

        if(MSVC)
            message(STATUS "Using MSVC toolchain; linking against MSVC-compatible FFmpeg libs if provided")
            target_link_libraries(${PROJECT_NAME} PRIVATE ${_ffmpeg_libs})
        else()
            message(STATUS "Using non-MSVC toolchain (likely MinGW). Handling static (.a) archives carefully")

            # Build an ordered list of ffmpeg libs (only those found)
            set(_ordered_ffmpeg_libs "")
            foreach(_name IN ITEMS ${AVFORMAT_LIB} ${AVCODEC_LIB} ${SWRESAMPLE_LIB} ${SWSCALE_LIB} ${AVUTIL_LIB})
                if(_name)
                    list(APPEND _ordered_ffmpeg_libs ${_name})
                endif()
            endforeach()

            # Partition ordered list into static (.a) and non-static
            set(_static_ffmpeg_libs "")
            set(_shared_ffmpeg_libs "")
            foreach(_l IN LISTS _ordered_ffmpeg_libs)
                get_filename_component(_lext ${_l} EXT)
                if(_lext STREQUAL "a")
                    list(APPEND _static_ffmpeg_libs ${_l})
                else()
                    list(APPEND _shared_ffmpeg_libs ${_l})
                endif()
            endforeach()

            message(STATUS "FFmpeg static libs to whole-archive: ${_static_ffmpeg_libs}")
            message(STATUS "FFmpeg shared/libs to link normally: ${_shared_ffmpeg_libs}")

            # Link static .a libs wrapped with --whole-archive if any
            if(_static_ffmpeg_libs)
                message(STATUS "Enabling --whole-archive for static ffmpeg libs")
                target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--whole-archive")
                target_link_libraries(${PROJECT_NAME} PRIVATE ${_static_ffmpeg_libs})
                target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--no-whole-archive")
            endif()

            # Link non-static ffmpeg libs
            if(_shared_ffmpeg_libs)
                target_link_libraries(${PROJECT_NAME} PRIVATE ${_shared_ffmpeg_libs})
            endif()

            # Common Windows system libraries that FFmpeg objects may depend on.
            # Common Windows system libraries that FFmpeg objects may depend on.
            # Add libraries frequently required by FFmpeg builds (bz2 for bzip2,
            # ncrypt/crypt32/ole32 for Windows crypto and cert functions).
            set(_ffmpeg_sys_libs ws2_32 winmm shlwapi advapi32 bcrypt ncrypt crypt32 ole32 bz2)
            message(STATUS "Also linking system libraries: ${_ffmpeg_sys_libs}")
            target_link_libraries(${PROJECT_NAME} PRIVATE ${_ffmpeg_sys_libs})
        endif()

        target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_FFMPEG=1)
    else()
        message(WARNING "FFmpeg headers found but one or more libs missing. You can set FFMPEG_SOURCE_DIR to a folder with include/ and lib/ or build FFmpeg from source.")
    endif()
else()
    message(STATUS "FFmpeg headers not found. The addon will build stub APIs that will instruct how to provide FFmpeg.")
endif()

# Link libraries
if(CMAKE_JS_LIB)
    if(MSVC)
        message(STATUS "Linking CMAKE_JS_LIB for MSVC: ${CMAKE_JS_LIB}")
        target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_JS_LIB})
    else()
        # CMake-js may provide MSVC-specific flags (e.g. /DELAYLOAD:NODE.EXE) which
        # the GNU linker treats as filenames. Filter those out and only pass
        # acceptable tokens (paths ending with .lib/.a/.dll.a, or -l flags).
        message(STATUS "Sanitizing CMAKE_JS_LIB for non-MSVC toolchain: ${CMAKE_JS_LIB}")
        # Normalize Windows backslashes to forward slashes so tokens survive
        string(REPLACE "\\\\" "/" _cmake_js_lib_path "${CMAKE_JS_LIB}")
        # Split on whitespace into a CMake list. This is best-effort; tokens containing
        # spaces will be split — if you have paths with spaces, provide them quoted via CMAKE.
        string(REGEX REPLACE "[\t\n\r ]+" ";" _cmake_js_lib_list "${_cmake_js_lib_path}")
        set(_sanitized_cmake_js_lib "")
        foreach(_tok IN LISTS _cmake_js_lib_list)
            string(TOUPPER _utok "${_tok}")
            # Skip MSVC-style options that start with '/'
            if(_tok MATCHES "^/" OR _utok MATCHES "^/DELAYLOAD:.*")
                message(VERBOSE "  Skipping MSVC flag from CMAKE_JS_LIB: ${_tok}")
                continue()
            endif()

            # Keep tokens that look like library files or -l flags
            if(_tok MATCHES ".*\\.(lib|a|dll\\.a)$" OR _tok MATCHES "^-l.*")
                list(APPEND _sanitized_cmake_js_lib ${_tok})
            else()
                # If token is a plain filename like 'node.lib' or a path, keep it
                if(_tok MATCHES ".*node.*" OR _tok MATCHES ".*\.lib$" OR _tok MATCHES ".*\.a$")
                    list(APPEND _sanitized_cmake_js_lib ${_tok})
                else()
                    message(VERBOSE "  Dropping unknown CMAKE_JS_LIB token for GNU: ${_tok}")
                endif()
            endif()
        endforeach()

        if(_sanitized_cmake_js_lib)
            message(STATUS "Linking sanitized CMAKE_JS_LIB tokens: ${_sanitized_cmake_js_lib}")
            target_link_libraries(${PROJECT_NAME} PRIVATE ${_sanitized_cmake_js_lib})
        else()
            message(WARNING "After sanitizing, no valid CMAKE_JS_LIB tokens remained for non-MSVC toolchain. You may need to provide a MinGW-compatible node import lib (e.g. libnode.dll.a) or use -l flags.")
        endif()
    endif()
endif()

# Sanitize any MSVC-specific shared linker flags when using non-MSVC toolchain
if(NOT MSVC AND CMAKE_SHARED_LINKER_FLAGS)
    message(STATUS "Sanitizing CMAKE_SHARED_LINKER_FLAGS for non-MSVC toolchain: ${CMAKE_SHARED_LINKER_FLAGS}")
    # normalize backslashes
    string(REPLACE "\\\\" "/" _shflags_path "${CMAKE_SHARED_LINKER_FLAGS}")
    # split on whitespace
    string(REGEX REPLACE "[\t\n\r ]+" ";" _shflags_list "${_shflags_path}")
    set(_san_shared_flags "")
    foreach(_tok IN LISTS _shflags_list)
        if(_tok MATCHES "^/")
            message(VERBOSE "  Dropping MSVC-only shared linker flag: ${_tok}")
            continue()
        endif()
        list(APPEND _san_shared_flags ${_tok})
    endforeach()
    if(_san_shared_flags)
        string(REPLACE ";" " " _san_shared_flags_str "${_san_shared_flags}")
        message(STATUS "Using sanitized shared linker flags: ${_san_shared_flags_str}")
        # Overwrite the cache variable so subsequent CMake runs pick it up
        set(CMAKE_SHARED_LINKER_FLAGS "${_san_shared_flags_str}" CACHE STRING "Sanitized shared linker flags" FORCE)
    else()
        message(STATUS "No usable shared linker flags remain after sanitization.")
        set(CMAKE_SHARED_LINKER_FLAGS "" CACHE STRING "Sanitized shared linker flags" FORCE)
    endif()
endif()

# Compile definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE NAPI_VERSION=8)

if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _WIN32 WIN32_LEAN_AND_MEAN)
endif()

# Compiler options
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE 
        /wd4251  # Disable DLL interface warnings
        /wd4275
    )
endif()