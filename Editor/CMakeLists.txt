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
    src/SceneEditor.cpp
    src/WorldInstance.cpp
    src/TestScenarios.cpp
    src/EditorGameplay.cpp
    src/EditorSave.cpp
    src/EditorAnimCommands.cpp
    src/EditorAnimPanels.cpp
    src/EditorApp.cpp)
target_include_directories(GBEditor PUBLIC src)
target_link_libraries(GBEditor PUBLIC GB::GB imgui)
target_compile_definitions(GBEditor PUBLIC GB_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
# The Tests panel can open the original Sandbox scene in its own window.
if(TARGET Sandbox)
    target_compile_definitions(GBEditor PRIVATE GB_SANDBOX_EXE="$<TARGET_FILE:Sandbox>")
endif()
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

# Logic tests (no window, no GL).
add_executable(GBEditorTests tests/editor_test.cpp)
target_link_libraries(GBEditorTests PRIVATE GBEditor)
add_test(NAME editor COMMAND GBEditorTests)

# Scripted runs of the real editor (hidden window; they need OpenGL like the GL tests). A line that fails makes the exit code 1 (--strict=1), and the pass
# patterns check that the replay says it is identical (a replay that differs is an order that returns false, so the script line fails).
function(gb_editor_script_test name script pass)
    add_test(NAME ${name} COMMAND Editor --hidden --quality=low --strict=1 --script=${CMAKE_CURRENT_SOURCE_DIR}/tests/${script}
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(${name} PROPERTIES PASS_REGULAR_EXPRESSION "${pass}" FAIL_REGULAR_EXPRESSION "REPLAY DIFFERS|script line failed|Could not read the script")
endfunction()
gb_editor_script_test(editor_replay replay_world.txt "replay identical")
gb_editor_script_test(editor_replay_society replay_society.txt "replay identical")
gb_editor_script_test(editor_replay_midrun replay_midrun.txt "replay identical")
