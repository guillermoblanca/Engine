add_executable(GBSim src/main.cpp)
target_link_libraries(GBSim PRIVATE GB::GB)

target_compile_definitions(GBSim PRIVATE GB_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
