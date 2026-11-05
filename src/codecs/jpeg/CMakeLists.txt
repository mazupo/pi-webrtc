project(jpeg_codecs)

find_package(JPEG REQUIRED)

aux_source_directory(${PROJECT_SOURCE_DIR} JPEG_FILES)

add_library(${PROJECT_NAME} ${JPEG_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE ${JPEG_INCLUDE_DIR})

# Use libyuv in libwebrtc.a through common
target_link_libraries(${PROJECT_NAME} PUBLIC common ${JPEG_LIBRARIES})
