project(rtc)

aux_source_directory(${PROJECT_SOURCE_DIR} RTC_FILES)
aux_source_directory(${PROJECT_SOURCE_DIR}/datachannel DATACHANNEL_FILES)

add_library(${PROJECT_NAME}
  ${RTC_FILES}
  ${DATACHANNEL_FILES}
)

target_include_directories(${PROJECT_NAME} PUBLIC
  ${CMAKE_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PUBLIC
  proto
  codecs
  track
  ipc
  jpeg_codecs
  player
  recorder
  ${WEBRTC_LIBRARY}
)
