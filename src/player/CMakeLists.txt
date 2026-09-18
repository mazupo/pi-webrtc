project(player)

find_package(PkgConfig REQUIRED)

pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)
pkg_check_modules(PULSE_SIMPLE REQUIRED IMPORTED_TARGET libpulse-simple)
pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)

add_library(${PROJECT_NAME}
    pa_player.cpp
    alsa_player.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${PULSE_INCLUDE_DIRS}
    ${PULSE_SIMPLE_INCLUDE_DIRS}
    ${ALSA_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    common
    ${PULSE_LINK_LIBRARIES}
    ${PULSE_SIMPLE_LINK_LIBRARIES}
    ${ALSA_LINK_LIBRARIES}
)
