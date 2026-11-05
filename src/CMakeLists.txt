project(src)

add_library(${PROJECT_NAME}
    conductor.cpp
    parser.cpp
)

find_package(yaml-cpp REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC
    signaling
    rtc
    track
    recorder
    codecs
    jpeg_codecs
    ipc
    boost_program_options
    yaml-cpp
)
