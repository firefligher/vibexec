cmake_minimum_required(VERSION 3.25.1)

project(
    vibexec
    VERSION 1.0
    LANGUAGES C
)

include(FindOpenAL)
include(FindPkgConfig)

pkg_check_modules(KISSFFT REQUIRED kissfft-float)

add_executable(
    vibexec
    src/main.c src/player.c src/scheduler.c src/vibeomatic.c
)

target_include_directories(
    vibexec
    SYSTEM
    PRIVATE ${OPENAL_INCLUDE_DIR}
)

target_link_libraries(
    vibexec
    ${OPENAL_LIBRARY}
    ${KISSFFT_LIBRARIES}
)

set(
    CMAKE_C_FLAGS_DEBUG
    "${CMAKE_C_FLAGS_RELEASE} ${KISSFFT_CFLAGS}"
)

set(
    CMAKE_C_FLAGS_RELEASE
    "${CMAKE_C_FLAGS_RELEASE} ${KISSFFT_CFLAGS}"
)
