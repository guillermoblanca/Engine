# Estudio del renderer de Unity 6000.5.8f1 → mejoras para GB Engine

Alcance: arquitectura del renderer. Este documento resume **ideas de diseño**; no se copia código de Unity
(los paquetes SRP y `Unity.dll` están bajo licencia de Unity, solo para referencia).

## 1. Qué se analizó y con qué evidencia

| Hallazgo | Tipo | Fuente |
|---|---|---|
| `Unity.exe` (282 KB) es solo un lanzador; busca `Unity.dll` y muestra "Unity.dll failed to load" si falta. | Observado | REA/Ghidra, `ev_04a10b8e…6216852` (strings), `ev_3b69de44…b34fe8` (overview: 491 procedimientos, 319 strings) |
| El motor nativo está en `Unity.dll` (87 MB); se distribuye `Unity_x64.pdb` (91 MB). | Observado | Listado de carpeta |
| Nombres/clases nativas (GfxDevice, BatchRendererGroup, ScriptableRenderContext…). | Observado (solo nombres de símbolos del PDB, ~263 k) | Extracción propia con regex sobre el PDB; **no** se decompiló `Unity.dll` |
| RenderGraph, culling de pasadas, fusión de pasadas, pool de recursos, Forward+, GPU Resident Drawer. | Observado en código C# distribuido | `com.unity.render-pipelines.core/universal` en `Data/Resources/PackageManager/BuiltInPackages` |
| Qué hace internamente `Unity.dll` (cuerpo de funciones, SRP Batcher nativo). | **Desconocido** | No analizado; ver §5 |

## 2. Estado actual de GB (resumen)

OpenGL 4.5 (DSA), `Renderer::Render` ejecuta una cadena fija: sombra → escena MSAA → hierba/agua/partículas →
bloom → AO/adaptación → composite. Cada objeto fija sus uniforms uno a uno (`SetMat4`, `SetFloat`…); un único mapa de sombras
direccional; culling por esfera/distancia; framebuffers creados a mano en `CreateTargets()`; hasta 32 luces puntuales
(`MaxPointLights`). Material = struct grande con ~40 parámetros que se suben todos en cada draw.

## 3. Qué hace bien Unity (con evidencia) y cómo se traduce a GB

### 3.1 RenderGraph: pasadas declarativas con recursos virtuales  (prioridad alta)
Observado en `RenderGraph/Compiler/NativePassCompiler.cs`:
- Cada pasada declara qué lee y qué escribe; el grafo se **compila** cada frame (o se reutiliza con caché por hash,
  `RenderGraphCompilationCache`).
- **Culling de pasadas** (`CullRenderGraphPassesWithNoSideEffect`): se marcan todas como culled y un recorrido
  BFS hacia atrás desde las pasadas con efectos secundarios (escribir al backbuffer/recursos importados) “revive” solo
  las necesarias. Un segundo paso elimina pasadas que solo escriben recursos que nadie lee.
- **Fusión de pasadas raster** (`TryMergeNativePasses`) para tilers móviles (subpasses); codicioso, sin reordenar.
- **Pool de recursos con alias**: los recursos se piden por descriptor (hash), se liberan al terminar su último uso y se
  reutilizan; los no usados durante 10 frames (`kStaleResourceLifetime`) se destruyen.
- Detección de recursos *memoryless* dentro de una pasada nativa.

**Para GB:** un `FrameGraph` mínimo. Hoy AO, SSR, bloom, DoF ya son condicionales “a mano”; con el grafo se activan solos
según quién consume su salida, y `CreateTargets()` desaparece (los targets se crean al vuelo y se reciclan por descriptor
`{w,h,formato,samples}`). Es además lo que facilita añadir nuevas pasadas sin tocar `Render()`.
Alcance razonable: culling por BFS + pool por hash; **sin** fusión de subpasses (no aplica a OpenGL de escritorio).

### 3.2 Separación recorrido/ejecución de la lista de renderers  (alta)
Observado en símbolos de `ScriptableRenderContext`: `CreateRendererList`, `PrepareRendererListsAsync`,
`ExecuteRendererList`, `DrawRenderers`, `DrawShadows`, `CreateShadowRendererList`. Es decir, el *qué dibujar* (culling,
ordenación) se prepara aparte, posiblemente en jobs, y luego se ejecuta (inferencia por nombres, no verificada).
**Para GB:** extraer de `ScenePass`/`ShadowPass` una fase `BuildDrawList(scene, view)` que devuelva comandos
`{mesh, material, transform, sortKey}` ordenados (opacos por estado/material y front-to-back; blend back-to-front). Hoy el
bucle de objetos hace culling, bind y draw mezclados; la lista permite reutilizarla para sombras y multithread.

### 3.3 Batching por buffer de constantes persistente (SRP Batcher)  (alta)
Observado en símbolos nativos: `ScriptableBatchRenderer::UpdateUseSRPBatcher`, `GenerateBuiltInCBuffer`,
`GetNumberOfConsecutiveStaticBuiltin`, `GfxDevice::AdjustInstancingConstantBufferBindings`,
`BatchRendererGroup::GetConstantBufferOffsetAlignment`/`GetConstantBufferMaxWindowSize`. Inferido: los parámetros de
material viven en un buffer GPU persistente y el draw solo cambia un offset/ventana; los objetos consecutivos con el mismo
shader evitan reconfigurar estado.
**Para GB:** pasar `Material` a un UBO/SSBO por material (std140, subida solo al cambiar), un UBO de objeto con la
transformada, y `glBindBufferRange` por draw. Quita decenas de `glUniform*` por objeto. Medirlo antes (contadores de
llamadas GL) para justificar.

### 3.4 Dibujo guiado por GPU / instancing  (media)
Observado: carpeta `GPUDriven` (GPU Resident Drawer, `Culling`, `Batching`, `OcclusionTestAABB` en
`BatchRendererGroup`). Es la evolución del culling a GPU con occlusion culling por AABB (Hi-Z, inferido).
**Para GB:** primero instancing automático para bosques/rocas (ya hay hierba instanciada); después, culling por compute +
`glMultiDrawElementsIndirect`. Solo merece la pena con miles de objetos distintos.

### 3.5 Iluminación: Forward+ por clústeres/tiles  (media)
Observado en `URP/Runtime/Tiling`: `TilingJob`, `ZBinningJob`, `LightMinMaxZJob`, `TileRangeExpansionJob`, `TileSize`
(luces asignadas a tiles de pantalla + bins en Z, calculado en jobs). Además atlas de sombras para luces adicionales
(`AdditionalLightsShadowAtlasLayout`) y sombras de luz principal en cascadas (`MainLightShadowCasterPass`).
**Para GB:** hoy el límite es 32 luces puntuales y todas se evalúan por píxel. Un compute de *light culling* por tiles
(grid + lista de índices en SSBO) sube el límite a cientos; cascadas (CSM) para la solar mejorarían la nitidez frente a
un único mapa de 2048 sobre `ShadowRadius`.

### 3.6 Biblioteca de shader compartida y variantes  (media)
Observado: `ShaderLibrary/` (BRDF.hlsl, Fog.hlsl, Clustering.hlsl, AmbientOcclusion.hlsl…) incluido por pasadas separadas
(ForwardPass, GBufferPass, DepthOnlyPass, DepthNormalsPass). Cada material compila solo las variantes (keywords) usadas.
**Para GB:** `Shaders.h` (1477 líneas de strings) se beneficiaría de `#include` GLSL a nivel de motor y de un sistema de
`#define` por variante (alpha-mask, skinning, viento…) en lugar de `if (uHasX)` en runtime.

### 3.7 Otros patrones útiles
- Pasadas especializadas: DepthOnly / DepthNormals (prepass), GBuffer/Deferred opcional, MotionVectors, TemporalAA,
  STP (upscaler). Candidatos futuros: **depth prepass** (reduce overdraw con MSAA) y TAA.
- Resolución dinámica (`DynamicResolutionHandler`) y `CommandBufferPool` / `ObjectPools` para evitar asignaciones por frame.
- Doble hebra de render: `GfxDeviceClient` ↔ `GfxDeviceWorker` (observado en símbolos) → cola de comandos entre hilo de
  juego y hilo de GPU. Complejo; solo si el CPU del hilo principal se vuelve el cuello de botella.
- Backends: `GfxDeviceD3D11/D3D12/VK/GLES/Null` tras una interfaz común; `GfxDeviceNull` encaja con vuestro requisito
  de ejecución headless.

## 4. Hoja de ruta propuesta (orden por retorno/riesgo)

1. **Medir**: contadores de draw calls, cambios de shader y `glUniform*` por frame (el `RenderStats` ya existe).
2. **Draw list ordenada** (§3.2) — pequeño, desbloquea lo demás, verificable con los tests golden existentes.
3. **UBO por material/objeto** (§3.3).
4. **FrameGraph mínimo** con pool de targets (§3.1).
5. **CSM + light culling por tiles** (§3.5).
6. Depth prepass, includes/variantes de shader (§3.6–3.7), GPU-driven si hace falta.

Cada paso debe mantener `ctest` y las capturas golden (`tests/golden`) sin cambios visuales.

## 5. Límites y pendientes

- No se decompiló `Unity.dll`; lo dicho sobre el SRP Batcher nativo y el culling GPU es **inferencia a partir de nombres de
  símbolos**. Para verificarlo: abrir `Unity.dll` con REA/Ghidra (87 MB, análisis largo) y mirar
  `ScriptableBatchRenderer::*` y `BatchRendererGroup::OcclusionTestAABB`.
- Los números (p. ej. 263 k símbolos, 2 598 con `GfxDevice`) provienen de un script regex sobre el PDB, no de una
  herramienta de PDB; son orientativos.
- Esta versión (6000.5.8f1) puede diferir de LTS; las ideas son generales.
