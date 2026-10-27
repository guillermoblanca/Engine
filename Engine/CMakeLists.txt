add_library(glad STATIC vendor/glad/src/gl.c)
target_include_directories(glad SYSTEM PUBLIC vendor/glad/include)

# ufbx is third-party C code: its own target, so the engine's warning flags do not apply to it.
add_library(ufbx STATIC ${ufbx_SOURCE_DIR}/ufbx.c)
target_include_directories(ufbx SYSTEM PUBLIC ${ufbx_SOURCE_DIR})

add_library(GB STATIC
    src/GB/Application.cpp
    src/GB/Game/CharacterController.cpp
    src/GB/Game/MotionState.cpp
    src/GB/Game/AIDriver.cpp
    src/GB/Game/MotionWarp.cpp
    src/GB/Game/Interaction.cpp
    src/GB/Game/TaskList.cpp
    src/GB/Audio/AudioClip.cpp
    src/GB/Audio/AudioDevice.cpp
    src/GB/Audio/AudioEngine.cpp
    src/GB/Audio/FootstepSounds.cpp
    src/GB/Audio/AudioMixer.cpp
    src/GB/Game/Entity.cpp
    src/GB/Animation/CharacterAnimator.cpp
    src/GB/Animation/AnimInstance.cpp
    src/GB/Animation/Montage.cpp
    src/GB/Animation/AirLayer.cpp
    src/GB/Animation/Rig.cpp
    src/GB/Animation/Character.cpp
    src/GB/Animation/IK.cpp
    src/GB/Animation/Socket.cpp
    src/GB/Animation/FootIK.cpp
    src/GB/Animation/LookAt.cpp
    src/GB/Game/SceneIO.cpp
    src/GB/Game/EntityRender.cpp
    src/GB/Input.cpp
    src/GB/Log.cpp
    src/GB/Assets/Model.cpp
    src/GB/Assets/ModelFbx.cpp
    src/GB/Animation/ClipAnalysis.cpp
    src/GB/Animation/Pose.cpp
    src/GB/Animation/BlendSpace.cpp
    src/GB/Animation/Locomotion.cpp
    src/GB/World/Footsteps.cpp
    src/GB/World/Grass.cpp
    src/GB/World/GroundInteraction.cpp
    src/GB/World/Terrain.cpp
    src/GB/World/Trees.cpp
    src/GB/World/WaterRipples.cpp
    src/GB/World/Water.cpp
    src/GB/Effects/Sky.cpp
    src/GB/World/Physics.cpp
    src/GB/World/Rocks.cpp
    src/GB/World/Erosion.cpp
    src/GB/Qa/Qa.cpp
    src/GB/World/Destruction.cpp
    src/GB/World/WorldGen.cpp
    src/GB/Renderer/EnvironmentMap.cpp
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
find_package(Threads REQUIRED)
target_link_libraries(GB PUBLIC spdlog::spdlog glad glfw Threads::Threads ${CMAKE_DL_LIBS})
target_link_libraries(GB PRIVATE ufbx)
target_include_directories(GB SYSTEM PRIVATE ${stb_SOURCE_DIR} ${cgltf_SOURCE_DIR} ${json_SOURCE_DIR}/include ${miniaudio_SOURCE_DIR})
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
