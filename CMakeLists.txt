cmake_minimum_required(VERSION 3.12)

project(system_cover
    VERSION 0.2.0
    LANGUAGES C
)

add_library(system_cover SHARED
    system_cover.c
)

target_include_directories(system_cover PRIVATE
    ${QEMU_ROOT}/include/qemu
)
