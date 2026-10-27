add_executable(GBTests smoke.cpp)
target_link_libraries(GBTests PRIVATE GB::GB)
add_test(NAME smoke COMMAND GBTests)

add_executable(GBGLTests gl_render.cpp)
target_link_libraries(GBGLTests PRIVATE GB::GB)
add_test(NAME gl_render COMMAND GBGLTests)
set_tests_properties(gl_render PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBSceneTests gl_scene.cpp)
target_link_libraries(GBSceneTests PRIVATE GB::GB)
add_test(NAME gl_scene COMMAND GBSceneTests)
set_tests_properties(gl_scene PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBImageTests image_test.cpp)
target_link_libraries(GBImageTests PRIVATE GB::GB)
target_compile_definitions(GBImageTests PRIVATE GB_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME image COMMAND GBImageTests)

add_executable(GBGltfTests gltf_test.cpp)
target_link_libraries(GBGltfTests PRIVATE GB::GB)
target_compile_definitions(GBGltfTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME gltf COMMAND GBGltfTests)
set_tests_properties(gltf PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBAnimationTests animation_test.cpp)
target_link_libraries(GBAnimationTests PRIVATE GB::GB)
target_compile_definitions(GBAnimationTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME animation COMMAND GBAnimationTests)
set_tests_properties(animation PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBCameraTests camera_test.cpp)
target_link_libraries(GBCameraTests PRIVATE GB::GB)
add_test(NAME camera COMMAND GBCameraTests)

add_executable(GBParticleTests particles_test.cpp)
target_link_libraries(GBParticleTests PRIVATE GB::GB)
add_test(NAME particles COMMAND GBParticleTests)

add_executable(GBWeatherTests weather_test.cpp)
target_link_libraries(GBWeatherTests PRIVATE GB::GB)
add_test(NAME weather COMMAND GBWeatherTests)

add_executable(GBTerrainTests terrain_test.cpp)
target_link_libraries(GBTerrainTests PRIVATE GB::GB)
add_test(NAME terrain COMMAND GBTerrainTests)
set_tests_properties(terrain PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBGroundTests ground_test.cpp)
target_link_libraries(GBGroundTests PRIVATE GB::GB)
add_test(NAME ground COMMAND GBGroundTests)
set_tests_properties(ground PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBGrassTests grass_test.cpp)
target_link_libraries(GBGrassTests PRIVATE GB::GB)
add_test(NAME grass COMMAND GBGrassTests)
set_tests_properties(grass PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBWaterTests water_test.cpp)
target_link_libraries(GBWaterTests PRIVATE GB::GB)
add_test(NAME water COMMAND GBWaterTests)
set_tests_properties(water PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBSkyTests sky_test.cpp)
target_link_libraries(GBSkyTests PRIVATE GB::GB)
target_compile_definitions(GBSkyTests PRIVATE GB_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME sky COMMAND GBSkyTests)
set_tests_properties(sky PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBPhysicsTests physics_test.cpp)
target_link_libraries(GBPhysicsTests PRIVATE GB::GB)
add_test(NAME physics COMMAND GBPhysicsTests)
