add_library(glad STATIC vendor/glad/src/gl.c)
target_include_directories(glad SYSTEM PUBLIC vendor/glad/include)

# ufbx is third-party C code: its own target, so the engine's warning flags do not apply to it.
add_library(ufbx STATIC ${ufbx_SOURCE_DIR}/ufbx.c)
target_include_directories(ufbx SYSTEM PUBLIC ${ufbx_SOURCE_DIR})

# SQLite is third-party C code: its own target, so the engine's warning flags do not apply to it.
add_library(sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3 SYSTEM PUBLIC ${sqlite3_SOURCE_DIR})
target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)
find_package(Threads REQUIRED)
target_link_libraries(sqlite3 PUBLIC Threads::Threads)

add_library(GB STATIC
    src/GB/Application.cpp
    src/GB/Game/CharacterController.cpp
    src/GB/Game/GameCamera.cpp
    src/GB/Game/MotionState.cpp
    src/GB/Navigation/NavGrid.cpp
    src/GB/Game/AIDriver.cpp
    src/GB/Game/MotionWarp.cpp
    src/GB/Game/Interaction.cpp
    src/GB/Game/TaskList.cpp
    src/GB/Game/Actor.cpp
    src/GB/Game/ActorWorld.cpp
    src/GB/Gameplay/GameplayTag.cpp
    src/GB/Gameplay/AttributeSet.cpp
    src/GB/Gameplay/GameplayEffect.cpp
    src/GB/Gameplay/AbilitySystemComponent.cpp
    src/GB/Gameplay/AbilityTasks.cpp
    src/GB/Gameplay/GameplayWorld.cpp
    src/GB/Gameplay/Garden.cpp
    src/GB/Gameplay/Items.cpp
    src/GB/Gameplay/Techs.cpp
    src/GB/Gameplay/ContentPack.cpp
    src/GB/Gameplay/Resources.cpp
    src/GB/Gameplay/WorldResources.cpp
    src/GB/Gameplay/Buildings.cpp
    src/GB/Gameplay/SiteChoice.cpp
    src/GB/Gameplay/Interactives.cpp
    src/GB/Gameplay/Ecology.cpp
    src/GB/Gameplay/Spoilage.cpp
    src/GB/Gameplay/Farming.cpp
    src/GB/Gameplay/DocsExport.cpp
    src/GB/Gameplay/Combat.cpp
    src/GB/Game/Tribe.cpp
    src/GB/Game/TribeWork.cpp
    src/GB/Game/TribeExplore.cpp
    src/GB/Game/TribeEconomy.cpp
    src/GB/Game/TribeConflict.cpp
    src/GB/Game/Conflict.cpp
    src/GB/Game/ClanHostility.cpp

    src/GB/Game/Economy.cpp
    src/GB/Game/Production.cpp
    src/GB/Game/Exploration.cpp
    src/GB/Game/Skills.cpp
    src/GB/Game/JobBoard.cpp
    src/GB/Game/Knowledge.cpp
    src/GB/Game/Society.cpp
    src/GB/Game/SocietyRules.cpp
    src/GB/Game/TribeSociety.cpp
    src/GB/Game/Social.cpp
    src/GB/Game/SocialRules.cpp
    src/GB/Game/TribeSocial.cpp
    src/GB/Game/ClansBeliefs.cpp
    src/GB/Game/SimQA.cpp
    src/GB/Game/Chronicle.cpp
    src/GB/Telemetry/TelemetrySink.cpp
    src/GB/Game/TribeTelemetry.cpp
    src/GB/Game/Settlement.cpp
    src/GB/Game/GameContent.cpp
    src/GB/Game/TribeClans.cpp
    src/GB/Game/Diplomacy.cpp
    src/GB/Game/Territory.cpp
    src/GB/Game/Clans.cpp
    src/GB/Game/ClansPlayer.cpp
    src/GB/Game/PlayerMember.cpp
    src/GB/Game/TribePlayer.cpp
    src/GB/AI/Perception.cpp
    src/GB/AI/Memory.cpp
    src/GB/AI/Needs.cpp
    src/GB/AI/Utility.cpp
    src/GB/AI/Mind.cpp
    src/GB/AI/Abstract.cpp
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
    src/GB/Animation/AnimManifest.cpp
    src/GB/Animation/AnimBindings.cpp
    src/GB/Animation/TurnLayer.cpp
    src/GB/Animation/Rig.cpp
    src/GB/Animation/Character.cpp
    src/GB/Animation/IK.cpp
    src/GB/Animation/Socket.cpp
    src/GB/Animation/Ragdoll.cpp
    src/GB/Animation/Retarget.cpp
    src/GB/Animation/AnimGraph.cpp
    src/GB/Animation/FootIK.cpp
    src/GB/Animation/LookAt.cpp
    src/GB/Game/SceneIO.cpp
    src/GB/Save/SaveGame.cpp
    src/GB/Save/SaveWorld.cpp
    src/GB/Save/SaveFiles.cpp
    src/GB/Save/Replay.cpp
    src/GB/Save/GameSession.cpp
    src/GB/Game/EntityRender.cpp
    src/GB/Game/TribeVisuals.cpp
    src/GB/Animation/ReachLayer.cpp
    src/GB/Visual/ProcMesh.cpp
    src/GB/Visual/ProcTexture.cpp
    src/GB/Visual/VisualDefs.cpp
    src/GB/Visual/PropVisuals.cpp
    src/GB/Visual/SceneVisuals.cpp
    src/GB/Visual/BuildingVisuals.cpp
    src/GB/Input.cpp
    src/GB/Log.cpp
    src/GB/Assets/Model.cpp
    src/GB/Assets/ModelFbx.cpp
    src/GB/Animation/ClipAnalysis.cpp
    src/GB/Animation/Pose.cpp
    src/GB/Animation/BlendSpace.cpp
    src/GB/Animation/Locomotion.cpp
    src/GB/World/Footsteps.cpp
    src/GB/World/Regions.cpp
    src/GB/World/WorldTiles.cpp
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
    src/GB/Renderer/GrassRenderer.cpp
    src/GB/Renderer/Image.cpp
    src/GB/Renderer/Mesh.cpp
    src/GB/Renderer/Particles.cpp
    src/GB/Renderer/ParticleRenderer.cpp
    src/GB/Renderer/PostProcessor.cpp
    src/GB/Renderer/Renderer.cpp
    src/GB/Renderer/Shader.cpp
    src/GB/Renderer/ShadowRenderer.cpp
    src/GB/Renderer/Texture.cpp
    src/GB/Renderer/WaterRenderer.cpp
    src/GB/Renderer/Visibility.cpp)
add_library(GB::GB ALIAS GB)

target_include_directories(GB PUBLIC src)
find_package(Threads REQUIRED)
target_link_libraries(GB PUBLIC spdlog::spdlog glad glfw Threads::Threads ${CMAKE_DL_LIBS})
target_link_libraries(GB PRIVATE ufbx sqlite3)
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
