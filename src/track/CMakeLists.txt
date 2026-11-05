project(track)

aux_source_directory(${PROJECT_SOURCE_DIR} TRACK_FILES)

add_library(${PROJECT_NAME} ${TRACK_FILES})

target_link_libraries(${PROJECT_NAME} PUBLIC capturer codecs)
