# Estudio: pack Mixamo "Male Locomotion" → animación y locomoción en GB Engine

## 1. Qué contiene el pack (medido, no supuesto)

11 archivos FBX binarios (5,7 MB): `X Bot.fbx` (malla + esqueleto) y 10 clips solo de animación.

**Personaje (`X Bot.fbx`)**: 2 mallas (`Beta_Surface` 14 232 vértices / 28 272 triángulos, `Beta_Joints`), 2 materiales sin texturas, 65 huesos `mixamorig:*` (Hips → Spine/Spine1/Spine2 → Neck/Head, brazos con 5 dedos × 3 falanges por mano, piernas con Foot/ToeBase/Toe_End). Unidades en **centímetros**, **Y arriba**, 30 fps. Altura de cadera en reposo ≈ 103 cm.

**Clips** (esqueleto idéntico en todos: 65 huesos, 159 curvas de translación/rotación/escala por clip, claves casi a 30 fps):

| Clip | Duración | Desplazamiento del root (Hips) | Velocidad implícita | Notas |
|---|---|---|---|---|
| idle | 8,30 s | 0 (se queda en sitio) | 0 | bucle, balanceo mínimo |
| walking | 1,03 s | 172 cm en +Z | **1,67 m/s** | root motion cocinado en el hueso Hips; ciclo = 2 pasos |
| standard run | 0,73 s | 324 cm en +Z | **4,42 m/s** | ciclo = 2 pasos, 3,24 m por ciclo |
| left strafe walking | 1,03 s | 179 cm en +X | 1,73 m/s | cadera girada ≈ 66° respecto al avance |
| right strafe walking | 1,03 s | 179 cm en −X | 1,73 m/s | |
| left strafe (carrera) | 0,67 s | 301 cm en +X | 4,52 m/s | |
| right strafe (carrera) | 0,63 s | 288 cm en −X | 4,54 m/s | |
| left turn 90 | 0,90 s | ≈ 2 cm (giro en sitio) | – | la cadera gira ≈ 88° (−6 → 82°) |
| right turn 90 | 0,93 s | ≈ 2 cm | – | ≈ −89° |
| jump | 2,17 s | casi 0 en horizontal | – | la cadera sube de 75 a 130 cm; despegue/aire/aterrizaje en un solo clip |

Consecuencias directas:

1. **El movimiento está dentro de la animación** (root motion en el hueso Hips, no hay hueso raíz aparte). Los clips de caminar y correr avanzan solos; para un personaje controlado hace falta decidir quién manda: la animación (root motion) o el controlador (velocidad), y para mezclar hay que poder separar ambos.
2. **Las duraciones de ciclo no coinciden** (walk 1,03 s, run 0,73 s, strafes 0,63–1,03 s). Mezclar caminar↔correr sin sincronizar fases hace que los pies "patinen" o se crucen. Hace falta **sincronización por fase del paso** (normalizar el tiempo del ciclo y alinear los apoyos de pie) o al menos un tiempo normalizado común.
3. **Hay velocidades de referencia por clip** (1,67 / 4,42 m/s): permiten **escalar la velocidad de reproducción** según la velocidad real del personaje (`speed_clip = v_real / v_ref`) y evitar el patinaje.
4. Los clips **no traen eventos** (apoyos de pie, despegue, aterrizaje): hay que **detectarlos** (el pie más bajo, la altura de cadera, el aire en `jump`) y guardarlos como marcadores para pasos (audio ya listo: 7 superficies) y para la física del salto.
5. Los giros de 90° son **en sitio**: encajan con una capa de "giro en parado" que dispara cuando el ángulo entre la cámara y el personaje supera un umbral y queda en idle.
6. El esqueleto es el estándar de Mixamo: sirve para **retargeting** barato (mapa de nombres a un esqueleto humanoide común) y así reutilizar el pack con otros personajes.

## 2. Estado actual del sistema de animación de GB

Lo que hay (`Model`, `ModelInstance`, `AnimationClip`):
- Importación glTF (Blender), clips por canales (traslación/rotación/escala; step/linear/cubic), un clip activo con fundido (cross-fade) hacia otro, velocidad, pausa, tiempo, palettes de joints, cámara glTF animada.
- Skinning en GPU con la paleta de huesos.

Lo que **no hay** y el pack necesita:

| Carencia | Impacto con este pack |
|---|---|
| **Importador FBX** (solo glTF) | Mixamo exporta FBX; hoy habría que pasar por Blender a mano. |
| Morph targets / blendshapes (`Model.cpp` avisa "morph weights not supported") | Expresiones, correcciones de músculo, deformaciones por clip. |
| Solo 1 clip + fundido | No hay mezcla 1D/2D (walk↔run, strafes), ni capas, ni máscaras. |
| Sin máquina de estados / parámetros | No hay "idle → walk → run → jump" guiado por variables (velocidad, en el suelo, giro). |
| Sin root motion | Imposible conectar la animación con el controlador del personaje. |
| Sin sincronización de fases ni eventos | Patinaje al mezclar; pasos sin sonido. |
| Sin retargeting | Cada personaje necesitaría sus propios clips. |
| Sin editor de animación | No se pueden crear/editar clips, curvas, claves ni blendshapes. |

## 3. Propuesta (por capas, cada una con tests headless y golden data)

**A. Datos de animación (motor, sin GL)**
- `AnimationClip` propio e independiente del formato: curvas por hueso con interpolación (step, lineal, Bézier/Hermite, slerp), eventos con nombre y tiempo, metadatos (bucle, velocidad de referencia, root bone).
- Importador **FBX** (ufbx, una sola cabecera .c/.h, MIT/dominio público, versión fijada) → mismo `Model`/`AnimationClip` que glTF; conversión de unidades (cm→m) y ejes. Opcional: exportador/guardado propio de clips (JSON) para editar.
- **Morph targets** (glTF y FBX BlendShape): pesos por malla y curvas de peso en el clip; subida a GPU por buffer de deltas.

**B. Runtime de personaje**
- `Pose` (TRS por hueso) con operaciones: mezcla lineal con slerp, aditiva, máscara por huesos.
- **Blend trees**: 1D (velocidad: idle–walk–run), 2D (dirección × velocidad con los strafes), con sincronización de fase por marcadores de apoyo.
- **Máquina de estados** con parámetros (float/bool/trigger), transiciones con condiciones y duración, capas con máscara (p. ej. torso/brazos).
- **Root motion**: extraer el delta del hueso raíz por frame (translación y yaw), con modo "animación manda" o "controlador manda + escalado de velocidad de clip".
- **Eventos**: callbacks por clip (apoyo izquierdo/derecho → `AudioEngine` con la superficie del suelo; despegue/aterrizaje → física).
- **Retargeting** humanoide por mapa de huesos (Mixamo, Blender Rigify…).

**C. Locomoción en tercera persona**
- `CharacterController` (cápsula, suelo, pendientes, escalones, salto/caída) usando `Physics` existente, con la velocidad deseada desde teclado/mando.
- Cámara de seguimiento (la `OrbitController` actual como base): colisión con el entorno, objetivo suavizado, bloqueo.
- Parámetros del animator conectados al controlador: velocidad, dirección relativa a la cámara, en el aire, ángulo de giro.

**D. Editor de animación (ImGui, sobre el editor actual)**
- Panel **Animator**: grafo de estados/blend trees editable y guardado en JSON (agentes y diff-eables).
- Panel **Timeline/Curvas**: claves por hueso y por morph, interpolación editable, eventos, previsualización con scrubbing, onion skin.
- Edición de poses (gizmos de rotación sobre huesos) y grabación de claves; comandos de texto equivalentes (`anim play`, `anim set param`, `anim key`…), igual que el resto del editor.
- QA: golden data por clip (duración, velocidad de root, apoyos detectados), pruebas de continuidad de mezcla (sin saltos de pose), capturas por script.

## 4. Orden sugerido de entrega

1. **FBX + datos de clip + análisis automático del pack** (velocidad de referencia, apoyos de pie, bucle): valida el pack con tests.
2. **Pose/mezcla + blend tree 1D/2D + sincronización + root motion** (headless, golden data).
3. **Máquina de estados + parámetros + eventos → audio de pasos**.
4. **Controlador de personaje + cámara de seguimiento + demo "personaje Mixamo caminando por el mundo procedural"**.
5. **Morph targets**.
6. **Editor: Animator (grafo) y Timeline/curvas**, retargeting.

## 5. Verificación posterior (fase 11A-1)

El importador FBX de la fase 11A-1 y `AnalyzeClip` confirmaron las cifras de la cadera y midieron los pies: caminar y correr tienen un apoyo por pie y ciclo (ciclo de 1,72 m y 3,24 m), el salto tiene ≈21 % de fase aérea (36–57 % del clip), los strafes corriendo tienen fase aérea (20–77 %) y `right strafe` (corriendo) no cierra bien el bucle (≈18° por hueso) ni coincide en velocidad con `left strafe` (4,32 frente a 4,52 m/s), así que conviene tratarlo con tolerancia o recortarlo. Los giros de 90° giran 90° la cadera y no son bucles.

## 6. Decisiones tomadas

- FBX nativo con ufbx (dependencia fijada), en lugar de convertir a glTF fuera del motor.
- **El controlador manda** el movimiento; las animaciones se reproducen a `velocidad real / velocidad de referencia` y se mezclan con sincronización de pies (no root motion por defecto).

La arquitectura completa (controlador de personaje, capas y slots, sockets, IK, ragdoll, interacciones) y el plan por fases están en `docs/ANIMACION_SISTEMA_COMPLETO.md`.
