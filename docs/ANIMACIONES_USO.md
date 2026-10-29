# Uso de las animaciones: qué clip hace cada acción

Auditoría de los 183 clips de `packs/full` (los 106 montajes y 10 sets de `assets/animation/mixamo.manifest.json`) y su asignación a cada
acción del juego. **La asignación es un dato del manifiesto**, no una lista en C++:

- `montages`: además de los de cada pack, **alias** con su velocidad y sus notificaciones (`chop_1` es `sword and shield slash` a 0,85 con la
  notificación `chop` donde la mano va más rápida). Las notificaciones son las que esperan los eventos de la habilidad; el test comprueba que
  cada montaje de una habilidad lleve las suyas.
- `abilities`: habilidad → montajes en turno (y, si no tiene clip, el set de locomoción). `ApplyManifestAbilities` (`AbilitySystemComponent.h`)
  los pone en la biblioteca al construir el animador de un personaje, solo con los montajes que ese personaje puede reproducir: un personaje sin
  los packs conserva los de antes. No cambia costes, eventos ni sus tiempos: sin personaje la habilidad sigue igual con temporizadores.
- `activities`: trabajo de la tribu → montajes (en turno; `loop` los repite mientras dure) y set. Los reproduce `TribeVisuals` cuando el miembro
  está quieto en su sitio haciendo el trabajo y ninguna habilidad lo ocupa ([VISUALES.md](VISUALES.md)).
- `stages`: niño ×1,2, adulto ×1, mayor ×0,8 sobre la velocidad de los montajes de actividad.
- `MontageDesc::HoldEnd` (`holdEnd` en JSON, nuevo): el montaje se queda en su último fotograma hasta que se para (tumbarse a dormir, caer muerto).

Los tiempos de las notificaciones están **medidos**: el test `anim_usage` con `GB_PACKS_DIR` calcula, para cada montaje de habilidad, el instante
del golpe (la mano más rápida, si pasa de 5 m/s) o del alcance (la mano más lejos del cuerpo y más baja) y exige que la notificación caiga ahí
(±0,12 en golpes, ±0,2 en el resto). `GB_ANIM_PROBE=1` imprime esa medida para los 183 clips (es la tabla con la que se eligió).

## Habilidades

| Habilidad | Montajes (clip, velocidad) | Notificación | Por qué / qué falta |
|---|---|---|---|
| `Chop` (talar) | `chop_1` (sword and shield slash ×0,85), `chop_2` (slash (3) ×0,85) | `chop` en el golpe | dos tajos laterales con el hacha en la mano; falta un hachazo de verdad en bucle y la caída |
| `Mine` (picar) | `mine_1` (sword and shield attack (4) ×0,8), `mine_2` (attack (3) ×0,8) | `mine` en el golpe | golpes de arriba abajo con el pico; falta un picado a dos manos contra el suelo |
| `Gather` (bayas) | `gather_bush` (pick fruit (2)), `gather_low` (pick fruit (3), agachado) | `gather` al alcanzar | a la altura del arbusto y abajo |
| `PullFiber` | `pull_fiber_1` (pull plant), `pull_fiber_2` (pull plant (2), de rodillas) | `pull` | arrancar una mata |
| `Collect` (arcilla, juncos) | `collect_dig` (dig and plant seeds ×1,1), `collect_pull` (pull plant (2)) | `collect` | cavar con las manos y arrancar; falta recoger arcilla a la cesta |
| `Stoke` (avivar) | `stoke_fire` (plant a plant ×1,15) | `stoke` | se agacha y mete la leña; falta agacharse y alimentar el fuego |
| `DrawWater` (pozo) | `draw_water` (pick fruit ×1,1) | `draw` | alzar los brazos y bajar como tirando de la cuerda; falta tirar de la cuerda con el cubo |
| `OpenDoor` / `CloseDoor` | `door_open` / `door_close` (Standing 1H Magic Attack 01 ×0,75) | `open` / `close` | empujar con una mano; falta empujar y tirar de una puerta |
| `Build` (H8) | `build_hammer` (sword and shield attack (4) ×0,75), `build_place` (plant tree ×1,3) | `build` | golpes de mazo y plantar un poste; falta clavar estacas, levantar vigas y techar |
| `Dig`, `Sow`, `Water`, `PlantTree` | `garden_dig`, `garden_sow`, `garden_water`, `garden_plant_tree` | `dig`, `sow`, `pour`, `plant` | los clips de granja de siempre, ahora con su notificación en los datos |
| `PickFruit`, `Pull`, `Milk`, `DumpWheelbarrow` | `garden_pick_1..3`, `garden_pull_1..2`, `garden_milk`, `garden_dump` | `pick`, `pull`, `milk`, `dump` | idem |
| `CarryBox` / `DropBox` | set `box` / `free` | — | sin clip propio: falta agacharse y levantar o dejar |
| `PushWheelbarrow` / `ReleaseWheelbarrow` | set `wheelbarrow` / `free` | — | falta coger y soltar las varas |
| `ShootBow` (futura, H14) | `bow_draw`, `bow_recoil`, set `bow_aim` | — | para el carril de conflicto: tensar, apuntar andando, soltar |

## Actividades de la tribu

| Trabajo (`TribeMember::Job`) | Montajes / set | Notas |
|---|---|---|
| `eat` | `act_kneel` (kneeling idle) en bucle | + puñado de bayas en la mano; falta llevarse la mano a la boca |
| `drink` (de la mochila) | `act_drink` (relieved sigh, torso) | en el pozo es `DrawWater`; falta beber |
| `sleep` | `act_lie_down` (Standing React Death Backward ×0,55, `holdEnd`) | cae hacia atrás y se queda tumbado; falta tumbarse, dormir y levantarse |
| `warm`, `stoke` | `act_crouch` (Crouch Idle) | agachado junto al fuego |
| `regroup`, `rest` | `idle_variant_2..4` en bucle | variaciones de reposo |
| `socialize` | seis gestos del torso en turno (happy, nod yes, acknowledging, lengthy nod, cocky, sigh) | se ven hablando |
| `teach` (un mayor socializando) | happy, hard nod, dismissing, lengthy nod | explicar; falta demostrar una tarea |
| `craft` (herramienta en el campamento) | `act_knap` (dig and plant seeds ×0,8) en bucle | de rodillas trabajando el suelo; falta tallar sílex, tejer |
| `make` (en el banco) | `act_bench` (Standing 2H Cast Spell 01 ×0,6) en bucle | manos trabajando delante + IK al tablero; falta trabajar en un banco |
| `deposit`, `haul`, `deliver` | `act_put_down` (plant a plant ×1,3) | la carga va en brazos con `holding`/`box`/`wheelbarrow` (reglas de props) |
| `fire` | `act_light_fire` (dig and plant seeds ×1,4) en bucle | + leña en la mano; falta un taladro de fuego |
| `experiment` | `act_kneel`, `gesture_thoughtful` | prueba y piensa |
| `build` (H8) | `act_crouch` | entre golpes (los golpes son la habilidad `Build`) |
| `farm.till` / `.sow` / `.tend` / `.harvest` | `act_till` (attack (4) ×0,7) / `plant` / `water` / `pull_plant`, `pull_plant_2` | + azada u hoz en la mano; falta azadonar y segar |
| `explore` | `act_look_around` (look away gesture, torso) | mira alrededor al llegar |
| `dead` | `act_die` (Standing React Death Forward, `holdEnd`) | |
| `flee` (H14) | set `standing` (sprint) | |
| `hunt` (H14) | `act_look_around` al pararse | el arma (hacha o pico) en la mano; los golpes son los `Attack.*` de H14; con un arco: `bow_draw`, `bow_recoil`, set `bow_aim` |
| `fight`, `raid` (H14) | — (los montajes de ataque y bloqueo son de las reglas de combate de H14) | el arma en la mano; con herida seria, set `injured`; muerto o en ragdoll, sin props |
| `recover` (H14) | `act_kneel` en bucle | el herido descansa; falta tumbarse herido, sujetarse la herida |
| `care` (H14) | `act_kneel`, `gesture_nod_long` | de rodillas junto al herido; falta curar una herida |
| `injured` | `injured_hurting`, `injured_stumble`, set `injured` | con `Health` bajo (gancho para H14) |

`gather` no es actividad: el trabajo son las habilidades de arriba, y mientras camina el director solo pone las herramientas.

## Clips que siguen sin usar

De los 183: los giros andando del pack de heridos (`injured walk/run left/right turn`, `injured backwards turn left/right`,
`injured run backwards left/right turn`), las variantes `sword and shield walk (2)` y `run (2)`, `wheelbarrow walk (2)` y
`wheelbarrow walk turn (2)`. Son curvas o duplicados que la mezcla de locomoción no necesita. Del pack de magia y del de espada quedan sin uso
de juego (sí en el manifiesto, para H14) los ataques mágicos, bloqueos, reacciones por dirección, muertes laterales y ataques de espada que no
hacen de herramienta.

## Lista para el artista

**Clips que faltan** (por prioridad, todos para el esqueleto Mixamo de 65 huesos):

1. Hachazo en bucle contra un tronco (inicio, bucle, final con el árbol cayendo).
2. Picar con pico a dos manos contra una roca en el suelo.
3. Agacharse y recoger del suelo / dejar en el suelo (con y sin caja); levantar una caja.
4. Tumbarse, dormir (bucle) y levantarse (boca arriba y boca abajo, también para el ragdoll).
5. Sentarse en el suelo y comer (mano a la boca); beber de un cuenco.
6. Tallar sílex sentado (percutor); tejer cestería sentado.
7. Trabajar en un banco de pie (manos sobre el tablero).
8. Clavar estacas con mazo, levantar una viga con otro, techar con paja (H8).
9. Azadonar, segar con hoz, atar haces.
10. Tirar de la cuerda de un pozo con cubo; avivar el fuego en cuclillas; taladro de fuego.
11. Empujar y tirar de una puerta; coger y soltar las varas de la carretilla.
12. Demostrar una tarea (maestro) y observar (aprendiz); calentarse las manos al fuego.

**Modelos que faltan o sustituirían primitivas:**

- Personajes **niño** y **mayor** (hoy el X Bot escalado: el niño es un adulto pequeño) y una mujer; ropa de piel por era.
- Herramientas con agarre medido (hacha, pico, azada, hoz, cesta) para sustituir las procedurales y no atravesar la mano en los clips de espada.
- Animales: conejo animado (hoy cuerpo procedural sin animación), ciervo y lobo para H14.
- Edificios: los procedurales de `visuals.json` sirven de bloque; un juego de piezas modulares (estaca, poste, panel de zarzo, techo de paja)
  daría el mismo crecimiento por fases con mejor aspecto.
- Hoguera, pozo y puerta (siguen siendo primitivas de los interactivos).
