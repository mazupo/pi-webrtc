project(h264_codecs)

aux_source_directory(${PROJECT_SOURCE_DIR} H264_FILES)

add_library(${PROJECT_NAME} ${H264_FILES})

# Use the OpenH264 library in libwebrtc.a
target_link_libraries(${PROJECT_NAME} PUBLIC common ${WEBRTC_LIBRARY})
