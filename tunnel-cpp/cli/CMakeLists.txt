add_executable(tunnel-cli main.cpp)
target_link_libraries(tunnel-cli PRIVATE tunnel_core)
set_target_properties(tunnel-cli PROPERTIES OUTPUT_NAME tunnel-cli)
