# Modelos, animación y cámaras

Importación glTF y FBX, animación esquelética, análisis de clips y cámaras.

[← Volver al README](../README.md)

## Importación glTF

`GB::Model::Load("modelo.glb")` carga glTF 2.0 (`.glb`/`.gltf`, exportado de Blender: *File > Export > glTF 2.0*) y `model->AddToScene(scene)` lo añade al renderer.

Soportado: mallas (normales/tangentes generadas si faltan), jerarquía de nodos, materiales metálico-rugosidad con texturas (color base, metal/rugosidad, normal, oclusión, emisivo), modos alfa (opaco, máscara, mezcla ordenada), doble cara, `KHR_materials_emissive_strength`, texturas embebidas o externas.
Pendiente: morph targets, luces del archivo, UV distintas de UV0, `KHR_texture_transform`.

`Sandbox --scene=model --model=ruta.glb [--yaw=GRADOS] [--nofloor]` (por defecto el casco de `assets/models`, CC BY 4.0).
Validado con el casco y modelos de prueba de Khronos (BoomBox, AlphaBlendModeTest, NormalTangentMirrorTest) y con un `.glb` generado por `tools/make_test_gltf.py` para el test automático.

![glTF](gltf_models.jpg)

## Animación

```cpp
auto model = GB::Model::Load("personaje.glb");          // asset inmutable y compartible
GB::ModelInstance actor(*model);                        // estado de reproducción + pose
actor.Play("Walk");                                     // bucle por defecto, mezcla de 0,2 s
actor.Play("Run", true, 0.3f);                          // cross-fade de 0,3 s desde la animación actual
actor.Update(dt);                                       // cada frame
actor.AddToScene(scene);                                // objetos con la pose actual (skinning en GPU)
```

Animación de nodos y esqueletal (skinning por GPU con SSBO de huesos, también en las sombras), interpolación STEP / LINEAR (slerp) / CUBICSPLINE, bucle o una sola pasada, velocidad, pausa, `SetTime` y mezcla entre clips.
`Sandbox --scene=model --model=x.glb --anim=Walk --time=0.3` (congela ese instante, ideal para capturas); teclas 1-9 cambian de clip y Espacio pausa.
Pendiente: morph targets (blend shapes), máquina de estados / blend trees, retargeting.

### Grabar un clip

```
xvfb-run -a ./build/Sandbox/Sandbox --scene=model --model=Fox.glb --playlist=Survey,Walk,Run --switch=1.2 \
    --orbit=25 --size=960x540 --hidden --frames=96 --fps=24 --record=frames
ffmpeg -framerate 24 -i frames/frame_%05d.png -c:v libx264 -pix_fmt yuv420p clip.mp4
```
`--record=DIR` guarda un PNG por frame con paso de tiempo fijo (1/fps), así el resultado es determinista aunque el render sea lento.

![Animación](animation.jpg)

## Cámaras

`GB::Camera`: perspectiva u ortográfica, `Camera::Blend(a, b, t)` para transiciones. Controladores (usables por código o por entrada):
- `OrbitController`: órbita alrededor de un punto (ratón: izq. rota, der./centro desplaza, rueda zoom; flechas/WASD, Q/E, stick derecho).
- `FlyController`: vuelo libre (WASD + Q/E, Shift acelera, botón derecho mira, rueda cambia la velocidad, sticks del mando).
- `FollowController`: cámara de seguimiento en tercera persona con suavizado independiente de los fps.
- Cámaras del glTF (p. ej. las de Blender, perspectiva u ortográficas, **animadas con el archivo**): `model->GetCameras()` y `instance.GetCamera(i, camera)`.

Sandbox: `C` (o botón Y del mando) recorre órbita / vuelo / cámaras del archivo con transición suave; `--camera=orbit|fly|N`.
`tools/add_camera_rig.py in.glb out.glb` añade a un modelo una plataforma de cámaras de demostración (giro 360°, travelling, cenital ortográfica).

## FBX y análisis de clips

`Model::Load` abre también `.fbx` (ufbx v0.9.0 fijada por FetchContent): malla, esqueleto, skin (4 influencias por vértice), materiales y animaciones, convertidos a **metros, Y arriba, mano derecha** (un FBX en centímetros como los de Mixamo se importa a escala real). Las animaciones se hornean a 30 fps por hueso; un archivo con una sola toma da su nombre al clip (`walking.fbx` → clip `walking`) y las tomas de un fotograma (la T-pose de Mixamo) se descartan.

- `Model::AppendAnimations(archivo.fbx)`: añade los clips de otro FBX (p. ej. un clip de Mixamo sin malla) emparejando los huesos por nombre; informa de los huesos que el modelo no tiene.
- `AnalyzeClip(model, clip)` (`GB/Animation/ClipAnalysis.h`): mide el movimiento de la raíz (desplazamiento, velocidad m/s, giro), la altura de la cadera, la calidad del bucle (grados por hueso entre la primera y la última pose), los apoyos de cada pie (tiempos normalizados), la longitud del ciclo de paso y la fase de vuelo de los saltos. Son los datos que necesitan los blend trees, el root motion y el audio de pasos.
- Editor: `load <modelo.fbx>`, `anims <archivo.fbx|carpeta>`, `clips`, `play <clip> [fundido]` y `anim_stats [clip|all]`.
- Test `fbx` (CPU, sin GL) con ficheros ASCII pequeños generados por `tools/make_test_fbx.py`; con `GB_MIXAMO_DIR=<carpeta del pack>` comprueba además el pack real (no está en el repositorio).

Medido en el pack Mixamo *Male Locomotion*: caminar 1,03 s a 1,67 m/s (ciclo de 1,72 m), correr 0,73 s a 4,42 m/s (3,24 m), strafes andando 1,73 m/s y corriendo 4,3–4,5 m/s, giros de 90° en sitio (≈0,93 s), salto de 2,17 s con 21 % de vuelo, idle de 8,33 s en bucle. Los clips llevan el movimiento en el hueso `Hips` (root motion), así que mezclar caminar y correr exige escalar la reproducción y sincronizar los pies. El estudio completo y el plan de locomoción están en `docs/ANIMACION_LOCOMOCION.md`.

## Mezcla de poses y locomoción

`GB/Animation/` (CPU, sin GL):

- `Pose`: una `BonePose` por hueso; `SampleClip`, `BlendPoses` (rotaciones por el camino corto), `BlendPosesMasked` y `MakeBoneMask` (máscaras por hueso, p. ej. tronco y brazos). `ModelInstance::SetPose` aplica una pose calculada fuera (animador, blend tree) y deja de reproducir clips propios.
- `BlendSpace`: clips colocados en un plano (1D o 2D). Los pesos son exactos en cada clip y lineales entre ellos (triangulación de Delaunay de las posiciones, sin triángulos degenerados); fuera de los clips se usa el punto más cercano, así que nunca se mezcla hacia una velocidad que ningún clip cubre.
- `LocomotionBlender`: andar/correr/strafe a partir de clips colocados por su **velocidad de raíz medida** (`MakeGaitInfo`). Una fase de ciclo compartida alineada con el apoyo del pie izquierdo (los pies no se cruzan al mezclar), velocidad de reproducción escalada a la velocidad pedida (sin patinaje entre las velocidades propias de los clips, limitada a 0,6–1,5x), movimiento de raíz eliminado (manda el controlador) y **eventos de apoyo** `FootEvent` (izquierdo/derecho) cuando el ciclo cruza un apoyo.
- `FootstepSounds` (`GB/Audio/`): reproduce el paso de la superficie bajo el pie (hierba, tierra, piedra, arena, madera, nieve, agua), con variaciones y posición 3D.
- Editor: `locomotion on|off` (con el personaje y sus clips cargados: `load`, `anims`), propiedades `loco.speed`, `loco.strafe`, `loco.turn`, `loco.follow`, `loco_info`, y `audio_record <wav>` / `audio_stop` para grabar el audio en ejecuciones headless (el editor mezcla al ritmo de la simulación). La superficie sale del bioma del mundo.
- Test `locomotion` (CPU) con el rig generado (`idle`, andar 1,2 m/s y correr 3 m/s con pies que de verdad se apoyan); con `GB_MIXAMO_DIR` mezcla también el pack Mixamo real.

Siguiente (11A-3): máquina de estados con parámetros y transiciones (idle, locomoción, salto, giro en sitio), controlador de personaje y cámara de seguimiento.
