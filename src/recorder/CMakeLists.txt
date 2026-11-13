project(recorder)

set(RECORDER_FILES
    ${PROJECT_SOURCE_DIR}/audio_recorder.cpp
    ${PROJECT_SOURCE_DIR}/media_query.cpp
    ${PROJECT_SOURCE_DIR}/recorder_manager.cpp
    ${PROJECT_SOURCE_DIR}/video_recorder.cpp
)

add_library(${PROJECT_NAME} ${RECORDER_FILES})

target_link_libraries(${PROJECT_NAME} PUBLIC
    capturer
    codecs
    jpeg_codecs
    avformat
    avcodec
    avutil
)
