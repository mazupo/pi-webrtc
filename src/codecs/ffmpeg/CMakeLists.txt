project(ffmpeg_codecs)

aux_source_directory(${PROJECT_SOURCE_DIR} FFMPEG_FILES)

add_library(${PROJECT_NAME} ${FFMPEG_FILES})

target_link_libraries(${PROJECT_NAME} PUBLIC common avcodec avutil)
