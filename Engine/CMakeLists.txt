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

# The engine is split by layers (documentation/02_tecnico/arquitectura/ARQUITECTURA_REVISION.md, sections 5.1 and 5.2): each target only links the
# ones below it, so an include that goes up a layer no longer links. Every target is a static library.
#   GB_Core     Core, Scene, Input                                  (glm)
#   GB_WorldGen World/Graph, Heightmap, erosions, Water, Scatter    (CPU only, only GB_Core: the generation of a world as data)
#   GB_Sim      the rest of World, Navigation, Assets, Audio, Save, Telemetry,
#               Animation, Gameplay, AI, Game, Tools                (CPU only: GBSim and the CPU tests link this, without GL or GLFW)
#   GB_RHI      Renderer/RHI without GL: the interface, the null backend   (only GB_Core: the CPU tests of the RHI link this)
#   GB_RHI_GL   Renderer/RHI/GL: the OpenGL backend                 (glad: the only library of the render that links it)
#   GB_Render   Renderer, Visual, Effects, Qa                       (over GB_RHI, with GB_RHI_GL as its device)
#   GB_Platform Platform: Window, Input, Application                (glfw)
#   GB          INTERFACE with all of them (Editor, Sandbox, the tests that open a window)
add_library(GB_Core STATIC
    src/GB/Scene/Task.cpp
    src/GB/Scene/MeshData.cpp
    src/GB/Scene/Entity.cpp
    src/GB/Scene/SceneIO.cpp
    src/GB/Input/InputActions.cpp
    src/GB/Core/Log.cpp
    src/GB/Core/Fingerprint.cpp
    src/GB/Scene/Image.cpp)

# GB_WorldGen (L2, CPU only, below GB_Sim): the generation of a world as plain data -- the terrain graph and its nodes (World/Graph), the
# heightmap, the erosions, the water model and its planner, the scatter and the biome ids (docs/GRAFO_TERRENO.md, section 6). It sees only
# GB_Core: what it builds is a guarantee of the build, not a convention (a tool or a test that only generates worlds links GB::WorldGen).
add_library(GB_WorldGen STATIC
    src/GB/World/Heightmap.cpp
    src/GB/World/Scatter.cpp
    src/GB/World/Erosion.cpp
    src/GB/World/ThermalErosion.cpp
    src/GB/World/FlowAccumulation.cpp
    src/GB/World/Drainage.cpp
    src/GB/World/Water.cpp
    src/GB/World/Water/WaterField.cpp
    src/GB/World/Water/WaterRaster.cpp
    src/GB/World/Water/WaterCarver.cpp
    src/GB/World/Water/WaterPresets.cpp
    src/GB/World/Water/WaterGeometry.cpp
    src/GB/World/Water/WaterWaves.cpp
    src/GB/World/Water/RiverHydraulics.cpp
    src/GB/World/Water/WaterPlanner.cpp
    src/GB/World/Water/LakeMesh.cpp
    src/GB/World/Graph/TerrainGraph.cpp
    src/GB/World/Graph/TerrainGraphPresets.cpp
    src/GB/World/Graph/TerrainGraphIO.cpp
    src/GB/World/Graph/TerrainGraphLibrary.cpp
    src/GB/World/Graph/TgHash.cpp
    src/GB/World/Graph/TerrainGraphCache.cpp
    src/GB/World/Graph/ITerrainNode.cpp
    src/GB/World/Graph/ITerrainTarget.cpp
    src/GB/World/Graph/Nodes/OutputNode.cpp
    src/GB/World/Graph/Nodes/HeightInputNode.cpp
    src/GB/World/Graph/Nodes/DepressionFillNode.cpp
    src/GB/World/Graph/Nodes/DrainageNodes.cpp
    src/GB/World/Graph/Nodes/LakeSiteNode.cpp
    src/GB/World/Graph/Nodes/LakeBasinNode.cpp
    src/GB/World/Graph/Nodes/RiverTraceNode.cpp
    src/GB/World/Graph/Nodes/NoiseNode.cpp
    src/GB/World/Graph/Nodes/ErosionNode.cpp
    src/GB/World/Graph/Nodes/ThermalErosionNode.cpp
    src/GB/World/Graph/Nodes/FlowAccumulationNode.cpp
    src/GB/World/Graph/Nodes/FieldNodes.cpp
    src/GB/World/Graph/Nodes/MaskNodes.cpp
    src/GB/World/Graph/Nodes/WaterCarveNodes.cpp
    src/GB/World/Graph/Nodes/ValleyNodes.cpp
    src/GB/World/Graph/Nodes/ScatterNode.cpp
    src/GB/World/Graph/Nodes/BiomeClassifyNode.cpp
    src/GB/World/Graph/Nodes/BuiltinNodes.cpp
    src/GB/World/Biome.cpp)

add_library(GB_Sim STATIC
    src/GB/Game/CharacterController.cpp
    src/GB/Game/GameCamera.cpp
    src/GB/Game/MotionState.cpp
    src/GB/Navigation/NavGrid.cpp
    src/GB/Game/AIDriver.cpp
    src/GB/Game/MotionWarp.cpp
    src/GB/Game/Interaction.cpp
    src/GB/Game/CharacterAbilityAvatar.cpp
    src/GB/Game/TaskList.cpp
    src/GB/Game/GBObject.cpp
    src/GB/Game/Components/Gameplay/MovementComponent.cpp
    src/GB/Game/Components/Gameplay/OrdersComponent.cpp
    src/GB/Game/Components/Gameplay/AbilityComponent.cpp
    src/GB/Game/Components/Presentation/GeometryComponent.cpp
    src/GB/Game/Components/Physics/RagdollComponent.cpp
    src/GB/Game/Components/Presentation/AnimationComponent.cpp
    src/GB/Game/Components/Presentation/PropsComponent.cpp
    src/GB/Game/Actor.cpp
    src/GB/Game/ActorWorld.cpp
    src/GB/Game/GameSession.cpp
    src/GB/Gameplay/GameplayTag.cpp
    src/GB/Gameplay/AttributeSet.cpp
    src/GB/Gameplay/GameplayEffect.cpp
    src/GB/Gameplay/AbilitySystemComponent.cpp
    src/GB/Gameplay/AbilityTasks.cpp
    src/GB/Gameplay/GameplayWorld.cpp
    src/GB/Gameplay/Garden.cpp
    src/GB/Gameplay/Items.cpp
    src/GB/Gameplay/ShippedContent.cpp
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
    src/GB/Tools/DocsExport.cpp
    src/GB/Gameplay/Combat.cpp
    src/GB/Gameplay/Vitals.cpp
    src/GB/Game/Tribe.cpp
    src/GB/Game/TribeWork.cpp
    src/GB/Game/TribeExplore.cpp
    src/GB/Game/TribeEconomy.cpp
    src/GB/Game/TribeConflict.cpp
    src/GB/Game/Trails.cpp
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
    src/GB/Game/SettlementPlanner.cpp
    src/GB/Game/Schedule.cpp
    src/GB/Game/GameContent.cpp
    src/GB/Game/TribeClans.cpp
    src/GB/Game/TribeHistory.cpp
    src/GB/Game/TribeNeeds.cpp
    src/GB/Game/TribeLeap.cpp
    src/GB/Game/ClansPolity.cpp
    src/GB/Game/DataNeeds.cpp
    src/GB/Game/History.cpp
    src/GB/Game/Values.cpp
    src/GB/Game/GameSeeds.cpp
    src/GB/Game/Requests.cpp
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
    src/GB/AI/PriorityGraph.cpp
    src/GB/AI/PriorityLibrary.cpp
    src/GB/Game/TribePriorities.cpp
    src/GB/AI/Mind.cpp
    src/GB/AI/Abstract.cpp
    src/GB/Audio/AudioClip.cpp
    src/GB/Audio/AudioDevice.cpp
    src/GB/Audio/AudioEngine.cpp
    src/GB/Audio/FootstepSounds.cpp
    src/GB/Audio/AudioMixer.cpp
    src/GB/Animation/CharacterAnimator.cpp
    src/GB/Animation/AnimInstance.cpp
    src/GB/Animation/Montage.cpp
    src/GB/Animation/AirLayer.cpp
    src/GB/Animation/AnimManifest.cpp
    src/GB/Gameplay/AnimBindings.cpp
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
    src/GB/Save/SaveGame.cpp
    src/GB/Save/SaveWorld.cpp
    src/GB/Save/SaveFiles.cpp
    src/GB/Save/Replay.cpp
    src/GB/Animation/ReachLayer.cpp
    src/GB/Assets/Model.cpp
    src/GB/Assets/ModelFbx.cpp
    src/GB/Animation/ClipAnalysis.cpp
    src/GB/Assets/Pose.cpp
    src/GB/Animation/BlendSpace.cpp
    src/GB/Animation/Locomotion.cpp
    src/GB/World/Footsteps.cpp
    src/GB/World/Regions.cpp
    src/GB/World/WorldTiles.cpp
    src/GB/World/Grass.cpp
    src/GB/World/Terrain.cpp
    src/GB/World/Trees.cpp
    src/GB/World/Physics.cpp
    src/GB/World/Rocks.cpp
    src/GB/World/Destruction.cpp
    src/GB/World/WorldGen.cpp
    src/GB/World/WorldGenLegacy.cpp
    src/GB/Assets/ModelInstance.cpp)

add_library(GB_Render STATIC
    src/GB/Visual/EntityRender.cpp
    src/GB/Visual/TribeVisuals.cpp
    src/GB/Visual/ProcMesh.cpp
    src/GB/Visual/ProcTexture.cpp
    src/GB/Visual/VisualDefs.cpp
    src/GB/Visual/PropVisuals.cpp
    src/GB/Visual/SceneVisuals.cpp
    src/GB/Visual/BuildingVisuals.cpp
    src/GB/Visual/VisualContent.cpp
    src/GB/Visual/ActorRender.cpp
    src/GB/Effects/Sky.cpp
    src/GB/Qa/Qa.cpp
    src/GB/Renderer/EnvironmentMap.cpp
    src/GB/Effects/Lightning.cpp
    src/GB/Effects/SandSpray.cpp
    src/GB/Effects/Weather.cpp
    src/GB/Renderer/CameraController.cpp
    src/GB/Renderer/Framebuffer.cpp
    src/GB/Renderer/GrassRenderer.cpp
    src/GB/Renderer/GrassGpu.cpp
    src/GB/Renderer/GroundInteraction.cpp
    src/GB/Renderer/Mesh.cpp
    src/GB/Renderer/Particles.cpp
    src/GB/Renderer/ParticleRenderer.cpp
    src/GB/Renderer/PostProcessor.cpp
    src/GB/Renderer/Renderer.cpp
    src/GB/Renderer/Shader.cpp
    src/GB/Renderer/ShadowRenderer.cpp
    src/GB/Renderer/Texture.cpp
    src/GB/Renderer/RocksGpu.cpp
    src/GB/Renderer/ModelGpu.cpp
    src/GB/Renderer/TerrainGpu.cpp
    src/GB/Renderer/TreesGpu.cpp
    src/GB/Renderer/WaterRenderer.cpp
    src/GB/Renderer/WaterRipples.cpp
    src/GB/Renderer/WorldTilesGpu.cpp
    src/GB/Renderer/Visibility.cpp
    src/GB/Renderer/MaterialLayers.cpp)

# GB_RHI (T65, ARQUITECTURA_REVISION 5.2): the render hardware interface and its null backend, without GL. GB_RHI_GL: the GL backend and
# the default device (GetDevice), the only library of the render that links glad.
add_library(GB_RHI STATIC
    src/GB/Renderer/RHI/RHI.cpp
    src/GB/Renderer/RHI/NullDevice.cpp)

add_library(GB_RHI_GL STATIC
    src/GB/Renderer/RHI/GL/GLDevice.cpp
    src/GB/Renderer/RHI/GL/DefaultDevice.cpp)

add_library(GB_Platform STATIC
    src/GB/Platform/Application.cpp
    src/GB/Platform/Input.cpp
    src/GB/Platform/Window.cpp)

add_library(GB::Core ALIAS GB_Core)
add_library(GB::WorldGen ALIAS GB_WorldGen)
add_library(GB::Sim ALIAS GB_Sim)
add_library(GB::RHI ALIAS GB_RHI)
add_library(GB::RHI_GL ALIAS GB_RHI_GL)
add_library(GB::Render ALIAS GB_Render)
add_library(GB::Platform ALIAS GB_Platform)

# GB_Core (L0-L2 without dependencies): Core, Scene, Input. glm is header-only and part of the public API (SYSTEM so its headers do not
# trip our warning flags); spdlog stays behind Log.h (H10); stb_image (Image.cpp) and json (SceneIO, InputActions) are private.
target_include_directories(GB_Core PUBLIC src)
target_include_directories(GB_Core SYSTEM PUBLIC ${glm_SOURCE_DIR})
target_link_libraries(GB_Core PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
target_link_libraries(GB_Core PRIVATE spdlog::spdlog)
target_include_directories(GB_Core SYSTEM PRIVATE ${stb_SOURCE_DIR} ${json_SOURCE_DIR}/include)

# GB_WorldGen: only GB_Core (glm); json (TerrainGraphIO.cpp) is private.
target_link_libraries(GB_WorldGen PUBLIC GB_Core)
target_include_directories(GB_WorldGen SYSTEM PRIVATE ${json_SOURCE_DIR}/include)

# GB_Sim (L2-L4, CPU only): what GBSim and the CPU tests link. No glad, no glfw, no imgui.
target_link_libraries(GB_Sim PUBLIC GB_WorldGen)
target_link_libraries(GB_Sim PRIVATE ufbx sqlite3)
target_include_directories(GB_Sim SYSTEM PRIVATE ${cgltf_SOURCE_DIR} ${json_SOURCE_DIR}/include ${miniaudio_SOURCE_DIR})
# the game's data (ShippedContent.h): the catalogue is read from here unless the application says otherwise
target_compile_definitions(GB_Sim PRIVATE GB_CONTENT_DIR="${CMAKE_SOURCE_DIR}/assets/data")

# GB_RHI: only GB_Core (glm, Log). GB_RHI_GL: GB_RHI, and glad private.
target_link_libraries(GB_RHI PUBLIC GB_Core)
target_link_libraries(GB_RHI_GL PUBLIC GB_RHI)
target_link_libraries(GB_RHI_GL PRIVATE glad)

# GB_Render (L5): the render frontend, Visual, Effects and Qa, over the RHI (GB_RHI, public: its headers use the RHI's types); the GL
# backend (GB_RHI_GL) is private: nothing in GB_Render's headers sees it, and it still links into every program that links GB_Render.
target_link_libraries(GB_Render PUBLIC GB_Sim GB_RHI)
target_link_libraries(GB_Render PRIVATE GB_RHI_GL)
target_include_directories(GB_Render SYSTEM PRIVATE ${stb_SOURCE_DIR} ${json_SOURCE_DIR}/include)

# GB_Platform (L5): window, input and application loop (glfw, private).
target_link_libraries(GB_Platform PUBLIC GB_Render)
target_link_libraries(GB_Platform PRIVATE glad glfw)

foreach(gb_target GB_Core GB_WorldGen GB_Sim GB_RHI GB_RHI_GL GB_Render GB_Platform)
    gb_parallel_compile(${gb_target})
    if(MSVC)
        target_compile_options(${gb_target} PRIVATE /W4 /permissive-)
        if(GB_WARNINGS_AS_ERRORS)
            target_compile_options(${gb_target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${gb_target} PRIVATE -Wall -Wextra -Wpedantic)
        if(GB_WARNINGS_AS_ERRORS)
            target_compile_options(${gb_target} PRIVATE -Werror)
        endif()
    endif()
endforeach()

# Precompiled headers (T50), PRIVATE: they speed up the target's own .cpp files and nothing else (no consumer sees them). Only the standard
# library and the third-party headers each layer may already include, never a GB header (touching one would rebuild the whole target) and
# never spdlog (private to Log.cpp, T39), glad, stb, cgltf, miniaudio or ufbx.
set(GB_PCH_STD <algorithm> <array> <chrono> <cmath> <cstdint> <cstdio> <cstdlib> <filesystem> <fstream> <functional> <map> <memory>
    <optional> <set> <sstream> <string> <unordered_map> <utility> <vector>)
set(GB_PCH_GLM <glm/glm.hpp> <glm/gtc/matrix_transform.hpp> <glm/gtc/quaternion.hpp>)
set(GB_PCH_STD ${GB_PCH_STD} PARENT_SCOPE)   # (the Editor reuses the list)
set(GB_PCH_GLM ${GB_PCH_GLM} PARENT_SCOPE)
target_precompile_headers(GB_Core PRIVATE ${GB_PCH_STD} ${GB_PCH_GLM})
target_precompile_headers(GB_WorldGen PRIVATE ${GB_PCH_STD} ${GB_PCH_GLM})
target_precompile_headers(GB_Sim PRIVATE ${GB_PCH_STD} ${GB_PCH_GLM} <nlohmann/json.hpp>)   # json: about 40 of its .cpp files read data
target_precompile_headers(GB_Render PRIVATE ${GB_PCH_STD} ${GB_PCH_GLM})                   # no glad: GL stays in the .cpp files that use it
# GB_Platform has three .cpp files, GB_RHI and GB_RHI_GL two each: building a PCH would cost about as much as it saves.

# GB: everything, for the windowed applications (Editor, Sandbox) and the tests that open a window. glad and glfw stay visible to them,
# as before the split (the Editor includes <glad/gl.h> and <GLFW/glfw3.h> itself).
add_library(GB INTERFACE)
target_link_libraries(GB INTERFACE GB_Platform glad glfw)
add_library(GB::GB ALIAS GB)
