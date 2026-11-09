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
target_link_libraries(GBImageTests PRIVATE GB::Core)
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
target_link_libraries(GBCameraTests PRIVATE GB::Render)
add_test(NAME camera COMMAND GBCameraTests)

add_executable(GBGameCameraTests game_camera_test.cpp)
target_link_libraries(GBGameCameraTests PRIVATE GB::Sim)
target_compile_definitions(GBGameCameraTests PRIVATE GB_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME game_camera COMMAND GBGameCameraTests)

add_executable(GBInputActionsTests input_actions_test.cpp)
target_link_libraries(GBInputActionsTests PRIVATE GB::Sim)
target_compile_definitions(GBInputActionsTests PRIVATE GB_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME input_actions COMMAND GBInputActionsTests)

add_executable(GBParticleTests particles_test.cpp)
target_link_libraries(GBParticleTests PRIVATE GB::Render)
add_test(NAME particles COMMAND GBParticleTests)

add_executable(GBWeatherTests weather_test.cpp)
target_link_libraries(GBWeatherTests PRIVATE GB::Render)
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
target_link_libraries(GBPhysicsTests PRIVATE GB::Sim)
add_test(NAME physics COMMAND GBPhysicsTests)

# The water module (GB/World/Water), headless: WaterField, carver, surfaces without GL, generated shader waves.
add_executable(GBWaterModuleTests water_module_test.cpp)
target_link_libraries(GBWaterModuleTests PRIVATE GB::Sim)
add_test(NAME water_module COMMAND GBWaterModuleTests)

add_executable(GBRiverTests river_test.cpp)
target_link_libraries(GBRiverTests PRIVATE GB::GB)
add_test(NAME river COMMAND GBRiverTests)
set_tests_properties(river PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBWorldTests world_test.cpp)
target_link_libraries(GBWorldTests PRIVATE GB::GB)
add_test(NAME world COMMAND GBWorldTests)

# The world generator without a window (WorldGenerator::GenerateData): determinism, generator versions, the baked biome map and distance to the water.
add_executable(GBWorldGenTests worldgen_test.cpp)
target_link_libraries(GBWorldGenTests PRIVATE GB::Sim)
add_test(NAME worldgen COMMAND GBWorldGenTests)
set_tests_properties(world PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBDestructionTests destruction_test.cpp)
target_link_libraries(GBDestructionTests PRIVATE GB::Sim)
add_test(NAME destruction COMMAND GBDestructionTests)

add_executable(GBPostTests post_test.cpp)
target_link_libraries(GBPostTests PRIVATE GB::GB)
add_test(NAME post COMMAND GBPostTests)
set_tests_properties(post PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBQaTests qa_test.cpp)
target_link_libraries(GBQaTests PRIVATE GB::Render)
add_test(NAME qa COMMAND GBQaTests)

add_executable(GBGoldenTests golden_test.cpp)
target_link_libraries(GBGoldenTests PRIVATE GB::GB)
target_compile_definitions(GBGoldenTests PRIVATE GB_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
add_test(NAME golden_images COMMAND GBGoldenTests)
set_tests_properties(golden_images PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBGoldenDataTests golden_data_test.cpp)
target_link_libraries(GBGoldenDataTests PRIVATE GB::Render)
target_compile_definitions(GBGoldenDataTests PRIVATE GB_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
add_test(NAME golden_data COMMAND GBGoldenDataTests)
set_tests_properties(golden_data PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBSweepTests sweep_test.cpp)
target_link_libraries(GBSweepTests PRIVATE GB::Sim)
add_test(NAME sweep COMMAND GBSweepTests)
set_tests_properties(sweep PROPERTIES SKIP_RETURN_CODE 77)

# The seed sweep of the simulation (H20): tribes on generated worlds, checked at every step against the invariants of GB/Game/SimQA.h. GB_SIM_SWEEP=full for the big one.
add_executable(GBSimSweepTests sim_sweep_test.cpp)
target_link_libraries(GBSimSweepTests PRIVATE GB::Sim)
add_test(NAME sim_sweep COMMAND GBSimSweepTests)

# Scale and budgets (H16): the fair plan budget, the path cache, the abstract agent waiting its turn, several clans sharing one budget of
# thinking turns, a world of several tiles. GB_SCALE_BENCH=1 for the 100 and 300 member runs (asserted milliseconds) and the 2x2 and 3x3 worlds.
add_executable(GBSimScaleTests sim_scale_test.cpp)
target_link_libraries(GBSimScaleTests PRIVATE GB::Sim)
add_test(NAME sim_scale COMMAND GBSimScaleTests)

add_executable(GBEntityTests entity_test.cpp)
target_link_libraries(GBEntityTests PRIVATE GB::Sim)
add_test(NAME entity COMMAND GBEntityTests)

add_executable(GBEntityRenderTests entity_render_test.cpp)
target_link_libraries(GBEntityRenderTests PRIVATE GB::GB)
add_test(NAME entity_render COMMAND GBEntityRenderTests)
set_tests_properties(entity_render PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBAudioTests audio_test.cpp)
target_link_libraries(GBAudioTests PRIVATE GB::Render)
target_compile_definitions(GBAudioTests PRIVATE GB_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
add_test(NAME audio COMMAND GBAudioTests)

add_executable(GBAudioDeviceTests audio_device_test.cpp)
target_link_libraries(GBAudioDeviceTests PRIVATE GB::Sim)
add_test(NAME audio_device COMMAND GBAudioDeviceTests)
set_tests_properties(audio_device PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBFbxTests fbx_test.cpp)
target_link_libraries(GBFbxTests PRIVATE GB::Sim)
target_compile_definitions(GBFbxTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME fbx COMMAND GBFbxTests)

add_executable(GBCharacterTests character_test.cpp)
target_link_libraries(GBCharacterTests PRIVATE GB::Sim)
add_test(NAME character COMMAND GBCharacterTests)

add_executable(GBVegetationTests vegetation_test.cpp)
target_link_libraries(GBVegetationTests PRIVATE GB::GB)
add_test(NAME vegetation COMMAND GBVegetationTests)
set_tests_properties(vegetation PROPERTIES SKIP_RETURN_CODE 77)

add_executable(GBLocomotionTests locomotion_test.cpp)
target_link_libraries(GBLocomotionTests PRIVATE GB::Sim)
target_compile_definitions(GBLocomotionTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME locomotion COMMAND GBLocomotionTests)

add_executable(GBTurnLayerTests turn_layer_test.cpp)
target_link_libraries(GBTurnLayerTests PRIVATE GB::Sim)
target_compile_definitions(GBTurnLayerTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME turn_layer COMMAND GBTurnLayerTests)

add_executable(GBCharacterLayerTests character_layer_test.cpp)
target_link_libraries(GBCharacterLayerTests PRIVATE GB::Sim)
target_compile_definitions(GBCharacterLayerTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME character_layer COMMAND GBCharacterLayerTests)

add_executable(GBAnimInstanceTests anim_instance_test.cpp)
target_link_libraries(GBAnimInstanceTests PRIVATE GB::Sim)
target_compile_definitions(GBAnimInstanceTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME anim_instance COMMAND GBAnimInstanceTests)

add_executable(GBMontageTests montage_test.cpp)
target_link_libraries(GBMontageTests PRIVATE GB::Sim)
target_compile_definitions(GBMontageTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME montage COMMAND GBMontageTests)

add_executable(GBPropsTests props_test.cpp)
target_link_libraries(GBPropsTests PRIVATE GB::Sim)
target_compile_definitions(GBPropsTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME props COMMAND GBPropsTests)

add_executable(GBProceduralTests procedural_test.cpp)
target_link_libraries(GBProceduralTests PRIVATE GB::Sim)
target_compile_definitions(GBProceduralTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME procedural COMMAND GBProceduralTests)

add_executable(GBInteractionTests interaction_test.cpp)
target_link_libraries(GBInteractionTests PRIVATE GB::Sim)
target_compile_definitions(GBInteractionTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME interaction COMMAND GBInteractionTests)

add_executable(GBCharacterProfileTests character_profile_test.cpp)
target_link_libraries(GBCharacterProfileTests PRIVATE GB::Sim)
target_include_directories(GBCharacterProfileTests SYSTEM PRIVATE ${json_SOURCE_DIR}/include)
target_compile_definitions(GBCharacterProfileTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME character_profile COMMAND GBCharacterProfileTests)

add_executable(GBActorTests actor_test.cpp)
target_link_libraries(GBActorTests PRIVATE GB::Render)
target_compile_definitions(GBActorTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME actor COMMAND GBActorTests)

add_executable(GBBoxClearanceTests box_clearance_test.cpp)
target_link_libraries(GBBoxClearanceTests PRIVATE GB::Sim)
add_test(NAME box_clearance COMMAND GBBoxClearanceTests)

add_executable(GBPhysicsJointTests physics_joint_test.cpp)
target_link_libraries(GBPhysicsJointTests PRIVATE GB::Sim)
add_test(NAME physics_joint COMMAND GBPhysicsJointTests)

add_executable(GBRagdollTests ragdoll_test.cpp)
target_link_libraries(GBRagdollTests PRIVATE GB::Sim)
target_compile_definitions(GBRagdollTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME ragdoll COMMAND GBRagdollTests)

add_executable(GBGameplayTests gameplay_test.cpp)
target_link_libraries(GBGameplayTests PRIVATE GB::Sim)
target_compile_definitions(GBGameplayTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME gameplay COMMAND GBGameplayTests)

add_executable(GBRetargetTests retarget_test.cpp)
target_link_libraries(GBRetargetTests PRIVATE GB::Sim)
target_compile_definitions(GBRetargetTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME retarget COMMAND GBRetargetTests)

add_executable(GBAnimGraphTests anim_graph_test.cpp)
target_link_libraries(GBAnimGraphTests PRIVATE GB::Sim)
target_compile_definitions(GBAnimGraphTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME anim_graph COMMAND GBAnimGraphTests)

add_executable(GBAnimManifestTests anim_manifest_test.cpp)
target_link_libraries(GBAnimManifestTests PRIVATE GB::Sim)
target_compile_definitions(GBAnimManifestTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data" GB_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME anim_manifest COMMAND GBAnimManifestTests)

add_executable(GBSaveTests save_test.cpp)
target_link_libraries(GBSaveTests PRIVATE GB::Render)
target_compile_definitions(GBSaveTests PRIVATE GB_SAVES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden/saves")
add_test(NAME save COMMAND GBSaveTests)

add_executable(GBItemsTests items_test.cpp)
target_link_libraries(GBItemsTests PRIVATE GB::Sim)
target_compile_definitions(GBItemsTests PRIVATE GB_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME items COMMAND GBItemsTests)

add_executable(GBPacksTests packs_test.cpp)
target_link_libraries(GBPacksTests PRIVATE GB::Sim)
target_compile_definitions(GBPacksTests PRIVATE GB_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME packs COMMAND GBPacksTests)

add_executable(GBResourcesTests resources_test.cpp)
target_link_libraries(GBResourcesTests PRIVATE GB::Sim)
add_test(NAME resources COMMAND GBResourcesTests)

add_executable(GBWorldResourcesTests world_resources_test.cpp)
target_link_libraries(GBWorldResourcesTests PRIVATE GB::Sim)
add_test(NAME world_resources COMMAND GBWorldResourcesTests)

# Forests, stony ground, flowers and wild food over a generated world (WorldResources patches, bosques.json), and the soft grass between zones
add_executable(GBForestTests forest_test.cpp)
target_link_libraries(GBForestTests PRIVATE GB::Sim)
target_compile_definitions(GBForestTests PRIVATE GB_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME forest COMMAND GBForestTests)

add_executable(GBTribeTests tribe_test.cpp)
target_link_libraries(GBTribeTests PRIVATE GB::Sim sqlite3)
target_compile_definitions(GBTribeTests PRIVATE GB_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME tribe COMMAND GBTribeTests)

add_executable(GBSettlementTests settlement_test.cpp)
target_link_libraries(GBSettlementTests PRIVATE GB::Sim)
target_compile_definitions(GBSettlementTests PRIVATE GB_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME settlement COMMAND GBSettlementTests)

# The settlement planner alone (needs and sites from plain facts, no Tribe)
add_executable(GBSettlementPlannerTests settlement_planner_test.cpp)
target_link_libraries(GBSettlementPlannerTests PRIVATE GB::Sim)
add_test(NAME settlement_planner COMMAND GBSettlementPlannerTests)

# The timetable alone (parts of the day, shifts, agenda factors)
add_executable(GBScheduleTests schedule_test.cpp)
target_link_libraries(GBScheduleTests PRIVATE GB::Sim)
add_test(NAME schedule COMMAND GBScheduleTests)

# The claims on nodes alone (first piece of the logistics)
add_executable(GBLogisticsTests logistics_test.cpp)
target_link_libraries(GBLogisticsTests PRIVATE GB::Sim)
add_test(NAME logistics COMMAND GBLogisticsTests)

# H7: aptitudes by practice, the job board and the trades of the clan (TribeWork, Skills, JobBoard)
add_executable(GBWorkTests work_test.cpp)
target_link_libraries(GBWorkTests PRIVATE GB::Sim)
target_compile_definitions(GBWorkTests PRIVATE GB_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME work COMMAND GBWorkTests)

# H18: production chains, spoilage and preservation, fields, stockpiles and hauling (Production, Spoilage, Farming, Economy, TribeEconomy)
add_executable(GBEconomyTests economy_test.cpp)
target_link_libraries(GBEconomyTests PRIVATE GB::Sim)
target_compile_definitions(GBEconomyTests PRIVATE GB_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME economy COMMAND GBEconomyTests)

# H14: conflict, combat and violence (Combat, Conflict, TribeConflict)
add_executable(GBCombatTests combat_test.cpp)
target_link_libraries(GBCombatTests PRIVATE GB::Sim)
target_include_directories(GBCombatTests SYSTEM PRIVATE ${json_SOURCE_DIR}/include)
target_compile_definitions(GBCombatTests PRIVATE GB_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME combat COMMAND GBCombatTests)

# Social life: conversations, rites, beliefs, diet, more game and traps (Social, TribeSocial, ClansBeliefs; docs/VIDA_SOCIAL.md)
add_executable(GBSocialTests social_test.cpp)
target_link_libraries(GBSocialTests PRIVATE GB::Sim)
target_compile_definitions(GBSocialTests PRIVATE GB_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME social COMMAND GBSocialTests)

add_executable(GBKnowledgeTests knowledge_test.cpp)
target_link_libraries(GBKnowledgeTests PRIVATE GB::Sim)
target_compile_definitions(GBKnowledgeTests PRIVATE GB_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME knowledge COMMAND GBKnowledgeTests)

# H9 + H10: demography, inheritance, relations, personality and culture (Society, TribeSociety)
add_executable(GBSocietyTests society_test.cpp)
target_link_libraries(GBSocietyTests PRIVATE GB::Sim)
target_compile_definitions(GBSocietyTests PRIVATE GB_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME society COMMAND GBSocietyTests)

add_executable(GBPerceptionTests perception_test.cpp)
target_link_libraries(GBPerceptionTests PRIVATE GB::Sim)
add_test(NAME perception COMMAND GBPerceptionTests)

add_executable(GBAiTests ai_test.cpp)
target_link_libraries(GBAiTests PRIVATE GB::Sim)
add_test(NAME ai COMMAND GBAiTests)

add_executable(GBInteractivesTests interactives_test.cpp)
target_link_libraries(GBInteractivesTests PRIVATE GB::Sim)
target_compile_definitions(GBInteractivesTests PRIVATE GB_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME interactives COMMAND GBInteractivesTests)

add_executable(GBEcologyTests ecology_test.cpp)
target_link_libraries(GBEcologyTests PRIVATE GB::Sim)
target_compile_definitions(GBEcologyTests PRIVATE GB_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME ecology COMMAND GBEcologyTests)

add_executable(GBScaleTests scale_test.cpp)
target_link_libraries(GBScaleTests PRIVATE GB::Sim)
add_test(NAME scale COMMAND GBScaleTests)

add_executable(GBNavigationTests navigation_test.cpp)
target_link_libraries(GBNavigationTests PRIVATE GB::Sim)
add_test(NAME navigation COMMAND GBNavigationTests)

# H12: regions of the generated world, the clan's map, expeditions and migration (Regions, Exploration, TribeExplore)
add_executable(GBExplorationTests exploration_test.cpp)
target_link_libraries(GBExplorationTests PRIVATE GB::Sim)
target_include_directories(GBExplorationTests SYSTEM PRIVATE ${json_SOURCE_DIR}/include)
target_compile_definitions(GBExplorationTests PRIVATE GB_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME exploration COMMAND GBExplorationTests)

# H13: several clans in one world, territory, relations between clans, trade, knowledge exchange, marriages and pacts (Clans, Diplomacy, Territory)
add_executable(GBClansTests clans_test.cpp)
target_link_libraries(GBClansTests PRIVATE GB::Sim)
target_include_directories(GBClansTests SYSTEM PRIVATE ${json_SOURCE_DIR}/include)
target_compile_definitions(GBClansTests PRIVATE GB_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME clans COMMAND GBClansTests)

add_executable(GBAnimUsageTests anim_usage_test.cpp)
target_link_libraries(GBAnimUsageTests PRIVATE GB::Sim)
target_compile_definitions(GBAnimUsageTests PRIVATE GB_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME anim_usage COMMAND GBAnimUsageTests)

# Animation bindings (docs/EDITOR_ANIMACIONES.md): JSON, validation, resolution with fallbacks, trim / loop / blend on an animation clock, headless
add_executable(GBAnimBindingsTests anim_bindings_test.cpp)
target_link_libraries(GBAnimBindingsTests PRIVATE GB::Sim)
target_compile_definitions(GBAnimBindingsTests PRIVATE GB_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets" GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME anim_bindings COMMAND GBAnimBindingsTests)

# The visual layer as data (GB/Visual): procedural meshes, textures, node looks by Amount, props by bag and job, buildings by phase
add_executable(GBVisualsTests visuals_test.cpp)
target_link_libraries(GBVisualsTests PRIVATE GB::Render)
target_compile_definitions(GBVisualsTests PRIVATE GB_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME visuals COMMAND GBVisualsTests)

# H15: the player inside the society (PlayerMember, TribePlayer, ClansPlayer)
add_executable(GBPlayerTests player_test.cpp)
target_link_libraries(GBPlayerTests PRIVATE GB::Sim)
target_compile_definitions(GBPlayerTests PRIVATE GB_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME player COMMAND GBPlayerTests)

# The priority graph: editable decision priorities per role or character, JSON library, validation, subgraphs, and the tribe unchanged with the builtin graph
add_executable(GBPriorityGraphTests priority_graph_test.cpp)
target_link_libraries(GBPriorityGraphTests PRIVATE GB::Sim)
target_compile_definitions(GBPriorityGraphTests PRIVATE GB_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME priority_graph COMMAND GBPriorityGraphTests)
add_executable(GBAnimBindingsWorldTests anim_bindings_world_test.cpp)
target_link_libraries(GBAnimBindingsWorldTests PRIVATE GB::Sim)
target_compile_definitions(GBAnimBindingsWorldTests PRIVATE GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
add_test(NAME anim_bindings_world COMMAND GBAnimBindingsWorldTests)

add_executable(GBObjectTests gbobject_test.cpp)
target_link_libraries(GBObjectTests PRIVATE GB::Sim)
add_test(NAME gbobject COMMAND GBObjectTests)

# The terrain graph (docs/GRAFO_TERRENO.md): nodes/edges, validation (cycles, unknown types), compiling,
# evaluating noise/erosion/biome nodes, determinism, and applying a result to both terrain targets.
add_executable(GBTerrainGraphTests terrain_graph_test.cpp)
target_link_libraries(GBTerrainGraphTests PRIVATE GB::Render)
add_test(NAME terrain_graph COMMAND GBTerrainGraphTests)

# V1 (docs/VISION_MUNDO_VIVO.md): the world's history and each person's memory of it
add_executable(GBHistoryTests history_test.cpp)
target_link_libraries(GBHistoryTests PRIVATE GB::Sim)
add_test(NAME history COMMAND GBHistoryTests)
# V5: eras as packs (needs and eras that a later pack adds, what a building provides)
add_executable(GBEraPackTests era_pack_test.cpp)
target_link_libraries(GBEraPackTests PRIVATE GB::Sim)
add_test(NAME era_pack COMMAND GBEraPackTests)

# T0: the master seed of a game (GB/Core/Seed.h, GB/Game/GameSeeds.h): pinned derivation, and the same master mounts the same game.
add_executable(GBSeedTests seed_test.cpp)
target_link_libraries(GBSeedTests PRIVATE GB::Render)
add_test(NAME seed COMMAND GBSeedTests)

# Build speed (T50). Every test is one .cpp file, so instead of one precompiled header each (that would cost as much as it saves) they all
# reuse the one of GBTestPch: the standard library and glm, the same lists as the engine. GBTestPch only needs GB::Core (the glm include
# directory), so it builds while GB_Sim does, and it has no compile definitions: the tests may add theirs (GB_ASSETS_DIR...) on top.
get_property(gb_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/test_pch.cpp CONTENT "// The precompiled header of the tests (tests/CMakeLists.txt)\n")
add_library(GBTestPch OBJECT ${CMAKE_CURRENT_BINARY_DIR}/test_pch.cpp)
target_link_libraries(GBTestPch PRIVATE GB::Core)
target_precompile_headers(GBTestPch PRIVATE ${GB_PCH_STD} ${GB_PCH_GLM})
foreach(gb_test IN LISTS gb_test_targets)
    target_precompile_headers(${gb_test} REUSE_FROM GBTestPch)
endforeach()
gb_parallel_compile(GBTestPch ${gb_test_targets})   # /MP (MSVC only)

# T40: the reference scene of the render plan (PLAN_RENDER E1): a village at 7 h, 12 h, 18:30 and 22 h, in rain and in snow, compared with
# tests/golden/images/reference. GB_REFERENCE_BENCH=1 GBReferenceSceneTests measures its cost per pass at 1080p instead (not in ctest).
add_executable(GBReferenceSceneTests reference_scene_test.cpp)
target_link_libraries(GBReferenceSceneTests PRIVATE GB::GB)
target_compile_definitions(GBReferenceSceneTests PRIVATE GB_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden" GB_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
                           GB_PACKS_DIR="${CMAKE_SOURCE_DIR}/packs/full")
target_precompile_headers(GBReferenceSceneTests REUSE_FROM GBTestPch)
gb_parallel_compile(GBReferenceSceneTests)
add_test(NAME reference_scene COMMAND GBReferenceSceneTests)
set_tests_properties(reference_scene PROPERTIES SKIP_RETURN_CODE 77 LABELS "imagen;gl")
