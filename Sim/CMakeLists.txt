add_executable(GBSim src/main.cpp)
target_link_libraries(GBSim PRIVATE GB::Sim)   # CPU only: no GL, no GLFW
gb_parallel_compile(GBSim)

target_compile_definitions(GBSim PRIVATE GB_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
