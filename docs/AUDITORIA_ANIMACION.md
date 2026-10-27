# Auditoría del sistema de animación y plan para los packs nuevos

Estado a fecha de la fase 11E-3 (PR #40) con `master` en #41 (huerto y habilidades). Todo lo marcado «comprobado» se ha ejecutado en esta
sesión (editor headless, `character_check`, `retarget`, capturas); el resto se ha leído en el código y en los documentos.

## 1. Qué hay y funciona (todo con test CPU, sin ventana)

| Capa | Piezas | Test |
|---|---|---|
| Datos | `Pose`, `SampleClip`, `BlendPoses(Masked)`, FBX con ufbx, glTF | `animation`, `gltf`, `fbx` |
| Locomoción | `BlendSpace`, `Locomotion`, giro en sitio, aire (salto/caída), sets (libre, caja, sostener, carretilla) | `locomotion`, `air_layer`, `turn_layer` |
| Montajes | slots, notificaciones, secciones, seek/pausa, JSON, panel Timeline | `montage` |
| Grafo | estados, transiciones con condiciones, triggers, JSON, panel Graph | `anim_graph` |
| Procedural | `FootIK`, `LookAt`, `SolveTwoBoneIK`, agarres de props | `procedural`, `props` |
| Interacción | puntos, `MotionWarp`, `InteractionRunner`, `TaskRunner` y su lenguaje de texto | `interaction` |
| Física | articulaciones, `Ragdoll` de 13 cuerpos en el `PhysicsWorld` del mundo | `physics_joint`, `ragdoll` |
| Datos de personaje | `AnalyzeCharacter` (convención, huesos, roles, avisos) y JSON | `character_profile` |
| Retargeting | mapa de huesos, rotaciones en mundo, escala de cadera, giro de 180°, horneado a 30 fps | `retarget` |
| Editor | paneles Animation/Timeline/Graph, comandos `player_*`, `montage_*`, `graph_*`, `retarget`, `character_check` | `editor` |

## 2. Hallazgos nuevos (comprobados con los packs recibidos)

### 2.1 Universal Animation Library (Quaternius, CC0, 43 clips, esqueleto estilo Unreal de 65 huesos)

`retarget` sobre el X Bot de Mixamo **funciona**: 43 clips, escala 1,13, giro de 180° detectado, la captura del ataque de espada se ve
coherente. Pero salen estos defectos:

1. **Solo se emparejan 21 huesos.** `spine_01..03` no se asocian con `Spine/Spine1/Spine2`, ni los dedos ni los dedos del pie: el torso
   pierde la torsión intermedia y las manos se quedan rígidas (46 huesos del destino conservan el reposo). Arreglo: cadenas por
   *posición relativa* (n huesos de columna ↔ m huesos, interpolando) y emparejado de dedos por nombre normalizado (`index_01_l` ↔ `RightHandIndex1`…).
2. **Los roles de clip se eligen mal.** `character_check` da `idle="Crouch_Idle_Loop"`, `walk="Walk_Formal_Loop"`, `jump="Jump_Land"`,
   y no encuentra `run` (hay `Jog_Fwd_Loop` y `Sprint_Loop`). Causas: `FindClip` toma la primera coincidencia por subcadena, no conoce
   `jog/sprint/loop`, y los nombres llegan con el prefijo `Armature|`. Arreglo: quitar prefijos `X|`, puntuar candidatos (exacto > sin
   `crouch/sit/formal/torch/...` > más corto) y un diccionario de sinónimos por rol (idle, walk, run=`jog|run|sprint`, jump=`jump_start|jump_loop`, land=`jump_land`).
3. **Ficheros.** Hay GLB y FBX, con y sin root motion (`_RM`). El FBX pesa 23 MB y el GLB 7,6 MB: por CLAUDE.md **no se versionan**
   (quedan en una carpeta local, p. ej. `GB_UAL_DIR`, como Mixamo). Conviene usar el GLB sin `_RM` (el motor ya gestiona el movimiento
   con `MotionWarp`/controlador) y comprobar que el retargeter lee clips de glTF (hoy `retarget` solo se ha probado con FBX).
4. **Orientación:** mira a −Z y su izquierda está en −X; el retargeter ya lo corrige, `character_check` lo avisa (bien).

Qué clips cubren huecos de `ANIMACIONES_PENDIENTES.md`: `PickUp_Table` y `Interact` (recoger/dejar la caja), `Crouch_Fwd_Loop`/`Crouch_Idle_Loop`/`Fixing_Kneeling`
(agacharse, sembrar a ras de suelo), `Push_Loop` (carretilla), `Roll`, `Hit_Chest/Hit_Head/Death01` (reacciones y muerte), `Sitting_*`,
`Swim_*` (agua profunda), `Driving_Loop`, `Spell_Simple_*` (habilidades tipo hechizo del sistema GAS), `Sword_*`, `Punch_*`, `Pistol_*`.
No traen «levantarse del ragdoll».

### 2.2 Robots (Arachnodroid, ReconBot, Companion-bot, MobileStorageBot; MagicaVoxel, OBJ+MTL+paleta PNG 256×1)

**No vienen «por piezas»**: cada uno es **una sola malla** (un `o` vacío, 2.800–5.200 caras) con la textura como paleta de colores,
y el `.vox` es un único modelo de vóxeles sin capas ni nombres. Lo que sí hay, medido por conectividad de vóxeles:

| Robot | Vóxeles | Componentes sueltos | Lectura |
|---|---|---|---|
| Arachnodroid | 4.007 | cuerpo (3.335) + 4 patas de 168 | 4 patas ya separadas |
| MobileStorageBot | 17.179 | cuerpo (16.055) + 4 ruedas/patas de ~275 + 3 piezas de 7 | 4 ruedas o patas ya separadas |
| ReconBot | 10.375 | cuerpo (10.307) + 2 pies de 31 + vóxeles sueltos | solo los pies; brazos y cabeza van pegados al cuerpo |
| Companion-bot | 2.703 | cuerpo (2.651) + 2 pies de 26 | solo los pies |

Consecuencias: separar patas/ruedas/pies es gratis (componentes conectados), pero **brazos, cabeza, torreta, tapas** hay que cortarlos
con regiones definidas por el usuario (cajas o planos en el espacio del vóxel). Además faltan: un cargador de OBJ/MTL (o de `.vox`) en el
motor, y una forma de describir un esqueleto de piezas rígidas.

### 2.3 Otros puntos del sistema (leídos en el código/documentos)

- **Sandbox**: huellas, polvo y salpicaduras aún no usan `Plants()` y no se ha decidido AnimInstance vs `CharacterAnimation` en todos los sitios
  (decisión documentada: AnimInstance para FBX, `CharacterAnimation` solo como respaldo glTF).
- **Retargeting**: solo rotaciones y traslación de cadera; sin corrección de zancada ni de deslizamiento de pies; mapa de huesos no editable.
- **Pies**: no hay bloqueo del apoyo (patinaje) ni manos/rodillas al suelo en posturas arrodilladas o sentadas.
- **Ragdoll**: solo completo (no parcial); sin motores (ragdoll activo); sin animación de levantarse; miembros como cajas.
- **Interacción**: reservas de puntos (dos personajes en el mismo punto) y conducta más allá de listas lineales.
- **Glb/morph targets**: sin morph targets (expresiones, respiración).
- **Grafo**: sin sub-grafos ni blend trees dentro de un estado (la locomoción sigue siendo un `BlendSpace` aparte); el panel no admite deshacer; el parámetro `Set` se maneja a mano.
- **Montajes**: la lista de `MixamoMontages()` está en código; debería leerse de un JSON por personaje/pack (ya existe `MontagesToJson`).
- **Datos dorados**: hay tests de lógica y capturas puntuales, pero no una captura/curva dorada por clip retargeteado (riesgo de regresión en el retargeting).
- **Sin motion matching** (11F, opcional).

## 2.4 Hecho tras la auditoría (esta rama)

- Los nombres de toma ya no llevan el ámbito del exportador (`Armature|Walk_Loop` → `Walk_Loop`).
- Los roles de clip puntúan: gana el nombre más corto que no sea una variante (agachado, sentado, formal, antorcha, hablando, pistola…); `run` acepta `jog` y `sprint`; `jump` no es el aterrizaje. Con UAL sale `idle=Idle_Loop walk=Walk_Loop run=Jog_Fwd_Loop jump=Jump_Loop`.
- `BuildRetargetMap` empareja los huesos intermedios de la columna en proporción y los **dedos** por lado, dedo y articulación: UAL → Mixamo pasa de 21 a **62 huesos emparejados**; solo quedan sin pareja los nodos de malla y los extremos (`HeadTop_End`, `Toe_End`).
- `UniversalMontages()`: interact, pick_up, fix_kneeling, roll, hit_chest/head, death, sit_enter/exit, sword_attack, punch_cross/jab, pistol_shoot/reload, spell_enter/shoot/exit, dance; se añaden al personaje cuando sus clips están (tras `retarget`, 11 → 29 montajes). Las notificaciones (momento del golpe, del disparo) se ponen en el panel Timeline.
- Pendiente de la parte A: GLB como fuente de `retarget`, mapa de huesos editable y `GB_UAL_DIR` con test saltable; de la B: catálogo de montajes leído de JSON y asignarlos a `AbilityDef::Montages` (recoger, agacharse, carretilla) — requiere decidir cuál sustituye a cuál en el huerto.

## 3. Plan recomendado (por orden de valor/coste)

**A. Arreglos baratos del retargeting y del análisis (sesión corta)**
1. Normalizar nombres de clip (`Armature|X` → `X`) y roles por puntuación + sinónimos (idle/walk/run/jump/land/crouch/sit/swim/death).
2. Emparejar columna y dedos en `BuildRetargetMap` (columna por reparto proporcional, dedos por nombre normalizado, dedo del pie).
3. `retarget` con GLB; variable de entorno `GB_UAL_DIR` y test saltable como el de Mixamo.
4. Mapa de huesos editable por JSON (`retarget_map <file>`) y `retarget_check` que liste huesos sin pareja (ya se imprime) y los exporte.

**B. Biblioteca UAL como fuente de acciones**: añadir sus clips a `MixamoMontages()`→ renombrar a un catálogo por pack (`MontageCatalog` leído de JSON), cubrir `ANIMACIONES_PENDIENTES.md` (recoger, agacharse, carretilla, rodar, golpes, muerte, nadar, sentarse) y un set de locomoción agachado (`Crouch_*`).

**C. Robots por piezas (`PartModel`)** — nuevo módulo, sin tocar el render:
1. `ModelObj`/`ModelVox`: cargador de OBJ+MTL (+PNG) o `.vox` a `Model`.
2. `PartSplit`: divide la malla en piezas por (a) componentes conectados y (b) regiones nombradas por JSON (`{"name":"head","box":[x0,y0,z0,x1,y1,z1]}`); cada pieza es un hueso rígido con pivote (por defecto el centro de contacto con su padre).
3. Esqueleto de piezas jerárquico + `SkinWeights` 100 % a un hueso → reutiliza `Pose`, `Model::AddClip`, `ModelInstance` y las capas de montaje/grafo tal cual.
4. Animación procedural por tipo (JSON `RobotRig`): `Legged` (ciclo de marcha con `SolveTwoBoneIK` y `FootIK` para 4 patas de Arachnodroid), `Wheeled` (giro de ruedas según velocidad, inclinación en frenada — MobileStorageBot), `Biped` (pies con balanceo + cuerpo que flota/rebota — ReconBot, Companion-bot), más «idle» (respiración, antena), «scan», «alert».
5. Comandos `parts_split`, `parts_list`, `parts_pivot`, `robot_rig`, `robot_play` y captura headless por robot.

**D. Animaciones de otro esqueleto en robots**: no tiene sentido retargetear humanoides a robots de patas; usar clips procedurales (C.4) y reservar el retargeting para humanoides (UAL, Mixamo, Unreal/Unity).

**E. Después**: bloqueo de pies, ragdoll parcial/activo, animación de levantarse (UAL no la trae: pedir clip o hacerla con un montaje de transición), sub-grafos, 11F.

## 4. Qué necesito de ti para seguir
- Si quieres que las piezas sean cortes **automáticos** (componentes) o también **regiones a mano** (cabeza, brazos); para cada robot, qué movimiento esperas (patrulla, seguir, cargar cajas…).
- Dónde guardar los packs en tu máquina (`GB_UAL_DIR`, `GB_ROBOTS_DIR`); no se versionan.
- Confirmar que el orden A → B → C te vale.
