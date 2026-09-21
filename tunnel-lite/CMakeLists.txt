cmake_minimum_required(VERSION 3.16)
project(tunnel-lite LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# wss:// 需要 OpenSSL；纯 IP 中转（ws://）不需要，默认关闭以保持最小体积
option(TUNNEL_LITE_TLS "Enable TLS (wss://) via OpenSSL" OFF)

find_package(Threads REQUIRED)

add_executable(tunnel-lite src/main.cpp src/net.cpp)
target_include_directories(tunnel-lite PRIVATE src)
target_link_libraries(tunnel-lite PRIVATE Threads::Threads)

if(TUNNEL_LITE_TLS)
    find_package(OpenSSL REQUIRED)
    target_compile_definitions(tunnel-lite PRIVATE TUNNEL_LITE_TLS)
    target_link_libraries(tunnel-lite PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

if(WIN32)
    target_link_libraries(tunnel-lite PRIVATE ws2_32)
endif()
