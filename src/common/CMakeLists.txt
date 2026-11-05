project(common)

set(COMMON_FILES
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
)

# v4l2_frame_buffer.cpp reads libargus DMA buffers through NvBufSurface
if(USE_LIBARGUS_CAPTURE)
    set(TEGRA_LIB_DIR /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/tegra)
    find_library(NVBUF_SURFACE_LIBRARY nvbufsurface HINTS ${TEGRA_LIB_DIR})
    find_library(NVBUF_SURFACE_TRANSFORM_LIBRARY nvbufsurftransform HINTS ${TEGRA_LIB_DIR})
    target_link_libraries(${PROJECT_NAME}
        ${NVBUF_SURFACE_LIBRARY}
        ${NVBUF_SURFACE_TRANSFORM_LIBRARY}
    )
endif()
