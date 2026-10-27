# Sistema de animación completo para GB Engine: investigación y arquitectura

Fase de investigación. Parte de lo medido en los dos packs de Mixamo (locomoción y granja), de lo que ya hay en el
motor y de cómo resuelven el problema los motores de referencia. Termina en una arquitectura por capas, un plan de
entregas y las decisiones que hay que tomar.

## 1. Qué tenemos ya

| Pieza | Estado |
|---|---|
| Importador FBX (ufbx) y glTF, clips horneados a 30 fps, `AppendAnimations` por nombre de hueso | hecho (11A-1) |
| `ClipAnalysis`: velocidad de raíz, giro, bucle, apoyos de pie, fase de vuelo | hecho |
| `Pose`, mezcla, máscaras por hueso, `ModelInstance::SetPose` | hecho (11A-2, PR #26) |
| `BlendSpace` 1D/2D + `LocomotionBlender` (fase por pie, escalado de velocidad, sin root motion, eventos de apoyo) | hecho |
| `FootstepSounds` por superficie, audio 3D | hecho |
| Entidades + componentes, JSON, editor ImGui con comandos | hecho (10B) |
| Física de cuerpos rígidos (esfera/caja/cápsula, impulsos secuenciales, islas dormidas, destrucción) | hecho (sin articulaciones) |

Lo que falta para un sistema "completo" (y que el resto de este documento diseña): controlador de personaje separado del
animador, máquina de estados, capas y slots, sockets y objetos, IK y animación procedural, adaptación al terreno,
ragdoll, interacciones, editor y automatización de personajes.

## 2. Lo que dicen los dos packs (medido)

Todos los clips (locomoción y granja) usan **el mismo esqueleto de 65 huesos** (`mixamorig:*`, sin hueso raíz propio ni
huesos IK; el movimiento va en `Hips`). Ningún hueso quedó sin emparejar al añadir los 25 clips de la granja al X Bot.
Por tanto las capas de todos los packs **se pueden combinar sin retargeting**; el retargeting hace falta solo para otros
esqueletos.

### 2.1 Familias de pose: cada clip empieza y acaba en la misma pose
Todos los clips de granja terminan donde empezaron (diferencia primer/último fotograma ≤ 3° de media por hueso), y se
agrupan por la pose de partida:

| Familia (pose base) | Clips | Altura de cadera | Distancia entre las poses base |
|---|---|---|---|
| **De pie neutra** | pick fruit (×3), plant tree, pull plant, watering | 1,03 m | 12–22° entre sí |
| **Arrodillado** | kneeling idle, dig and plant seeds, plant a plant, pull plant (2) | 0,53 m | 0–2° entre sí |
| **Sentado** (ordeño) | cow milking | 0,39 m | 25–35° del resto |
| **Llevar caja** | box idle, box turn (×2), box walk arc | 1,00 m | 0–4° entre sí |
| **Sostener** (objeto contra el pecho) | holding idle, turn left, turn right, walk | 1,02 m | 10–17° entre sí |
| **Carretilla** | wheelbarrow idle, dump, walk (×2), walk turn (×2) | 1,0 m | 13–16° entre sí |

Consecuencias de diseño:
- Cada familia es una **postura/overlay** (como un "stance" o "linked anim layer" de Unreal): dentro de ella hay su propio idle,
  caminar, giros y acciones. La máquina de estados no es plana: es **postura × movimiento × acción**.
- Entre familias **no hay clips de transición** (de pie ↔ arrodillado ↔ sentado). Salvo los que lo incluyen a mitad de clip
  (pick fruit 3, plant tree, pull plant: la cadera baja de 1,03 a 0,48–0,77 m y vuelve). Las transiciones entre posturas
  tendrán que ser mezclas cortas con ayuda procedural (IK de pelvis y rodillas), o montajes dedicados.
- Los clips cerrados permiten usarlos como **montajes de un solo disparo** con entrada/salida por fundido (la pose de
  partida es la de reposo de su familia).

### 2.2 Caminar con objetos: la mano agarra algo rígido
Medido sobre la posición de las manos en el espacio del personaje (media ± dispersión):

| Clip | Mano izq. | Mano der. | Separación entre manos |
|---|---|---|---|
| box idle / walk arc | (+0,27, 1,20, +0,33) ±0,01–0,06 | (−0,27, 0,93, +0,12) ±0,01–0,05 | **0,65 m constante** |
| holding idle / walk | (+0,30, 0,85, −0,03) ±0,04–0,07 | (−0,26, 0,92, −0,03) ±0,08–0,12 | 0,54–0,68 m |
| wheelbarrow idle / walk | (+0,27, 0,85, −0,03) ±0,00–0,03 | (−0,28, 0,85, −0,03) ±0,02–0,03 | **0,51–0,55 m constante** |
| wheelbarrow dump | (+0,26, 1,11, +0,31) ±0,38 | (−0,24, 1,11, +0,32) ±0,38 | 0,34–0,59 m (vuelca) |

La caja y la carretilla son **cuerpos rígidos que acompañan a las manos** (separación constante a 1–5 cm). Conclusiones:
- Un objeto puede ir pegado a **un socket** del cuerpo (mano o "punto de transporte") sin IK extra mientras el clip sea el
  autorizado para ese objeto; con otro personaje o clip distinto hace falta **IK de manos** a los puntos de agarre del objeto.
- Las 2 manos del objeto definen un **eje de agarre**: guardar en el objeto sus dos puntos de agarre (`GripPoints`) y
  resolver con IK de dos huesos el ajuste fino (otra altura de personaje, otra pendiente).
- En acciones (watering, pick fruit, plant tree) las manos se mueven ±0,3 m: el objeto va a una sola mano (regadera, azada,
  semillas) con un offset local; la otra mano usa IK o el clip.

### 2.3 Acciones en el sitio y adaptación al entorno
- Los clips de estación (plantar, cavar, ordeñar, arrancar, recoger fruta) **no mueven la raíz** (0,00 m): el personaje debe estar
  en el punto exacto de la interacción. Hace falta **motion warping** (alinear posición y orientación del personaje al punto de la
  interacción, repartiendo el ajuste durante una fase del clip) y **puntos de interacción** declarados en los objetos del mundo
  (bancal, árbol, vaca, regadera, carretilla) que usan por igual el jugador y los NPC.
- Postura arrodillada/sentada a 0,53/0,39 m: sobre un terreno inclinado o irregular necesita **IK de rodillas, manos y pelvis**
  contra el suelo.
- Giros: los clips de giro en sitio giran 77°–173° (giros de 90° del pack anterior, U-turns aquí) y no son bucles: el giro
  del clip debe **aplicarse a la orientación del personaje** (root motion de rotación) o corregirse con un offset de raíz
  (como el "root yaw offset" de los sistemas modernos).
- Caminar con objeto y girando (wheelbarrow walk turn, box walk arc): clips curvos de 27°–29°: sirven de **variantes direccionales** del blend space.

## 3. Cómo lo resuelven los motores de referencia

(Las fuentes oficiales y de comunidad consultadas están enlazadas al final. Donde no hay fuente oficial se dice.)

### 3.1 Unreal Engine
- **AnimInstance**: la instancia de animación del `SkeletalMeshComponent` ejecuta un **Anim Graph** (máquinas de estados, blend
  spaces, nodos de capa, IK) y recibe variables del juego. Las **Linked Anim Layers** permiten cambiar entre familias de clips
  en caliente (p. ej. por arma o postura) sin cambiar el grafo principal.
- **Slots y montajes**: un montaje (una secuencia con secciones y notificaciones) se inserta en el grafo en un *slot*; el grafo
  decide dónde cae su resultado, y solo hay un asset por slot a la vez ([Epic: capas de animación](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-layered-animations-in-unreal-engine), [API AnimInstance](https://docs.unrealengine.com/4.27/API/Runtime/Engine/Animation/UAnimInstance/index.html)).
- **Layered Blend Per Bone**: mezcla por rama de huesos desde un hueso raíz (p. ej. `spine_01`): tronco/brazos de un montaje sobre las
  piernas de locomoción ([ejemplo en la comunidad](https://medium.com/software-dev-explore/ue5-blending-animation-montage-933f00adaa1e)). Fallo típico: dos slots que se pisan sobre los mismos huesos, así que las máscaras deben ser disjuntas.
- **Locomoción separada del control** (por conocimiento general del motor, sin fuente concreta en la búsqueda): el
  `CharacterMovementComponent` mueve a la cápsula; el controlador (jugador o IA) solo le da entradas; el animador lee
  velocidad/aceleración/en suelo del movimiento. Es el patrón que se pide: la capa de animación no sabe quién manda.
- Control Rig / IK Rig, Motion Warping, Physical Animation y Motion Matching (UE5) son los módulos para procedural, alineado,
  ragdoll parcial y selección de poses por base de datos.

### 3.2 Rockstar (RAGE + Euphoria)
Lo comprobable públicamente: Euphoria (NaturalMotion) simula huesos, equilibrio y fuerzas musculares para dar reacciones
dinámicas encima de la animación, y Rockstar dijo haberlo "evolucionado" para Red Dead Redemption 2 ([GamingBolt](https://gamingbolt.com/red-dead-redemption-2s-euphoria-engine-is-radically-overhauled-says-rockstar/amp), [WCCFTech](https://wccftech.com/rockstar-euphoria-evolved-rdr2/amp/)). No hay documentación primaria de Rockstar accesible sobre su IK de pies ni su
sistema de capas; lo que se sabe de la técnica es genérico (animación + física activa mezclada). **No copiamos nada: usamos
las ideas públicas conocidas** (ragdoll activo con motores PD, reacciones a impactos, IK al entorno) sobre nuestra física.

## 4. Arquitectura propuesta

Principio rector: **cuatro capas independientes** que se hablan por datos simples. Cualquier personaje (jugador, NPC, animal)
usa las mismas.

```
 Controlador (Player: teclado/mando · AI: ruta/comportamiento · Script · Red)
        │  MoveInput { velocidad deseada, orientación deseada, salto, acción }
        ▼
 CharacterMotor  (cápsula, suelo, pendientes, escalones, salto/caída, agua; mueve la entidad)
        │  MotionState { velocidad, aceleración, en suelo, normal del suelo, giro, postura, velocidad vertical }
        ▼
 AnimInstance  (por personaje)
   ├─ Parámetros (float/bool/trigger/enum) ← MotionState + gameplay
   ├─ Estado: Postura (de pie, agachado, arrodillado, sentado) × Overlay (nada, caja, sostener, carretilla, herramienta)
   ├─ Capa base: LocomotionBlender por conjunto de clips (Locomotion Set)  ← ya existe
   ├─ Capas de montaje por slot y máscara (FullBody, UpperBody, Hands...) ← acciones (plantar, ordeñar, regar)
   ├─ Capas aditivas y por hueso (respiración, inclinación, daño)
   ├─ Eventos/notificaciones → audio, atar/soltar objetos, partículas, gameplay
   └─ Salida: Pose
        ▼
 Procedural (Control Rig): IK de pies al terreno, IK de manos a puntos de agarre, mirada, inclinación, rotación de cadera,
 stride/orientation warping, huesos secundarios
        ▼
 Física (Ragdoll/Physical Animation): mezcla pose ↔ simulación por hueso (parcial o total)
        ▼
 ModelInstance::SetPose → matrices, paletas → Renderer (skinning) ; sockets para objetos
```

### 4.1 Separación Controlador / Motor / Animador (el "como Unreal")
- `CharacterMotor` es un componente de la entidad que consume `MoveInput` y produce `MotionState`. No sabe nada de teclado ni IA.
- `PlayerController` (entrada → `MoveInput` relativo a la cámara) y `AIController` (siguiente punto de ruta → `MoveInput`) implementan
  la misma interfaz; también un `ScriptedController` para cinemáticas y QA por agentes.
- El `AnimInstance` solo lee `MotionState` y parámetros: cambiar de jugador a NPC no cambia ni una línea de animación.

### 4.2 Locomotion Sets y posturas (aprovecha las "familias de pose" medidas)
- Un **LocomotionSet** es un conjunto de clips (idle, andar, correr, strafes, giros, salto) con su `BlendSpace`. El pack de
  locomoción es el set "Libre"; "Caja", "Sostener" y "Carretilla" son sets del pack de granja (idle + walk + turns, y la
  carretilla además con `dump`).
- Cambiar de set = **overlay**: fundido corto entre sets (misma fase del ciclo, ya compartida por la fase por pie).
- **Postura** (de pie, agachado, arrodillado, sentado) como eje aparte con transiciones procedurales entre ellas.

### 4.3 Montajes, slots y capas
- `Montage`: clip(s) con secciones (entrada, bucle, salida), puntos de fundido, notificaciones (`attach`, `detach`, `spawn`,
  `sound`, `footstep`, `enable_ik`...) y opciones: bloquear movimiento, root motion, motion warping, máscara de huesos.
- **Slots** (`FullBody`, `UpperBody`, `Hands`...) con prioridad; cada uno aplica su máscara; las máscaras de slots activos a la
  vez no deben solaparse (aviso en el editor).
- Mezcla por hueso con **perfil de mezcla** (peso por hueso y tiempo de entrada/salida) y capas aditivas.

### 4.4 Sockets y objetos
- `Socket { nombre, hueso, offset T/R/S }` definidos sobre el esqueleto (no sobre el modelo): valen para todos los personajes
  con ese esqueleto. Sockets estándar generados automáticamente: `hand_r`, `hand_l`, `head`, `back`, `hip_l`, `hip_r`, `carry`,
  `foot_l`, `foot_r`.
- Las entidades se **atan** a un socket: `Entity.Attach { entidad dueña, socket, offset, reglas (conservar mundo / encajar) }`.
  Se ordenan después de la animación para que el objeto no vaya un fotograma retrasado.
- Objetos con **puntos de agarre** (`GripPoint`s: una mano o dos) y poses de dedos; el IK de manos los alcanza. Caja y
  carretilla definen dos agarres; regadera y azada uno.
- Inventario/equipo (más adelante) solo cambia qué entidad hay en cada socket.

### 4.5 Animación procedural (Control Rig propio)
Un pequeño grafo lineal de **modificadores** sobre la pose, cada uno con peso y máscara:
- **IK de dos huesos** (brazos, piernas), **FABRIK/CCD** (columna, cola), **look-at** con límites y reparto cabeza/cuello/tronco.
- **IK de pies al terreno**: trazado de rayos contra terreno/colisiones por pie, desplazamiento de pelvis, orientación del pie a la
  normal, bloqueo del apoyo (sin patinaje). La fase de apoyo ya la dan los eventos del `LocomotionBlender`.
- **Alineación de orientación y zancada** (stride/orientation warping) para strafes y giros con velocidad variable.
- **Inclinación** por aceleración y giro, **respiración/idle procedural**, huesos secundarios (muelles) para pelo, ropa, herramientas.
- **Rodillas y manos al suelo** en posturas arrodilladas/sentadas (ordeño, plantar) sobre terreno irregular.

### 4.6 Física: ragdoll y animación física
- **Articulaciones** en `Physics`: bola con cono de límite y bisagra (hoy no existen). Es el cambio de motor más grande.
- **Ragdoll automático** desde el esqueleto: cápsulas por hueso (largo = distancia al hijo, radio por plantilla humanoide),
  límites de articulación por plantilla; editable.
- **Mezcla animación ↔ física por hueso** (parcial: sólo las piernas tras un golpe; total: muerte), seguimiento de la pose con motores PD
  ("physical animation" / ragdoll activo) y reacciones a impactos con levantarse/recuperar.
- Colisiones multi-capa: capas de colisión (`Pawn`, `Ragdoll`, `Prop`, `Terrain`, `Trigger`) y consultas por capa para IK, cámara y
  alcance de interacciones.

### 4.7 Interacciones y motion warping
- `Interactable` en las entidades del mundo: lista de **puntos de interacción** (posición, orientación, tipo de postura,
  montaje que ejecutar, objeto requerido, objeto que deja).
- Jugador y NPC usan **el mismo flujo**: ir al punto (controlador) → alinear con *motion warping* → reproducir el montaje →
  eventos de la animación (atar fruta, soltar semilla) → fin.
- Los NPC reciben sus tareas por un sistema aparte (lista de tareas / comportamiento), que solo emite `MoveInput` e
  "interactuar con X".

### 4.8 Editor y automatización
- Paneles: **Animator** (grafo de estados y blend spaces), **Montage** (línea de tiempo con secciones y notificaciones),
  **Sockets** (previsualizar objetos), **IK/Rig** (cadenas, objetivos, límites), **Ragdoll** (cuerpos y articulaciones),
  **Retarget** (mapa de huesos).
- Todo se guarda como **JSON** (agentes y diffs); comandos de texto equivalentes; capturas y datos dorados por clip/estado.
- **Alta automática de un personaje** (`character import`): importa el FBX/glTF, detecta humanoide, genera mapa de huesos, sockets,
  ragdoll y cadenas IK, y comprueba compatibilidad con los sets de animación (huesos que faltan, escala, orientación).

### 4.9 Motion Matching (opcional, fase final)
Con la medición ya hecha (velocidades, apoyos, trayectoria) se puede construir una **base de datos de poses** con
rasgos (velocidad, trayectoria futura, posición de pies) y elegir el fotograma que mejor encaje. Se deja para cuando haya
suficiente material de animación continuo; los blend spaces + sincronización por pie ya cubren la locomoción de un primer juego.

## 5. Plan de entregas

| Fase | Contenido | Pruebas |
|---|---|---|
| **11A-2** (PR #26) | Pose, blend space, locomoción, eventos de pasos | hecho |
| **11A-3** | `CharacterMotor` + controladores Player/AI/Script + `MotionState`; cámara de seguimiento con colisión | cápsula contra terreno y cajas, pendientes, escalones; mismas entradas = mismo movimiento |
| **11B-1** | `AnimInstance`: parámetros, posturas × overlays, `LocomotionSet` (libre, caja, sostener, carretilla), transiciones | golden data de transiciones; continuidad de pose |
| **11B-2** | Montajes, slots, máscaras por hueso, notificaciones | montaje de plantar/regar sobre locomoción |
| **11C-1** | Sockets y `Attach` + objetos con puntos de agarre (caja, carretilla, regadera, azada) | objeto sigue la mano; IK de manos a agarres |
| **11C-2** | IK de dos huesos/FABRIK/look-at, IK de pies al terreno, pelvis, manos/rodillas al suelo | pies sobre terreno irregular sin patinaje |
| **11D-1** | Interacciones + motion warping + tareas de NPC | jugador y NPC hacen la misma tarea |
| **11D-2** | Articulaciones físicas, ragdoll automático, mezcla pose ↔ física, reacciones | ragdoll estable; caída y recuperación |
| **11E** | Editor de animación (Animator, Montage, Sockets, IK, Ragdoll), importador automático de personajes, retargeting | tests por script, capturas |
| **11F** | Motion matching (opcional) y ajustes de calidad | comparación con blend spaces |

Cada fase mantiene lo ya acordado: todo ejecutable sin ventana, datos de prueba en texto, golden data y capturas, sin binarios
en el repositorio (los packs de Mixamo se usan con `GB_MIXAMO_DIR` y no se versionan).

## 6. Riesgos y decisiones abiertas

1. **Licencia/redistribución**: los clips de Mixamo se pueden usar en proyectos propios, pero no conviene versionarlos en el repositorio.
   Los tests usan rigs generados; los packs reales solo en local o con una variable de entorno.
2. **Esqueleto sin raíz ni huesos IK**: hace falta crear **huesos virtuales** (raíz, IK de pies y manos) en el motor, no en el FBX.
3. **Articulaciones físicas**: es el mayor trabajo de motor (solver con restricciones angulares, estabilidad del ragdoll).
4. **Transiciones entre posturas sin clips**: de pie → arrodillado → sentado se resuelve con IK/mezcla; si no queda bien, se
   capturan o se animan clips de transición.
5. **Orden de ejecución con física y entidades**: animación → IK → física → sockets → render, con un orden fijo y comprobable.
