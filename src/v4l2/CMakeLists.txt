project(v4l2_utils)

add_library(${PROJECT_NAME} v4l2_utils.cpp)

target_link_libraries(${PROJECT_NAME} PUBLIC common)
