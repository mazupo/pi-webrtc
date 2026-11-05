project(v4l2_codecs)

aux_source_directory(${PROJECT_SOURCE_DIR} ENCODER_FILES)

add_library(${PROJECT_NAME} ${ENCODER_FILES})

target_link_libraries(${PROJECT_NAME} PUBLIC common v4l2_utils)
