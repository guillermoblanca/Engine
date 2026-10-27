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
