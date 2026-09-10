cmake_minimum_required(VERSION 3.18)

# Set compilers natively
set(CMAKE_C_COMPILER "clang")
set(CMAKE_CXX_COMPILER "clang++")

project(pi-webrtc)

# General Project Configuration
set(CMAKE_CXX_STANDARD 20)
find_package(Threads REQUIRED)
set(THREADS_PREFER_PTHREAD_FLAG ON)
set(BUILD_TEST "" CACHE STRING "test")

# Global Compiler and Linker Options
add_compile_options(-pipe)

# Clang-specific options
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-rtlib=compiler-rt)
    add_link_options(-fuse-ld=lld -rtlib=compiler-rt)
endif()

add_compile_definitions(
    NDEBUG
    WEBRTC_POSIX
    WEBRTC_LINUX
    WEBRTC_USE_H264
    OPENSSL_IS_BORINGSSL
)

# Build Type Configuration
string(TOLOWER "${CMAKE_BUILD_TYPE}" BUILD_TYPE_LOWER)
if(NOT BUILD_TYPE_LOWER OR BUILD_TYPE_LOWER STREQUAL "debug")
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build (Debug or Release)" FORCE)
    add_compile_options(-g)
    add_compile_definitions(DEBUG_MODE)
    find_library(PROFILER_LIB profiler)
    if(PROFILER_LIB)
        message(STATUS "Profiler library found: ${PROFILER_LIB}")
    else()
        message(STATUS "Profiler library not found")
    endif()
else()
    add_compile_options(-O2)
endif()

# Detect Platform
if(NOT DEFINED PLATFORM)
    if(EXISTS "/etc/nv_tegra_release")
        set(PLATFORM "jetson")
    elseif(EXISTS "/usr/bin/raspi-config")
        set(PLATFORM "raspberrypi")
    endif()
endif()
message(STATUS "Target platform: ${PLATFORM}")

if(${PLATFORM} STREQUAL "jetson")
    set(JETSON_PLATFORM ON)
    set(USE_LIBARGUS_CAPTURE ON)
    set(USE_JETSON_HW_ENCODER ON)
    add_definitions(
        -DJETSON_PLATFORM
        -DUSE_LIBARGUS_CAPTURE
        -DUSE_JETSON_HW_ENCODER
    )
elseif(${PLATFORM} STREQUAL "raspberrypi")
    set(RPI_PLATFORM ON)
    set(USE_LIBCAMERA_CAPTURE ON)
    set(USE_RPI_HW_ENCODER ON)
    add_definitions(
        -DRPI_PLATFORM
        -DUSE_LIBCAMERA_CAPTURE
        -DUSE_RPI_HW_ENCODER
    )
else()
    message(FATAL_ERROR "Unknown PLATFORM: ${PLATFORM}")
endif()

# Dependencies Validation
set(WEBRTC_INCLUDE_DIR /usr/local/include/webrtc)
find_library(WEBRTC_LIBRARY_PATH webrtc PATHS /usr/local/lib REQUIRED)

# Create an IMPORTED target for WebRTC
add_library(WebRTC::WebRTC STATIC IMPORTED)
set_target_properties(WebRTC::WebRTC PROPERTIES
    IMPORTED_LOCATION "${WEBRTC_LIBRARY_PATH}"
)

# Mark WebRTC includes as SYSTEM to suppress internal warnings
target_include_directories(WebRTC::WebRTC SYSTEM INTERFACE
    "${WEBRTC_INCLUDE_DIR}"
    "${WEBRTC_INCLUDE_DIR}/third_party/abseil-cpp"
    "${WEBRTC_INCLUDE_DIR}/third_party/libyuv/include"
    "${WEBRTC_INCLUDE_DIR}/third_party/protobuf/src"
    "${WEBRTC_INCLUDE_DIR}/third_party/boringssl/src/include"
    "${WEBRTC_INCLUDE_DIR}/tools/json_schema_compiler"
)
set(WEBRTC_LIBRARY WebRTC::WebRTC)

message(STATUS "BUILD_TEST: ${BUILD_TEST}")
message(STATUS "WEBRTC_INCLUDE_DIR: ${WEBRTC_INCLUDE_DIR}")
message(STATUS "WEBRTC_LIBRARY: ${WEBRTC_LIBRARY_PATH}")

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

add_subdirectory(src/capturer)
add_subdirectory(src/codecs/h264)
add_subdirectory(src/codecs/v4l2)
add_subdirectory(src/codecs/jetson)
add_subdirectory(src/common)
add_subdirectory(src/recorder)
add_subdirectory(src/rtc)
add_subdirectory(src/ipc)
add_subdirectory(src/signaling)
add_subdirectory(src/track)

if(BUILD_TEST STREQUAL "whep")
    add_executable(test-whep test/test_whep.cpp)
    target_link_libraries(test-whep
        signaling
    )
elseif(BUILD_TEST STREQUAL "pulseaudio")
    add_executable(test-pulseaudio test/test_pulseaudio.cpp)
    target_link_libraries(test-pulseaudio
        pulse-simple pulse
    )
elseif(BUILD_TEST STREQUAL "recorder")
    add_executable(test-recorder test/test_recorder.cpp)
    target_link_libraries(test-recorder
        recorder
    )
elseif(BUILD_TEST STREQUAL "unix-socket")
    add_executable(test_unix_socket test/test_unix_socket_server.cpp)
    target_link_libraries(test_unix_socket ipc)
elseif(BUILD_TEST STREQUAL "mqtt")
    add_executable(test-mqtt test/test_mqtt.cpp)
    target_link_libraries(test-mqtt
        signaling
    )
elseif(BUILD_TEST STREQUAL "livekit")
    add_executable(test-livekit test/test_livekit.cpp)
    target_link_libraries(test-livekit
        signaling
    )
elseif(BUILD_TEST STREQUAL "openh264")
    add_executable(test-openh264 test/test_openh264.cpp)
    target_link_libraries(test-openh264
        ${WEBRTC_LIBRARY}
    )
elseif(BUILD_TEST STREQUAL "v4l2_capturer")
    add_executable(test-v4l2-capturer test/test_v4l2_capturer.cpp)
    target_link_libraries(test-v4l2-capturer
        capturer
        v4l2_codecs
    )
elseif(BUILD_TEST STREQUAL "v4l2_encoder")
    add_executable(test-v4l2-encoder test/test_v4l2_encoder.cpp)
    target_link_libraries(test-v4l2-encoder
        capturer
        v4l2_codecs
    )
elseif(BUILD_TEST STREQUAL "v4l2_decoder")
    add_executable(test-v4l2-decoder test/test_v4l2_decoder.cpp)
    target_link_libraries(test-v4l2-decoder
        capturer
        v4l2_codecs
    )
elseif(BUILD_TEST STREQUAL "v4l2_scaler")
    add_executable(test-v4l2-scaler test/test_v4l2_scaler.cpp)
    target_link_libraries(test-v4l2-scaler
        capturer
        v4l2_codecs
    )
elseif(BUILD_TEST STREQUAL "jetson_encoder")
    add_executable(test-jetson-encoder test/test_jetson_encoder.cpp)
    target_link_libraries(test-jetson-encoder
        capturer
        jetson_codecs
    )
elseif(BUILD_TEST STREQUAL "jetson_decoder")
    add_executable(test-jetson-decoder test/test_jetson_decoder.cpp)
    target_link_libraries(test-jetson-decoder
        capturer
        jetson_codecs
    )
elseif(BUILD_TEST STREQUAL "jetson_scaler")
    add_executable(test-jetson-scaler test/test_jetson_scaler.cpp)
    target_link_libraries(test-jetson-scaler
        capturer
        jetson_codecs
    )
elseif(BUILD_TEST STREQUAL "libargus")
    add_executable(test-libargus test/test_libargus.cpp)
    target_link_libraries(test-libargus
        capturer
    )
elseif(BUILD_TEST STREQUAL "libcamera")
    add_executable(test-libcamera test/test_libcamera.cpp)
    target_link_libraries(test-libcamera
        capturer
    )
else()
    add_subdirectory(src)
    add_executable(${PROJECT_NAME} src/main.cpp)
    target_link_libraries(${PROJECT_NAME}
        src
    )
endif()
