project(codecs)

add_subdirectory(ffmpeg)
add_subdirectory(h264)
add_subdirectory(jetson)
add_subdirectory(jpeg)
add_subdirectory(v4l2)

# The factories pick the hardware backend for this platform, so callers link codecs only.
add_library(${PROJECT_NAME}
    decoder_factory.cpp
    encoder_factory.cpp
    scaler_factory.cpp
    tracing_video_encoder.cpp
)

target_link_libraries(${PROJECT_NAME}
    PUBLIC common ${WEBRTC_LIBRARY}
    PRIVATE ffmpeg_codecs h264_codecs jpeg_codecs
)

if(USE_RPI_HW_ENCODER)
    target_link_libraries(${PROJECT_NAME} PRIVATE v4l2_codecs)
endif()

if(USE_JETSON_HW_ENCODER)
    target_link_libraries(${PROJECT_NAME} PRIVATE jetson_codecs)
endif()
