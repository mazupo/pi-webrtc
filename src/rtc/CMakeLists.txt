project(rtc)

# Use protoc matching WebRTC's bundled protobuf (v33) to avoid version conflicts
find_program(PROTOC_EXECUTABLE protoc)
if(NOT PROTOC_EXECUTABLE)
  message(FATAL_ERROR "protoc not found. Install protoc v33 matching WebRTC's bundled protobuf.")
endif()
execute_process(
  COMMAND ${PROTOC_EXECUTABLE} --version
  OUTPUT_VARIABLE PROTOC_VERSION_RAW
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REGEX MATCH "[0-9]+\\.[0-9]+" PROTOC_VERSION "${PROTOC_VERSION_RAW}")
message(STATUS "Using protoc: ${PROTOC_EXECUTABLE} (${PROTOC_VERSION_RAW})")
if(PROTOC_VERSION VERSION_LESS "33.0")
  message(FATAL_ERROR "protoc >= 33.0 is required to match WebRTC's bundled protobuf, found ${PROTOC_VERSION}")
endif()

# well-known proto include path (e.g. google/protobuf/empty.proto)
get_filename_component(PROTOC_DIR ${PROTOC_EXECUTABLE} DIRECTORY)
get_filename_component(PROTOC_PREFIX ${PROTOC_DIR} DIRECTORY)
set(PROTOC_INCLUDE_DIR "${PROTOC_PREFIX}/include")

# path of the proto files
set(LIVEKIT_PROTO_DIR "${CMAKE_SOURCE_DIR}/external/livekit-protocol/protobufs")
set(APP_PROTO_DIR "${CMAKE_SOURCE_DIR}/external/protocol/protos")
set(PROTO_OUT_DIR "${CMAKE_SOURCE_DIR}/proto")

set(PROTO_SRCS "")

# Generate well-known types not included in libwebrtc.a (Timestamp, Duration)
set(WELL_KNOWN_PROTOS timestamp duration)
foreach(wk_name ${WELL_KNOWN_PROTOS})
  set(wk_proto "${PROTOC_INCLUDE_DIR}/google/protobuf/${wk_name}.proto")
  set(wk_cc "${PROTO_OUT_DIR}/google/protobuf/${wk_name}.pb.cc")
  set(wk_h "${PROTO_OUT_DIR}/google/protobuf/${wk_name}.pb.h")
  add_custom_command(
    OUTPUT ${wk_cc} ${wk_h}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PROTO_OUT_DIR}
    COMMAND ${PROTOC_EXECUTABLE}
    ARGS --cpp_out=lite:${PROTO_OUT_DIR}
    -I ${PROTOC_INCLUDE_DIR}
    ${wk_proto}
    DEPENDS ${wk_proto}
  )
  list(APPEND PROTO_SRCS ${wk_cc})
endforeach()

# generate `*.pb.cc` and `*.pb.h` to `proto/`, keeping the path relative to its proto dir
macro(generate_proto _dir _rel)
  string(REGEX REPLACE "\\.proto$" "" _base "${_rel}")
  set(_cc "${PROTO_OUT_DIR}/${_base}.pb.cc")
  set(_h "${PROTO_OUT_DIR}/${_base}.pb.h")
  add_custom_command(
    OUTPUT ${_cc} ${_h}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PROTO_OUT_DIR}
    COMMAND ${PROTOC_EXECUTABLE}
    ARGS --cpp_out=lite:${PROTO_OUT_DIR}
    -I ${_dir}
    -I ${PROTOC_INCLUDE_DIR}
    "${_dir}/${_rel}"
    DEPENDS "${_dir}/${_rel}"
  )
  list(APPEND PROTO_SRCS ${_cc})
endmacro()

set(LIVEKIT_PROTOS
  livekit_models.proto
  livekit_metrics.proto
  logger/options.proto
)
foreach(livekit_proto ${LIVEKIT_PROTOS})
  generate_proto("${LIVEKIT_PROTO_DIR}" "${livekit_proto}")
endforeach()

file(GLOB APP_PROTOS RELATIVE "${APP_PROTO_DIR}" "${APP_PROTO_DIR}/*.proto")
foreach(app_proto ${APP_PROTOS})
  generate_proto("${APP_PROTO_DIR}" "${app_proto}")
endforeach()

aux_source_directory(${PROJECT_SOURCE_DIR} RTC_FILES)
aux_source_directory(${PROJECT_SOURCE_DIR}/datachannel DATACHANNEL_FILES)

add_library(${PROJECT_NAME}
  ${RTC_FILES}
  ${DATACHANNEL_FILES}
  ${PROTO_SRCS}
)

target_include_directories(${PROJECT_NAME} PUBLIC
  ${CMAKE_SOURCE_DIR}
  ${PROTO_OUT_DIR}
)

target_link_libraries(${PROJECT_NAME} PUBLIC
  track
  ipc
  recorder
  ${WEBRTC_LIBRARY}
)

if(USE_JETSON_HW_ENCODER)
  target_link_libraries(${PROJECT_NAME} PUBLIC
    jetson_codecs
  )
endif()

if(USE_RPI_HW_ENCODER)
  target_link_libraries(${PROJECT_NAME} PUBLIC
    v4l2_codecs
  )
endif()
