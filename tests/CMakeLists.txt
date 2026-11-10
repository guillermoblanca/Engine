# The tests of the engine. Every test has LABELS, so a run can pick the kind of test it needs (T51):
#
#   rapido   under about ten seconds and no OpenGL: the pass for every change
#   lento    over about ten seconds (long simulations, the crowd of ai, the editor runs)
#   imagen   checks pixels: golden images and data, read back renders, image files. A change that does not touch the image skips them
#   gl       needs an OpenGL context (a hidden window); with no GPU they skip (exit code 77, or the editor's skip pattern)
#   sim      the simulation of the clans (Tribe, Clans, Society, the economy, the AI...)
#   editor   the scripted runs of the real editor (editor_*, Editor/CMakeLists.txt). The logic test `editor` (GBEditorTests, no window)
#            is not one of them: it is `rapido`, and the regex -E editor would drop it too, so pick by label (-LE editor), not by name
#
# A test can have several. Typical commands, from the build directory (or with --test-dir build), always with -C Release:
#   ctest -C Release -L rapido -j 8                     the fast pass (under two minutes)
#   ctest -C Release -L rapido -LE imagen -j 8          the fast pass for a change that does not touch the image
#   ctest -C Release -LE "imagen|editor" -j 8          everything but the images and the editor (a change that does not touch the image)
#   ctest -C Release -LE editor -j 8                    everything but the editor runs (bounded by ai_crowd_lod, a few minutes)
#   ctest -C Release -L sim -j 8                        the clan simulation
#   ctest -C Release -L imagen                          the image tests alone (after a change to the render)
#   ctest -C Release -L editor -j 4                     the editor runs, four at a time (Editor/CMakeLists.txt: no shared files)
#   ctest -C Release -R "^ai$" --output-on-failure      one test by name
#
# A new test is one gb_test() line:
#
#   gb_test(<name> <source> EXE <executable> | POOL <pool>  LINK <targets...>  LABELS <labels...>
#           [DEFS <definitions...>] [JSON] [SKIP77] [ARGS <arguments...>])
#
#   name     the ctest name
#   EXE      the test is an executable of its own and the source has its own int main() (the golden tests and image)
#   POOL     the test goes into a shared executable, one per layer of GB: GBSimTests (LINK GB::Sim), GBRenderTests (LINK GB::Render) and
#            GBGLTests (LINK GB::GB, OpenGL). Its source has no main(): its entry is int GBTest_<file>(int argc, char** argv), <file> being the
#            file name without .cpp and without _test (ai_test.cpp: GBTest_ai), and its file-local classes go in an anonymous namespace
#            (two FakeBody in one executable would break the one-definition rule without a word from the linker). ctest runs
#            `<pool> <file> [ARGS]`, one process per test as before; by hand, GBSimTests with no argument lists its tests
#   LINK     the lowest layer of GB it needs (GB::Core, GB::Sim, GB::Render; GB::GB for OpenGL), and any extra library (sqlite3)
#   LABELS   from the list above; every test has rapido, lento or gl
#   DEFS     its compile definitions (only its own source sees them), JSON if it includes nlohmann/json itself, SKIP77 if it can skip
#   ARGS     arguments after the test's name: several tests of one source (ai, ai_crowd_full, ai_crowd_lod)
#
# Why pools (T51): every executable links its whole layer of GB, so touching one .cpp of GB relinked ~90 executables; now it relinks five.
# A new golden test, for example, is a test of its own with OpenGL that compares images:
#   gb_test(golden_scene golden_scene_test.cpp EXE GBGoldenSceneTests LINK GB::GB LABELS "imagen;gl" SKIP77
#           DEFS GB_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
function(gb_test name source)
    cmake_parse_arguments(T "JSON;SKIP77" "EXE;POOL" "LINK;LABELS;DEFS;ARGS" ${ARGN})
    if((NOT T_EXE AND NOT T_POOL) OR (T_EXE AND T_POOL) OR NOT T_LINK OR NOT T_LABELS)
        message(FATAL_ERROR "gb_test(${name}): EXE or POOL, LINK and LABELS are required")
    endif()
    if(T_EXE)
        set(exe ${T_EXE})
        set(command ${exe} ${T_ARGS})
    else()
        set(exe ${T_POOL})
        get_filename_component(entry ${source} NAME_WE)
        string(REGEX REPLACE "_test$" "" entry ${entry})
        set(command ${exe} ${entry} ${T_ARGS})
    endif()
    if(NOT TARGET ${exe})
        add_executable(${exe})
        if(T_POOL)
            set_property(GLOBAL APPEND PROPERTY GB_TEST_POOLS ${exe})
        endif()
    endif()
    get_target_property(sources ${exe} SOURCES)
    if(NOT source IN_LIST sources)   # (several tests of one source add it once)
        target_sources(${exe} PRIVATE ${source})
        target_link_libraries(${exe} PRIVATE ${T_LINK})
        if(T_JSON)
            target_include_directories(${exe} SYSTEM PRIVATE ${json_SOURCE_DIR}/include)
        endif()
        if(T_DEFS)
            set_source_files_properties(${source} PROPERTIES COMPILE_DEFINITIONS "${T_DEFS}")
        endif()
        if(T_POOL)
            set_property(TARGET ${exe} APPEND PROPERTY GB_TEST_ENTRIES ${entry})
        endif()
    endif()
    add_test(NAME ${name} COMMAND ${command})
    set_tests_properties(${name} PROPERTIES LABELS "${T_LABELS}")
    if(T_SKIP77)
        set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endfunction()

# The main() of every pool: the first argument picks the entry, the rest are its arguments (argv[0] becomes the entry's name).
function(gb_test_write_pool_mains)
    get_property(pools GLOBAL PROPERTY GB_TEST_POOLS)
    foreach(pool IN LISTS pools)
        get_target_property(entries ${pool} GB_TEST_ENTRIES)
        set(declarations "")
        set(table "")
        foreach(entry IN LISTS entries)
            string(APPEND declarations "int GBTest_${entry}(int argc, char** argv);\n")
            string(APPEND table "\t{ \"${entry}\", &GBTest_${entry} },\n")
        endforeach()
        file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${pool}_main.cpp @ONLY CONTENT
"// The main() of the test pool ${pool}, written by tests/CMakeLists.txt (gb_test): `${pool} <test> [arguments]` runs one test.
#include <cstdio>
#include <cstring>

${declarations}
namespace
{
	struct Entry { const char* Name; int (*Run)(int, char**); };
	const Entry kEntries[] = {
${table}	};
}

int main(int argc, char** argv)
{
	if (argc >= 2)
		for (const Entry& e : kEntries)
			if (std::strcmp(argv[1], e.Name) == 0) return e.Run(argc - 1, argv + 1);
	std::fprintf(stderr, \"usage: ${pool} <test> [arguments]; tests:\");
	for (const Entry& e : kEntries) std::fprintf(stderr, \" %s\", e.Name);
	std::fprintf(stderr, \"\\n\");
	return 2;
}
")
        target_sources(${pool} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${pool}_main.cpp)
    endforeach()
endfunction()

set(gb_data GB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
set(gb_asset GB_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")      # (two spellings in the tests: both point at assets/)
set(gb_assets GB_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
set(gb_golden GB_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

gb_test(smoke smoke.cpp POOL GBGLTests LINK GB::GB LABELS rapido)
gb_test(gl_render gl_render.cpp POOL GBGLTests LINK GB::GB LABELS "imagen;gl" SKIP77)
gb_test(gl_scene gl_scene.cpp POOL GBGLTests LINK GB::GB LABELS "imagen;gl" SKIP77)
gb_test(image image_test.cpp EXE GBImageTests LINK GB::Core LABELS "rapido;imagen" DEFS ${gb_asset})
gb_test(gltf gltf_test.cpp POOL GBGLTests LINK GB::GB LABELS gl DEFS ${gb_data} SKIP77)
gb_test(animation animation_test.cpp POOL GBGLTests LINK GB::GB LABELS "imagen;gl" DEFS ${gb_data} SKIP77)
gb_test(camera camera_test.cpp POOL GBRenderTests LINK GB::Render LABELS rapido)
gb_test(game_camera game_camera_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido DEFS ${gb_assets})
gb_test(input_actions input_actions_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido DEFS ${gb_assets})
gb_test(particles particles_test.cpp POOL GBRenderTests LINK GB::Render LABELS rapido)
gb_test(weather weather_test.cpp POOL GBRenderTests LINK GB::Render LABELS rapido)
gb_test(terrain terrain_test.cpp POOL GBGLTests LINK GB::GB LABELS "imagen;gl" SKIP77)
gb_test(ground ground_test.cpp POOL GBGLTests LINK GB::GB LABELS "imagen;gl" SKIP77)
gb_test(grass grass_test.cpp POOL GBGLTests LINK GB::GB LABELS "imagen;gl" SKIP77)
gb_test(water water_test.cpp POOL GBGLTests LINK GB::GB LABELS "imagen;gl" SKIP77)
gb_test(sky sky_test.cpp POOL GBGLTests LINK GB::GB LABELS "imagen;gl" DEFS ${gb_asset} SKIP77)
gb_test(physics physics_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido)
# The water module (GB/World/Water), headless: WaterField, carver, surfaces without GL, generated shader waves.
gb_test(water_module water_module_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido)
gb_test(river river_test.cpp POOL GBGLTests LINK GB::GB LABELS "imagen;gl" SKIP77)
gb_test(world world_test.cpp POOL GBGLTests LINK GB::GB LABELS gl SKIP77)
# The world generator without a window (WorldGenerator::GenerateData): determinism, generator versions, the baked biome map and distance to the water.
gb_test(worldgen worldgen_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido)
gb_test(destruction destruction_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido)
gb_test(post post_test.cpp POOL GBGLTests LINK GB::GB LABELS "imagen;gl" SKIP77)
gb_test(qa qa_test.cpp POOL GBRenderTests LINK GB::Render LABELS "rapido;imagen")
gb_test(golden_images golden_test.cpp EXE GBGoldenTests LINK GB::GB LABELS "imagen;gl" DEFS ${gb_golden} SKIP77)
gb_test(golden_data golden_data_test.cpp EXE GBGoldenDataTests LINK GB::Render LABELS "rapido;imagen" DEFS ${gb_golden} SKIP77)
gb_test(sweep sweep_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido SKIP77)
# The seed sweep of the simulation (H20): tribes on generated worlds, checked at every step against the invariants of GB/Game/SimQA.h. GB_SIM_SWEEP=full for the big one.
gb_test(sim_sweep sim_sweep_test.cpp POOL GBSimTests LINK GB::Sim LABELS "rapido;sim")
# Scale and budgets (H16): the fair plan budget, the path cache, the abstract agent waiting its turn, several clans sharing one budget of
# thinking turns, a world of several tiles. GB_SCALE_BENCH=1 for the 100 and 300 member runs (asserted milliseconds) and the 2x2 and 3x3 worlds.
gb_test(sim_scale sim_scale_test.cpp POOL GBSimTests LINK GB::Sim LABELS "rapido;sim")
gb_test(entity entity_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido)
gb_test(entity_render entity_render_test.cpp POOL GBGLTests LINK GB::GB LABELS gl SKIP77)
gb_test(audio audio_test.cpp POOL GBRenderTests LINK GB::Render LABELS rapido DEFS ${gb_golden})
gb_test(audio_device audio_device_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido SKIP77)
gb_test(fbx fbx_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido DEFS ${gb_data})
gb_test(character character_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido)
gb_test(vegetation vegetation_test.cpp POOL GBGLTests LINK GB::GB LABELS gl SKIP77)
gb_test(locomotion locomotion_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido DEFS ${gb_data})
gb_test(turn_layer turn_layer_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido DEFS ${gb_data})
gb_test(character_layer character_layer_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido DEFS ${gb_data})
gb_test(anim_instance anim_instance_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido DEFS ${gb_data})
gb_test(montage montage_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido DEFS ${gb_data})
gb_test(props props_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido DEFS ${gb_data})
gb_test(procedural procedural_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido DEFS ${gb_data})
gb_test(interaction interaction_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido DEFS ${gb_data})
gb_test(character_profile character_profile_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido DEFS ${gb_data} JSON)
gb_test(actor actor_test.cpp POOL GBRenderTests LINK GB::Render LABELS rapido DEFS ${gb_data})
gb_test(box_clearance box_clearance_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido)
gb_test(physics_joint physics_joint_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido)
gb_test(ragdoll ragdoll_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido DEFS ${gb_data})
gb_test(gameplay gameplay_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido DEFS ${gb_data})
gb_test(retarget retarget_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido DEFS ${gb_data})
gb_test(anim_graph anim_graph_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido DEFS ${gb_data})
gb_test(anim_manifest anim_manifest_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido DEFS ${gb_data} ${gb_asset})
gb_test(save save_test.cpp POOL GBRenderTests LINK GB::Render LABELS rapido DEFS GB_SAVES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden/saves")
gb_test(items items_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido DEFS ${gb_assets})
gb_test(packs packs_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido DEFS ${gb_assets})
gb_test(resources resources_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido)
gb_test(world_resources world_resources_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido)
# Forests, stony ground, flowers and wild food over a generated world (WorldResources patches, bosques.json), and the soft grass between zones
gb_test(forest forest_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido DEFS ${gb_assets})
gb_test(tribe tribe_test.cpp POOL GBSimTests LINK GB::Sim sqlite3 LABELS "lento;sim" DEFS ${gb_assets})
gb_test(settlement settlement_test.cpp POOL GBSimTests LINK GB::Sim LABELS "lento;sim" DEFS ${gb_assets})
# The settlement planner alone (needs and sites from plain facts, no Tribe)
gb_test(settlement_planner settlement_planner_test.cpp POOL GBSimTests LINK GB::Sim LABELS "rapido;sim")
# The timetable alone (parts of the day, shifts, agenda factors)
gb_test(schedule schedule_test.cpp POOL GBSimTests LINK GB::Sim LABELS "rapido;sim")
# The claims on nodes alone (first piece of the logistics)
gb_test(logistics logistics_test.cpp POOL GBSimTests LINK GB::Sim LABELS "rapido;sim")
# H7: aptitudes by practice, the job board and the trades of the clan (TribeWork, Skills, JobBoard)
gb_test(work work_test.cpp POOL GBSimTests LINK GB::Sim LABELS "lento;sim" DEFS ${gb_assets})
# H18: production chains, spoilage and preservation, fields, stockpiles and hauling (Production, Spoilage, Farming, Economy, TribeEconomy)
gb_test(economy economy_test.cpp POOL GBSimTests LINK GB::Sim LABELS "rapido;sim" DEFS ${gb_assets})
# H14: conflict, combat and violence (Combat, Conflict, TribeConflict)
gb_test(combat combat_test.cpp POOL GBSimTests LINK GB::Sim LABELS "rapido;sim" DEFS ${gb_assets} JSON)
# Social life: conversations, rites, beliefs, diet, more game and traps (Social, TribeSocial, ClansBeliefs; docs/VIDA_SOCIAL.md)
gb_test(social social_test.cpp POOL GBSimTests LINK GB::Sim LABELS "lento;sim" DEFS ${gb_assets})
gb_test(knowledge knowledge_test.cpp POOL GBSimTests LINK GB::Sim LABELS "rapido;sim" DEFS ${gb_assets})
# H9 + H10: demography, inheritance, relations, personality and culture (Society, TribeSociety)
gb_test(society society_test.cpp POOL GBSimTests LINK GB::Sim LABELS "lento;sim" DEFS ${gb_assets})
gb_test(perception perception_test.cpp POOL GBSimTests LINK GB::Sim LABELS "rapido;sim")
# The brain of an agent (H5 + H6). Its crowd of a hundred is most of its time, so it is a test of its own, one per run (with and without level of
# detail): `ai` runs every other case, `ai_crowd_full` and `ai_crowd_lod` the crowd. `GBSimTests ai` with no part runs all of it (the "why" checks now before the crowd).
gb_test(ai ai_test.cpp POOL GBSimTests LINK GB::Sim LABELS "lento;sim" ARGS main)
gb_test(ai_crowd_full ai_test.cpp POOL GBSimTests LINK GB::Sim LABELS "lento;sim" ARGS crowd_full)
gb_test(ai_crowd_lod ai_test.cpp POOL GBSimTests LINK GB::Sim LABELS "lento;sim" ARGS crowd_lod)
gb_test(interactives interactives_test.cpp POOL GBSimTests LINK GB::Sim LABELS "rapido;sim" DEFS ${gb_assets})
gb_test(ecology ecology_test.cpp POOL GBSimTests LINK GB::Sim LABELS "rapido;sim" DEFS ${gb_assets})
gb_test(scale scale_test.cpp POOL GBSimTests LINK GB::Sim LABELS "rapido;sim")
gb_test(navigation navigation_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido)
# H12: regions of the generated world, the clan's map, expeditions and migration (Regions, Exploration, TribeExplore)
gb_test(exploration exploration_test.cpp POOL GBSimTests LINK GB::Sim LABELS "lento;sim" DEFS ${gb_assets} JSON)
# H13: several clans in one world, territory, relations between clans, trade, knowledge exchange, marriages and pacts (Clans, Diplomacy, Territory)
gb_test(clans clans_test.cpp POOL GBSimTests LINK GB::Sim LABELS "rapido;sim" DEFS ${gb_assets} JSON)
gb_test(anim_usage anim_usage_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido DEFS ${gb_asset})
# Animation bindings (docs/EDITOR_ANIMACIONES.md): JSON, validation, resolution with fallbacks, trim / loop / blend on an animation clock, headless
gb_test(anim_bindings anim_bindings_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido DEFS ${gb_asset} ${gb_data})
# The visual layer as data (GB/Visual): procedural meshes, textures, node looks by Amount, props by bag and job, buildings by phase
gb_test(visuals visuals_test.cpp POOL GBRenderTests LINK GB::Render LABELS rapido DEFS ${gb_asset})
# H15: the player inside the society (PlayerMember, TribePlayer, ClansPlayer)
gb_test(player player_test.cpp POOL GBSimTests LINK GB::Sim LABELS "rapido;sim" DEFS ${gb_assets})
# The priority graph: editable decision priorities per role or character, JSON library, validation, subgraphs, and the tribe unchanged with the builtin graph
gb_test(priority_graph priority_graph_test.cpp POOL GBSimTests LINK GB::Sim LABELS "rapido;sim" DEFS ${gb_assets})
gb_test(anim_bindings_world anim_bindings_world_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido DEFS ${gb_data})
gb_test(gbobject gbobject_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido)
# The terrain graph (docs/GRAFO_TERRENO.md): nodes/edges, validation (cycles, unknown types), compiling,
# evaluating noise/erosion/biome nodes, determinism, and applying a result to both terrain targets.
gb_test(terrain_graph terrain_graph_test.cpp POOL GBRenderTests LINK GB::Render LABELS rapido)
# V1 (docs/VISION_MUNDO_VIVO.md): the world's history and each person's memory of it
gb_test(history history_test.cpp POOL GBSimTests LINK GB::Sim LABELS "rapido;sim")
# V5: eras as packs (needs and eras that a later pack adds, what a building provides)
gb_test(era_pack era_pack_test.cpp POOL GBSimTests LINK GB::Sim LABELS "rapido;sim")
# T0: the master seed of a game (GB/Core/Seed.h, GB/Game/GameSeeds.h): pinned derivation, and the same master mounts the same game.
gb_test(seed seed_test.cpp POOL GBRenderTests LINK GB::Render LABELS rapido)

# Terrain graphs as files (docs/GRAFO_TERRENO.md 10.5): JSON round trip of every preset (same graph, same evaluation, same world bit for bit),
# TerrainGraphLibrary, and the shipped assets/data/terrain_graphs/*.json equal to the presets of their id.
gb_test(terrain_graph_io terrain_graph_io_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido
        DEFS GB_TERRAIN_GRAPHS_DIR="${CMAKE_SOURCE_DIR}/assets/data/terrain_graphs")
# The per-node cache of the terrain graph (TerrainGraphCache): only what changed runs again, bit for bit the same result.
# GB_TGRAPH_BENCH=1 GBSimTests terrain_graph_cache times it on the 3 km highland_valley.
gb_test(terrain_graph_cache terrain_graph_cache_test.cpp POOL GBSimTests LINK GB::Sim LABELS rapido)
# GB_WorldGen alone (docs/GRAFO_TERRENO.md 6): a pool that links only GB::WorldGen, so it linking proves the world generation needs nothing above it.
gb_test(worldgen_graph worldgen_graph_test.cpp POOL GBWorldGenTests LINK GB::WorldGen LABELS rapido)
gb_test_write_pool_mains()   # (after the last gb_test of a pool)

# Build speed (T50). Every test executable is a few .cpp files, so instead of one precompiled header each (that would cost as much as it saves) they all
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
# (A test added after this point, as a block of its own, sets its own REUSE_FROM GBTestPch: the loop above only saw the targets before it.)

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

# T25: the render frontend on the null RHI backend (no window, no GL context): the passes, the culling, the instanced draws and the order
# of the translucent objects, read from the null device's command log. An executable of its own (this block is after the pools' main()).
gb_test(rhi rhi_test.cpp EXE GBRhiTests LINK GB::Render LABELS rapido)
target_precompile_headers(GBRhiTests REUSE_FROM GBTestPch)
gb_parallel_compile(GBRhiTests)
