add_executable(Sandbox src/SandboxAPP.cpp)
target_link_libraries(Sandbox PRIVATE GB::GB)
target_compile_definitions(Sandbox PRIVATE GB_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
