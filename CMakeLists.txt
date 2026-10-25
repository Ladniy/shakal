cmake_minimum_required(VERSION 3.21)

project(shakal VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Test)

qt_standard_project_setup()

qt_add_executable(shakal
    src/main.cpp
    src/Presets.h
    src/EncoderController.h
    src/EncoderController.cpp
    src/MainWindow.h
    src/MainWindow.cpp
)

target_link_libraries(shakal PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
target_include_directories(shakal PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

enable_testing()

# Pure logic (argv assembly, container suffixes) — no display required.
qt_add_executable(tst_presets
    tests/tst_presets.cpp
    src/Presets.h
    src/EncoderController.h
    src/EncoderController.cpp
)

target_link_libraries(tst_presets PRIVATE Qt6::Core)
target_include_directories(tst_presets PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_test(NAME presets COMMAND tst_presets)

# Real ffmpeg/ffprobe round-trips: success, refused overwrite, mux failure, cancel.
qt_add_executable(tst_encoder
    tests/tst_encoder.cpp
    src/Presets.h
    src/EncoderController.h
    src/EncoderController.cpp
)

target_link_libraries(tst_encoder PRIVATE Qt6::Core Qt6::Test)
target_include_directories(tst_encoder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_test(NAME encoder COMMAND tst_encoder)
