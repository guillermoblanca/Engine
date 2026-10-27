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

add_executable(GBEntityTests entity_test.cpp)
target_link_libraries(GBEntityTests PRIVATE GB::GB)
add_test(NAME entity COMMAND GBEntityTests)

add_executable(GBEntityRenderTests entity_render_test.cpp)
target_link_libraries(GBEntityRenderTests PRIVATE GB::GB)
add_test(NAME entity_render COMMAND GBEntityRenderTests)
set_tests_properties(entity_render PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBAudioTests audio_test.cpp)
target_link_libraries(GBAudioTests PRIVATE GB::GB)
target_compile_definitions(GBAudioTests PRIVATE GB_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
add_test(NAME audio COMMAND GBAudioTests)

add_executable(GBAudioDeviceTests audio_device_test.cpp)
target_link_libraries(GBAudioDeviceTests PRIVATE GB::GB)
add_test(NAME audio_device COMMAND GBAudioDeviceTests)
set_tests_properties(audio_device PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBFbxTests fbx_test.cpp)
target_link_libraries(GBFbxTests PRIVATE GB::GB)
target_compile_definitions(GBFbxTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME fbx COMMAND GBFbxTests)

add_executable(GBCharacterTests character_test.cpp)
target_link_libraries(GBCharacterTests PRIVATE GB::GB)
add_test(NAME character COMMAND GBCharacterTests)

add_executable(GBVegetationTests vegetation_test.cpp)
target_link_libraries(GBVegetationTests PRIVATE GB::GB)
add_test(NAME vegetation COMMAND GBVegetationTests)
set_tests_properties(vegetation PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBLocomotionTests locomotion_test.cpp)
target_link_libraries(GBLocomotionTests PRIVATE GB::GB)
target_compile_definitions(GBLocomotionTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME locomotion COMMAND GBLocomotionTests)

add_executable(GBTurnLayerTests turn_layer_test.cpp)
target_link_libraries(GBTurnLayerTests PRIVATE GB::GB)
target_compile_definitions(GBTurnLayerTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME turn_layer COMMAND GBTurnLayerTests)

add_executable(GBCharacterLayerTests character_layer_test.cpp)
target_link_libraries(GBCharacterLayerTests PRIVATE GB::GB)
target_compile_definitions(GBCharacterLayerTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME character_layer COMMAND GBCharacterLayerTests)

add_executable(GBAnimInstanceTests anim_instance_test.cpp)
target_link_libraries(GBAnimInstanceTests PRIVATE GB::GB)
target_compile_definitions(GBAnimInstanceTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME anim_instance COMMAND GBAnimInstanceTests)

add_executable(GBMontageTests montage_test.cpp)
target_link_libraries(GBMontageTests PRIVATE GB::GB)
target_compile_definitions(GBMontageTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME montage COMMAND GBMontageTests)

add_executable(GBPropsTests props_test.cpp)
target_link_libraries(GBPropsTests PRIVATE GB::GB)
target_compile_definitions(GBPropsTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME props COMMAND GBPropsTests)

add_executable(GBProceduralTests procedural_test.cpp)
target_link_libraries(GBProceduralTests PRIVATE GB::GB)
target_compile_definitions(GBProceduralTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME procedural COMMAND GBProceduralTests)

add_executable(GBInteractionTests interaction_test.cpp)
target_link_libraries(GBInteractionTests PRIVATE GB::GB)
target_compile_definitions(GBInteractionTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME interaction COMMAND GBInteractionTests)

add_executable(GBCharacterProfileTests character_profile_test.cpp)
target_link_libraries(GBCharacterProfileTests PRIVATE GB::GB)
target_include_directories(GBCharacterProfileTests SYSTEM PRIVATE ${json_SOURCE_DIR}/include)
target_compile_definitions(GBCharacterProfileTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME character_profile COMMAND GBCharacterProfileTests)

add_executable(GBActorTests actor_test.cpp)
target_link_libraries(GBActorTests PRIVATE GB::GB)
target_compile_definitions(GBActorTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME actor COMMAND GBActorTests)

add_executable(GBBoxClearanceTests box_clearance_test.cpp)
target_link_libraries(GBBoxClearanceTests PRIVATE GB::GB)
add_test(NAME box_clearance COMMAND GBBoxClearanceTests)

add_executable(GBPhysicsJointTests physics_joint_test.cpp)
target_link_libraries(GBPhysicsJointTests PRIVATE GB::GB)
add_test(NAME physics_joint COMMAND GBPhysicsJointTests)

add_executable(GBRagdollTests ragdoll_test.cpp)
target_link_libraries(GBRagdollTests PRIVATE GB::GB)
target_compile_definitions(GBRagdollTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME ragdoll COMMAND GBRagdollTests)

add_executable(GBGameplayTests gameplay_test.cpp)
target_link_libraries(GBGameplayTests PRIVATE GB::GB)
target_compile_definitions(GBGameplayTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME gameplay COMMAND GBGameplayTests)

add_executable(GBRetargetTests retarget_test.cpp)
target_link_libraries(GBRetargetTests PRIVATE GB::GB)
target_compile_definitions(GBRetargetTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME retarget COMMAND GBRetargetTests)

add_executable(GBAnimGraphTests anim_graph_test.cpp)
target_link_libraries(GBAnimGraphTests PRIVATE GB::GB)
target_compile_definitions(GBAnimGraphTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME anim_graph COMMAND GBAnimGraphTests)

add_executable(GBAnimManifestTests anim_manifest_test.cpp)
target_link_libraries(GBAnimManifestTests PRIVATE GB::GB)
target_compile_definitions(GBAnimManifestTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data" GB_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME anim_manifest COMMAND GBAnimManifestTests)

add_executable(GBSaveTests save_test.cpp)
target_link_libraries(GBSaveTests PRIVATE GB::GB)
target_compile_definitions(GBSaveTests PRIVATE GB_SAVES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden/saves")
add_test(NAME save COMMAND GBSaveTests)

add_executable(GBItemsTests items_test.cpp)
target_link_libraries(GBItemsTests PRIVATE GB::GB)
target_compile_definitions(GBItemsTests PRIVATE GB_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME items COMMAND GBItemsTests)

add_executable(GBResourcesTests resources_test.cpp)
target_link_libraries(GBResourcesTests PRIVATE GB::GB)
add_test(NAME resources COMMAND GBResourcesTests)

add_executable(GBTribeTests tribe_test.cpp)
target_link_libraries(GBTribeTests PRIVATE GB::GB)
add_test(NAME tribe COMMAND GBTribeTests)

add_executable(GBInteractivesTests interactives_test.cpp)
target_link_libraries(GBInteractivesTests PRIVATE GB::GB)
add_test(NAME interactives COMMAND GBInteractivesTests)

add_executable(GBEcologyTests ecology_test.cpp)
target_link_libraries(GBEcologyTests PRIVATE GB::GB)
add_test(NAME ecology COMMAND GBEcologyTests)

add_executable(GBScaleTests scale_test.cpp)
target_link_libraries(GBScaleTests PRIVATE GB::GB)
add_test(NAME scale COMMAND GBScaleTests)
