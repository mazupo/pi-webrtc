project(common)

find_package(JPEG REQUIRED)
include_directories(${JPEG_INCLUDE_DIR})

set(COMMON_FILES
    ${PROJECT_SOURCE_DIR}/jpeg_util.cpp
    ${PROJECT_SOURCE_DIR}/latency_tracer.cpp
    ${PROJECT_SOURCE_DIR}/v4l2_frame_buffer.cpp
    ${PROJECT_SOURCE_DIR}/utils.cpp
    ${PROJECT_SOURCE_DIR}/worker.cpp
)

if(JETSON_PLATFORM)
    set(MULTIMEDIA_API_BASE /usr/src/jetson_multimedia_api)
endif()

add_library(${PROJECT_NAME} ${COMMON_FILES})

if(JETSON_PLATFORM)
    target_include_directories(${PROJECT_NAME} PUBLIC
        ${MULTIMEDIA_API_BASE}/include
    )
endif()

# Use libyuv in libwebrtc.a
target_link_libraries(${PROJECT_NAME}
    ${WEBRTC_LIBRARY}
    uuid
    ${JPEG_LIBRARIES}
)
