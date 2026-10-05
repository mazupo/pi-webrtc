project(recorder)

find_package(JPEG REQUIRED)
include_directories(${JPEG_INCLUDE_DIR})

set(RECORDER_FILES
    ${PROJECT_SOURCE_DIR}/audio_recorder.cpp
    ${PROJECT_SOURCE_DIR}/media_query.cpp
    ${PROJECT_SOURCE_DIR}/openh264_recorder.cpp
    ${PROJECT_SOURCE_DIR}/recorder_manager.cpp
    ${PROJECT_SOURCE_DIR}/video_recorder.cpp
)

if(USE_RPI_HW_ENCODER)
    list(APPEND RECORDER_FILES
        ${PROJECT_SOURCE_DIR}/v4l2_h264_recorder.cpp
    )
endif()

if(USE_JETSON_HW_ENCODER)
    list(APPEND RECORDER_FILES
        ${PROJECT_SOURCE_DIR}/jetson_recorder.cpp
    )
endif()

add_library(${PROJECT_NAME} ${RECORDER_FILES})

target_link_libraries(${PROJECT_NAME} PUBLIC
    capturer
    v4l2_codecs
    h264_codecs
    avformat
    avcodec
    avutil
    swscale
    ${JPEG_LIBRARIES}
)
