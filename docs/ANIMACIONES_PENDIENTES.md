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
| **Talar con hacha en bucle** | `Chop` (nodos de recurso, [OBJETOS.md](OBJETOS.md)) | dos tajos `sword_slash_1` y `sword_slash_3` con la herramienta (`tool`) en la mano | Golpes seguidos de hacha sobre el tronco y el final al caer |
| **Picar roca con el pico** | `Mine` | golpe `sword_attack_4` con la herramienta en la mano | Golpes de pico sobre la roca |
| **Agacharse a recolectar** (bayas, fibra) | `Gather` (bayas), `PullFiber` (fibra) | `pick_fruit_2` (brazo a la altura de un arbusto; `pick_fruit` alcanzaba hacia arriba y parecía invertido) y `pull_plant` (agachado) | Recoger de plantas bajas |
