add_library(tunnel_core STATIC
    include/tunnel/Protocol.hpp
    include/tunnel/Config.hpp
    include/tunnel/Forwarder.hpp
    include/tunnel/Client.hpp
    include/tunnel/Api.hpp
    src/Protocol.cpp
    src/Config.cpp
    src/Forwarder.cpp
    src/Client.cpp
    src/Api.cpp
)

target_include_directories(tunnel_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(tunnel_core PUBLIC Qt6::Core Qt6::Network Qt6::WebSockets)
set_target_properties(tunnel_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
