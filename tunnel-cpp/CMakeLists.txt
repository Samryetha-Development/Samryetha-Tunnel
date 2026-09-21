cmake_minimum_required(VERSION 3.21)
project(SamryethaTunnel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# macOS 上用 Homebrew 的 Qt 时自动找路径（Windows 上用 Qt 安装器或 vcpkg，传 -DCMAKE_PREFIX_PATH=...）
if(APPLE AND NOT CMAKE_PREFIX_PATH)
    find_program(BREW_EXECUTABLE brew)
    if(BREW_EXECUTABLE)
        execute_process(COMMAND ${BREW_EXECUTABLE} --prefix qt
            OUTPUT_VARIABLE _qt_prefix OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(_qt_prefix AND EXISTS "${_qt_prefix}/lib/cmake/Qt6")
            set(CMAKE_PREFIX_PATH "${_qt_prefix}")
            message(STATUS "Auto-detected Homebrew Qt at ${_qt_prefix}")
        endif()
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Network WebSockets)

option(BUILD_GUI "Build the Qt Widgets GUI" ON)
if(BUILD_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Widgets Charts)
endif()

add_subdirectory(core)
add_subdirectory(cli)
if(BUILD_GUI)
    add_subdirectory(gui)
endif()
