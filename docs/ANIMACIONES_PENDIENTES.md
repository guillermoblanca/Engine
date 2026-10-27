# Animaciones pendientes

Acciones del huerto ([habilidades.md](habilidades.md)) para las que los packs Mixamo que tenemos (locomoción, granja, heridos) no traen
un clip propio. No se han inventado: cada habilidad usa el montaje más cercano (o ninguno) y aquí queda anotado qué falta y para qué.
Cuando llegue el clip basta con añadirlo a `MixamoMontages()` (`Engine/src/GB/Animation/Montage.cpp`) y poner su nombre en
`AbilityDef::Montages` (`Engine/src/GB/Gameplay/Garden.cpp`); el resto no cambia.

| Acción | Habilidad que la usa | Qué se hace ahora | Para qué se necesita |
|---|---|---|---|
| **Recoger del suelo** (agacharse, coger la caja, levantarse con ella) | `CarryBox` | Ningún montaje: el set de locomoción pasa a `box` y la caja se engancha a las manos con IK en cuanto la habilidad se activa | Coger cajas (y cualquier objeto) del suelo sin que aparezcan ya en los brazos |
| **Dejar la caja** (agacharse y soltarla) | `DropBox` | Ningún montaje: la caja se suelta y se vuelve al set `free` con un fundido | Dejar la caja en el suelo o en la carretilla |
| **Cavar en bucle** (varias paladas seguidas) | `Dig` | Un solo `dig and plant seeds` (un golpe de pala) por activación | Cavar una parcela entera con varias paladas y un final, en vez de una acción corta |
| **Agacharse / ponerse en cuclillas** | `Sow`, `PlantTree` | `plant a plant` y `plant tree` tal como vienen | Sembrar a ras de suelo y plantar el esbozo de árbol con las manos en la tierra |
| **Levantarse del ragdoll** | (ragdoll del jugador, no una habilidad) | Fundido de la postura tumbada a la animación (`EndRagdoll`), sin clip | Que el personaje se ponga en pie con una animación (boca arriba y boca abajo) tras caer |
| **Empuñar / guardar la herramienta** (sacar la pala, la regadera, el semillero) | `Dig`, `Sow`, `Water`, `PlantTree` | El objeto aparece en la mano al activar la habilidad y desaparece al acabar | Un gesto corto de coger y guardar para que no aparezca de golpe |
| **Empujar y soltar la carretilla** | `PushWheelbarrow`, `ReleaseWheelbarrow` | El set `wheelbarrow` (manos en las varas) sin transición | Agarrar las varas y soltarlas; además falta el **modelo** de la carretilla en la mano (hoy solo se dibuja la del escenario) |
| **Regar un arbolito** | `Water` sobre `Tree Spot` | El mismo `watering` de pie | Regar agachado a un sapling a la altura del suelo |
| **Talar con hacha en bucle** | `Chop` (nodos de recurso, [OBJETOS.md](OBJETOS.md)) | `dig_plant` como sustituto | Golpes seguidos de hacha sobre el tronco y el final al caer |
| **Picar roca con el pico** | `Mine` | `dig_plant` como sustituto | Golpes de pico sobre la roca |
| **Agacharse a recolectar** (bayas, fibra) | `Gather` | `pick_fruit` (de pie) | Recoger de plantas bajas |
| **Segar con hoz** | `Harvest` (propuesta, [PROPUESTA_HABILIDADES_Y_ECONOMIA.md](PROPUESTA_HABILIDADES_Y_ECONOMIA.md)) | Interino: `pull_plant` | Cosechar grano y cortar junco |
| **Tejer sentado** (cestería) | `Craft` de cestas y cuerda | Interino: `kneel_idle` | Fabricar cestas y esteras |
| **Tallar piedra** (percutor sobre pedernal) | `Craft` de herramientas de piedra | Interino: `kneel_idle` / `idle_variant_2` | Fabricar hachas de pedernal |
| **Colgar a secar** | `Craft` en el secadero | Interino: `pick_fruit` (alzar los brazos) | Secar bayas y carne |
| **Martillear, levantar viga, cargar tablón** | `Build` | Interino: `dig_plant` | Obras por fases (H8) |
| **Moler con piedra** | `Craft` de harina | Ninguno | Procesar grano |
| **Demostrar y observar** (maestro y aprendiz) | `Teach`, `Study` | Interino: `gesture_acknowledge`, `gesture_thoughtful` | Difusión del conocimiento (H11) |
| **Recoger arcilla** (agachado) | `Gather` en `clay_bank` | Interino: `dig_plant` | Cavar arcilla |

Sugerencia de la propuesta de economía: para bayas bajas y fibra probar `pull_plant` / `plant` en lugar de `pick_fruit` (alcanza hacia arriba y se ve invertido), y para talar probar `sword_slash_2` a `playRate` 0,7 antes que `dig_plant`; ver el apartado 13 de la propuesta.
