project(recorder)

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
    h264_codecs
    jpeg_codecs
    avformat
    avcodec
    avutil
)

if(USE_RPI_HW_ENCODER)
    target_link_libraries(${PROJECT_NAME} PUBLIC v4l2_codecs)
endif()

if(USE_JETSON_HW_ENCODER)
    target_link_libraries(${PROJECT_NAME} PUBLIC jetson_codecs)
endif()
