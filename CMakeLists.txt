cmake_minimum_required(VERSION 3.20)
project(ds5-edge-relay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt6 автоматическая обработка moc/uic/rcc
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets DBus)

add_compile_options(
    -Wall -Wextra
    -Wno-unused-parameter
    -Wno-unused-result
)

# Раскомментируй чтобы включить дамп Edge-кнопок в stderr:
# add_compile_definitions(EDGE_BTN_DEBUG)

add_executable(ds5-edge-relay
    src/main.cpp
    src/mainwindow.cpp
    src/relayworker.cpp
    src/gamepadwidget.cpp
    src/bindeditorwidget.cpp
    src/bindstorage.cpp
    resources.qrc
)

target_include_directories(ds5-edge-relay PRIVATE
    src
    relay-core
)

target_link_libraries(ds5-edge-relay PRIVATE
    Qt6::Widgets
    Qt6::DBus
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(ds5-edge-relay PRIVATE -O3)
    set_target_properties(ds5-edge-relay PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()
