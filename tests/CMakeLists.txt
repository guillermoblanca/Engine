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

add_executable(GBRiverTests river_test.cpp)
target_link_libraries(GBRiverTests PRIVATE GB::GB)
add_test(NAME river COMMAND GBRiverTests)
set_tests_properties(river PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBWorldTests world_test.cpp)
target_link_libraries(GBWorldTests PRIVATE GB::GB)
add_test(NAME world COMMAND GBWorldTests)
set_tests_properties(world PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBDestructionTests destruction_test.cpp)
target_link_libraries(GBDestructionTests PRIVATE GB::GB)
add_test(NAME destruction COMMAND GBDestructionTests)

add_executable(GBPostTests post_test.cpp)
target_link_libraries(GBPostTests PRIVATE GB::GB)
add_test(NAME post COMMAND GBPostTests)
set_tests_properties(post PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBQaTests qa_test.cpp)
target_link_libraries(GBQaTests PRIVATE GB::GB)
add_test(NAME qa COMMAND GBQaTests)

add_executable(GBGoldenTests golden_test.cpp)
target_link_libraries(GBGoldenTests PRIVATE GB::GB)
target_compile_definitions(GBGoldenTests PRIVATE GB_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
add_test(NAME golden_images COMMAND GBGoldenTests)
set_tests_properties(golden_images PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBGoldenDataTests golden_data_test.cpp)
target_link_libraries(GBGoldenDataTests PRIVATE GB::GB)
target_compile_definitions(GBGoldenDataTests PRIVATE GB_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
add_test(NAME golden_data COMMAND GBGoldenDataTests)
set_tests_properties(golden_data PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBSweepTests sweep_test.cpp)
target_link_libraries(GBSweepTests PRIVATE GB::GB)
add_test(NAME sweep COMMAND GBSweepTests)
set_tests_properties(sweep PROPERTIES SKIP_RETURN_CODE 77)
