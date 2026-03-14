cmake_minimum_required(VERSION 3.20)
project(ds5_edge_relay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Строгие предупреждения — production-стиль
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    -fstack-protector-strong
)

add_executable(ds5-edge-relay
    src/main.cpp
    # header-only — подключаются через include
)

target_include_directories(ds5-edge-relay PRIVATE src)

# Release: оптимизация + LTO
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(ds5-edge-relay PRIVATE -O3 -march=native)
    set_target_properties(ds5-edge-relay PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

install(TARGETS ds5-edge-relay RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
