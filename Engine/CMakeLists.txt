add_library(glad STATIC vendor/glad/src/gl.c)
target_include_directories(glad SYSTEM PUBLIC vendor/glad/include)

add_library(GB STATIC
    src/GB/Application.cpp
    src/GB/Input.cpp
    src/GB/Log.cpp
    src/GB/Assets/Model.cpp
    src/GB/World/Footsteps.cpp
    src/GB/World/Grass.cpp
    src/GB/World/GroundInteraction.cpp
    src/GB/World/Terrain.cpp
    src/GB/World/Trees.cpp
    src/GB/World/WaterRipples.cpp
    src/GB/World/Water.cpp
    src/GB/Effects/Sky.cpp
    src/GB/Effects/Lightning.cpp
    src/GB/Effects/SandSpray.cpp
    src/GB/Effects/Weather.cpp
    src/GB/Assets/ModelInstance.cpp
    src/GB/Window.cpp
    src/GB/Renderer/CameraController.cpp
    src/GB/Renderer/Framebuffer.cpp
    src/GB/Renderer/Image.cpp
    src/GB/Renderer/Mesh.cpp
    src/GB/Renderer/Particles.cpp
    src/GB/Renderer/Renderer.cpp
    src/GB/Renderer/Shader.cpp
    src/GB/Renderer/Texture.cpp)
add_library(GB::GB ALIAS GB)

target_include_directories(GB PUBLIC src)
target_link_libraries(GB PUBLIC spdlog::spdlog glad glfw)
target_include_directories(GB SYSTEM PRIVATE ${stb_SOURCE_DIR} ${cgltf_SOURCE_DIR})
# glm is header-only; added as SYSTEM so its headers do not trip our warning flags.
target_include_directories(GB SYSTEM PUBLIC ${glm_SOURCE_DIR})

if(MSVC)
    target_compile_options(GB PRIVATE /W4 /permissive-)
    if(GB_WARNINGS_AS_ERRORS)
        target_compile_options(GB PRIVATE /WX)
    endif()
else()
    target_compile_options(GB PRIVATE -Wall -Wextra -Wpedantic)
    if(GB_WARNINGS_AS_ERRORS)
        target_compile_options(GB PRIVATE -Werror)
    endif()
endif()
