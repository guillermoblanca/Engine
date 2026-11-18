# Dear ImGui with the GLFW + OpenGL3 backends.
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw)

# The editor as a library (so tests can drive it headless) and as an executable.
add_library(GBEditor STATIC
    src/Properties.cpp
    src/Commands.cpp
    src/LogBuffer.cpp
    src/ImGuiLayer.cpp
    src/PlayerMode.cpp
    src/GameView.cpp
    src/GameMode.cpp
    src/GamePanels.cpp
    src/GameSheets.cpp
    src/SelectionView.cpp
    src/RadialMenu.cpp
    src/ColonyTabs.cpp
    src/MapOverlay.cpp
    src/ObserverView.cpp
    src/TechTreeView.cpp
    src/Lang.cpp
    src/UserSettings.cpp
    src/TrailsView.cpp
    src/SceneEditor.cpp
    src/WorldInstance.cpp
    src/TestScenarios.cpp
    src/Gameplay/EditorGameplay.cpp
    src/Gameplay/EditorGameplayParsing.cpp
    src/Gameplay/EditorExplore.cpp
    src/Gameplay/EditorClans.cpp
    src/Gameplay/EditorFounding.cpp
    src/Gameplay/EditorColonyWork.cpp
    src/Gameplay/EditorFarmZones.cpp
    src/EditorVisuals.cpp
    src/Gameplay/EditorEconomy.cpp
    src/Gameplay/EditorConflict.cpp
    src/Gameplay/EditorSocial.cpp
    src/Gameplay/EditorSave.cpp
    src/Gameplay/EditorQA.cpp
    src/Animation/EditorAnimCommands.cpp
    src/Animation/EditorAnimPanels.cpp
    src/Animation/EditorAnimBindings.cpp
    src/Animation/EditorAnimActionList.cpp
    src/Animation/EditorAnimBindingLifecycle.cpp
    src/Animation/EditorAnimClipSettings.cpp
    src/Animation/EditorAnimContext.cpp
    src/Animation/EditorAnimPreview.cpp
    src/Animation/EditorAnimTransitions.cpp
    src/Animation/EditorAnimInteraction.cpp
    src/EditorPriorityGraph.cpp
    src/TerrainGraphUi.cpp
    src/GameShell.cpp
    src/EditorApp.cpp)
# G2: the session.json of a game of GB.exe names the build it was played with (the commit at configure time; "-dirty" with local changes)
find_package(Git QUIET)
set(GB_GIT_HASH "unknown")
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --always --dirty WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                    OUTPUT_VARIABLE GB_GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE GB_GIT_RESULT)
    if(NOT GB_GIT_RESULT EQUAL 0 OR GB_GIT_HASH STREQUAL "")
        set(GB_GIT_HASH "unknown")
    endif()
endif()
set_source_files_properties(src/GameShell.cpp PROPERTIES COMPILE_DEFINITIONS "GB_GIT_HASH=\"${GB_GIT_HASH}\"")
target_include_directories(GBEditor PUBLIC src)
target_link_libraries(GBEditor PUBLIC GB::GB imgui)
target_include_directories(GBEditor SYSTEM PRIVATE ${json_SOURCE_DIR}/include)   # (H14: EditorConflict.cpp reads the "combat" section of a save)
target_compile_definitions(GBEditor PUBLIC GB_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
# The Tests panel can open the original Sandbox scene in its own window.
if(TARGET Sandbox)
    target_compile_definitions(GBEditor PRIVATE GB_SANDBOX_EXE="$<TARGET_FILE:Sandbox>")
endif()
gb_parallel_compile(GBEditor)
# Precompiled headers (T50, PRIVATE): the standard library, glm and Dear ImGui, the same lists as the engine (Engine/CMakeLists.txt).
target_precompile_headers(GBEditor PRIVATE ${GB_PCH_STD} ${GB_PCH_GLM} <imgui.h>)
if(MSVC)
    target_compile_options(GBEditor PRIVATE /W4 /permissive-)
    if(GB_WARNINGS_AS_ERRORS)
        target_compile_options(GBEditor PRIVATE /WX)
    endif()
else()
    target_compile_options(GBEditor PRIVATE -Wall -Wextra -Wpedantic)
    if(GB_WARNINGS_AS_ERRORS)
        target_compile_options(GBEditor PRIVATE -Werror)
    endif()
endif()

add_executable(Editor src/main.cpp)
target_link_libraries(Editor PRIVATE GBEditor)
gb_parallel_compile(Editor)
if(MSVC)
    target_link_options(Editor PRIVATE /MAP)   # Editor.map: what the crash trace in the log (main.cpp) points at
endif()

# G1: the game, GB.exe: the same library started as the game (GameMain.cpp, GameShell.cpp). The target is GBGame because GB is the engine's
# library; the file is GB.exe. A window program (no console behind it on a double click); run from a terminal or by ctest it still prints.
add_executable(GBGame src/GameMain.cpp)
set_target_properties(GBGame PROPERTIES OUTPUT_NAME GB)
target_link_libraries(GBGame PRIVATE GBEditor)
gb_parallel_compile(GBGame)
if(MSVC)
    target_compile_options(GBGame PRIVATE /W4 /permissive-)
    if(GB_WARNINGS_AS_ERRORS)
        target_compile_options(GBGame PRIVATE /WX)
    endif()
    target_link_options(GBGame PRIVATE /MAP /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)   # GB.map for the crash trace
else()
    target_compile_options(GBGame PRIVATE -Wall -Wextra -Wpedantic)
    if(GB_WARNINGS_AS_ERRORS)
        target_compile_options(GBGame PRIVATE -Werror)
    endif()
endif()

# Logic tests (no window, no GL).
add_executable(GBEditorTests tests/editor_test.cpp)
target_link_libraries(GBEditorTests PRIVATE GBEditor)
gb_parallel_compile(GBEditorTests)
add_test(NAME editor COMMAND GBEditorTests)
set_tests_properties(editor PROPERTIES LABELS rapido)   # (a logic test, not one of the editor runs labelled "editor": tests/CMakeLists.txt)

# Scripted runs of the real editor (hidden window; they need OpenGL like the GL tests). A line that fails makes the exit code 1 (--strict=1), and the pass
# patterns check that the replay says it is identical (a replay that differs is an order that returns false, so the script line fails).
function(gb_editor_script_test name script pass)
    # (T88: each run reads and writes a settings file of its own, never the player's saves/settings.json nor another test's)
    add_test(NAME ${name} COMMAND Editor --hidden --quality=low --strict=1 --settings=${CMAKE_CURRENT_BINARY_DIR}/test_settings/${name}.json ${ARGN} --script=${CMAKE_CURRENT_SOURCE_DIR}/tests/${script}
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(${name} PROPERTIES LABELS "editor;gl;lento" PASS_REGULAR_EXPRESSION "${pass}" FAIL_REGULAR_EXPRESSION "REPLAY DIFFERS|script line failed|Could not read the script"
                          SKIP_REGULAR_EXPRESSION "GLFW error|does not appear to support OpenGL|Failed to create|Could not create")   # no OpenGL (a CI runner without a GPU): skipped, like the GL tests
endfunction()
gb_editor_script_test(editor_replay replay_world.txt "replay identical")
gb_editor_script_test(editor_replay_society replay_society.txt "replay identical")
gb_editor_script_test(editor_replay_midrun replay_midrun.txt "replay identical")
gb_editor_script_test(editor_game_camera game_camera.txt "game camera: observe Ana Biel")
gb_editor_script_test(editor_replay_wild replay_wild.txt "replay identical")
gb_editor_script_test(editor_replay_same_world replay_same_world.txt "replay identical")   # played in the world it was recorded in (H8)
# The demos of the milestones (principle 7 of the plan): a scripted run of the real editor per milestone that asserts what the milestone promises with
# `qa expect` and `qa check` (the invariants of the simulation, GB/Game/SimQA.h) and writes a `shot_ui` capture to the build directory (not versioned).
gb_editor_script_test(editor_demo_h2 h2_gather_craft.txt "qa sim 1 day.*: 0 violation")
configure_file(tests/h3_hungry_fox.json ${CMAKE_CURRENT_BINARY_DIR}/h3_hungry_fox.json COPYONLY)   # (read by the script from the working directory)
# (--step=60: the same game time per `wait` on any machine; without it a fast idle machine reached ~16 s of game before the fire is lit at ~45 s)
gb_editor_script_test(editor_demo_h3 h3_fire_fox.txt "qa expect: 'ecology status' says" --step=60)
gb_editor_script_test(editor_demo_h5_h6 h5_h6_agents.txt "says /.*live checks, 0 violation")
# T257: a save with a wounded member, loaded as the first thing of a fresh editor (two runs: the second needs the first's save)
gb_editor_script_test(editor_wound_save t257_wound_save.txt "saved 't257_wound'")
gb_editor_script_test(editor_wound_load t257_wound_load.txt "says /effects")
set_tests_properties(editor_wound_save PROPERTIES FIXTURES_SETUP t257_wound)
set_tests_properties(editor_wound_load PROPERTIES FIXTURES_REQUIRED t257_wound)
gb_editor_script_test(editor_clans clans.txt "the Tribe panel and the tribe reports show the Blue")   # H13: two clans saved and loaded
gb_editor_script_test(editor_demo_h8 h8_settlement.txt "says /.*live checks, 0 violation")   # H8: site choice, a construction site worked, saved and loaded, the invariants
# H15: the game mode headless: play, the player's actions, save and load, the HUD. In the game the characters follow the frame time (a player drives one),
# so the test fixes it (--step=60, extra arguments go to the editor): the same stretch of game per `wait` on a fast machine and on a slow one.
gb_editor_script_test(editor_play play.txt "says /.*live checks, 0 violation" --step=60)   # (the line qa expect writes when the live checks held)
# V0 (docs/VISION_MUNDO_VIVO.md): the player takes over a member of another clan (`game become`), saves, loads and is still that member
gb_editor_script_test(editor_become v0_become.txt "says /.*live checks, 0 violation" --step=60)
# V1: the world's history and the members' memories are written while the clans live, shown in the story sheet and kept by save and load
gb_editor_script_test(editor_history v1_history.txt "says /.*live checks, 0 violation" --step=60)
# V5 + T181: a need of the data with its verb (faith, pray at the temple) that no code knows, counted once the society knows the rites (no eras)
configure_file(tests/v5_faith_fast.json ${CMAKE_CURRENT_BINARY_DIR}/v5_faith_fast.json COPYONLY)
gb_editor_script_test(editor_bronze v5_bronze.txt "prays at Temple 1" --step=60)
# V8 + T181: every content file is one pack and one tree (no eras), loaded strictly with no id twice
gb_editor_script_test(editor_eras v8_eras.txt "every invariant holds" --step=60)
# V6: the leap of years between two lives (aggregate society, history the heir remembers)
gb_editor_script_test(editor_leap v6_leap.txt "every invariant holds" --step=60)
# V7: ranks of settlements and polities of allied clans, both history
gb_editor_script_test(editor_polity v7_polity.txt "Clan 1 and Blue form the confederation" --step=60)
# V2: talking to a member brings a real fact of the game, with what it makes of it; the player remembers who told it
gb_editor_script_test(editor_talk v2_talk.txt "says /.*live checks, 0 violation" --step=60)
# V3: people ask the player for help with their real problems; a life ends and the player chooses who carries on; saved and loaded
gb_editor_script_test(editor_adventure v3_adventure.txt "says /.*live checks, 0 violation" --step=60)
# T271: the colony's selection and its right-click radial (RadialMenu.cpp): two people sent to the clay with a bubble
gb_editor_script_test(editor_colony_radial t271_radial.txt "colony radial: two people sent to the clay" --shell --step=60)
# T308: the colony's tabs Work and Stock (ColonyTabs.cpp): a priority and a stock goal set the way their clicks set them
gb_editor_script_test(editor_colony_work t308_work.txt "colony work: a priority and a stock goal set through the tabs" --shell --step=60)
# G1/G2: GB.exe (the game) headless: start menu, new god's game, pause menu, save, back to the menu, load (the same day, clans and living), the
# director's note and the session folder, every text translated. Its saves and sessions go to a folder of its own in the build directory.
add_test(NAME editor_game_shell COMMAND GBGame --hidden --quality=low --strict=1 --step=60 --settings=${CMAKE_CURRENT_BINARY_DIR}/test_settings/editor_game_shell.json
         --saves=${CMAKE_CURRENT_BINARY_DIR}/shell_test --script=${CMAKE_CURRENT_SOURCE_DIR}/tests/game_shell.txt WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(editor_game_shell PROPERTIES LABELS "editor;gl;lento" PASS_REGULAR_EXPRESSION "shell same: day" FAIL_REGULAR_EXPRESSION "REPLAY DIFFERS|script line failed|Could not read the script"
                     SKIP_REGULAR_EXPRESSION "GLFW error|does not appear to support OpenGL|Failed to create|Could not create")

# In parallel (T51): the runs write to this directory, but each with names of its own (saves/<script>, replays/<script>, <script>.png), so they
# can go several at a time: `ctest -C Release -L editor -j 4` (T51: 4 to 6 min instead of 14 at -j 1, the same failures). COST (the seconds one takes at -j 1) makes ctest start the longest first, even
# in a fresh build directory with no Testing/Temporary/CTestCostData.txt yet; otherwise the longest may start last and the run waits for it.
set_tests_properties(editor_polity PROPERTIES COST 500)
set_tests_properties(editor_bronze PROPERTIES COST 300)
set_tests_properties(editor_replay_midrun PROPERTIES COST 260)
set_tests_properties(editor_demo_h3 PROPERTIES COST 250)
set_tests_properties(editor_eras PROPERTIES COST 165)
set_tests_properties(editor_replay_society editor_demo_h5_h6 editor_history PROPERTIES COST 90)
