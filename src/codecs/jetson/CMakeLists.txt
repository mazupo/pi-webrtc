project(jetson_codecs)

if(USE_JETSON_HW_ENCODER)

    aux_source_directory(${PROJECT_SOURCE_DIR} FILES)

    set(MULTIMEDIA_API_BASE /usr/src/jetson_multimedia_api)
    list(APPEND FILES
        ${MULTIMEDIA_API_BASE}/samples/common/classes/NvBuffer.cpp
        ${MULTIMEDIA_API_BASE}/samples/common/classes/NvElement.cpp
        ${MULTIMEDIA_API_BASE}/samples/common/classes/NvElementProfiler.cpp
        ${MULTIMEDIA_API_BASE}/samples/common/classes/NvV4l2Element.cpp
        ${MULTIMEDIA_API_BASE}/samples/common/classes/NvV4l2ElementPlane.cpp
        ${MULTIMEDIA_API_BASE}/samples/common/classes/NvVideoDecoder.cpp
        ${MULTIMEDIA_API_BASE}/samples/common/classes/NvVideoEncoder.cpp
        # ${MULTIMEDIA_API_BASE}/samples/common/classes/NvLogging.cpp

        ${MULTIMEDIA_API_BASE}/argus/samples/utils/NativeBuffer.cpp
        ${MULTIMEDIA_API_BASE}/argus/samples/utils/nvmmapi/NvNativeBuffer.cpp
    )

    add_library(${PROJECT_NAME} ${FILES})

    target_include_directories(${PROJECT_NAME} PUBLIC
        ${MULTIMEDIA_API_BASE}/include
        ${MULTIMEDIA_API_BASE}/samples/common/classes
        ${MULTIMEDIA_API_BASE}/argus/samples/utils
    )

    find_library(NVBUF_SURFACE_LIBRARY nvbufsurface HINTS ${TEGRA_LIB_DIR})
    find_library(NVBUF_SURFACE_TRANSFORM_LIBRARY nvbufsurftransform HINTS ${TEGRA_LIB_DIR})
    find_library(NVV4L2_LIBRARY nvv4l2 HINTS ${TEGRA_LIB_DIR})

    target_link_libraries(${PROJECT_NAME}
        ${WEBRTC_LIBRARY}
        ${NVBUF_SURFACE_LIBRARY}
        ${NVBUF_SURFACE_TRANSFORM_LIBRARY}
        ${NVV4L2_LIBRARY}
    )
endif()
