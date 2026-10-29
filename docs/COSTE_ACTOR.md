# Coste de un actor completo (medida de H0)

Fija los presupuestos de H6 (cerebro de los agentes) y H16 (escala). Se mide con `tests/actor_test.cpp`:

```
GB_ACTOR_BENCH=1 GB_MIXAMO_DIR=<packs>/locomotion GB_INJURED_DIR=<packs>/injured GB_FARMING_DIR=<packs>/farming build/tests/Release/GBActorTests
```

Un "actor completo" es el de `Actor`: controlador y seguimiento del movimiento, mezcla de locomoción del personaje real (X Bot con los siete packs, 65 huesos), IK de pies sobre el suelo, sockets y mirada, y las tareas de un NPC (`goto ... run`). **No incluye el dibujo**: el skinning y la malla son de la GPU y cuentan aparte (H16). La física del ragdoll solo corre mientras el actor está caído.

## Medidas

Máquina: Intel Core i7-11700K (3,6 GHz), Windows 11, MSVC, Release, un solo hilo, pasos de 1/60 s, 300 fotogramas tras 120 de calentamiento, actores caminando y corriendo por un suelo plano.

| Actores | ms por fotograma del mundo | µs por actor y fotograma | de ellos: animación + IK | de ellos: control |
|---:|---:|---:|---:|---:|
| 1 | 0,043 | 42,5 | 42,1 | 0,3 |
| 8 | 0,352 | 44,0 | 43,3 | 0,4 |
| 32 | 1,723 | 53,9 | 51,9 | 1,0 |
| 128 | 5,873 | 45,9 | 44,2 | 0,8 |

- **Un actor completo cuesta unos 45 µs por fotograma** (≈ 50 µs con 32): el 98 % es animación (mezcla de clips, IK, skinning de la pose en CPU); el controlador y las órdenes son menos de 1 µs.
- **Un actor caído (ragdoll):** unos 114 µs por fotograma (paso de física de 13 cuerpos y 12 uniones más la pose); solo mientras está en el suelo.
- **Obstáculos:** ver la última tabla de la salida del test (32 actores entre 3000 árboles y rocas). Cada actor lleva **su copia** de la lista de obstáculos estáticos (≈ 12 B por obstáculo) y comprueba todos; hay que sustituirlo por una rejilla espacial compartida cuando haya cientos de actores o decenas de miles de obstáculos (H16).

**Remedición (pase de calidad, otra compilación y otro día, misma máquina):** 16 µs por actor con 1 actor, 18 con 8, 21 con 32 y 22 con 128 (2,8 ms los 128); ragdoll 88 µs. Las cifras de arriba siguen siendo el techo prudente para presupuestar; la diferencia (más del doble) conviene
repetirla con la compilación de CI antes de fiarse de la cifra baja. Datos de tribu, ecología y búsqueda por nombre en [INFORME_CALIDAD.md](INFORME_CALIDAD.md).

## Nivel de detalle de la animación (H16)

`ActorWorld::SetViewer` (la cámara) y `AnimLodConfig` (por preset de `quality`: `AnimLodForQuality`) deciden, por distancia al visor, cuánto se anima cada actor.
**Solo es presentación**: todo lo que lleva tiempo (fase de la locomoción, fundidos, grafo de estados, montajes y sus notificaciones, que las habilidades esperan,
y los pasos) avanza cada fotograma igual que a detalle completo; lo que se salta es lo que solo se ve (`AnimInstance::SetDetail`):

| Nivel | Distancia (medium) | Qué se hace |
|---|---|---|
| completo | < 25 m | muestreo de clips, IK de pies, mirada, manos en los objetos (`ApplyGrips`) y la `ReachLayer` (capas `IAnimLayer::Procedural`) |
| reducido | 25–50 m | sin IK ni manos; pose cada 2 fotogramas (escalonada por actor) |
| lejano | > 50 m | sin IK; pose cada 4 fotogramas; no se dibujan los accesorios; `TribeVisuals` no calcula el alcance de las manos |
| abstracto | > `DetailRadius` de la tribu | el actor se suspende (ya existía) |

Presets: `minimum` 12/30 m y pose cada 2/6; `low` 18/40, 2/4; `medium` 25/50, 2/4; `high` 35/70, 1/3; `ultra` 50/100, 1/2. El test `actor` comprueba con el humanoide
generado y con el X Bot (packs/full) que **las posiciones y los eventos de montaje son idénticos fotograma a fotograma** con y sin nivel de detalle.

**Medido** (`GB_ACTOR_BENCH=1`, X Bot de packs/full, filas de actores de 8 a 92 m del visor, mediana de 5 ejecuciones de 300 fotogramas; la máquina estaba cargada con
otras compilaciones, así que el coste absoluto por actor salió de 120 a 270 µs, muy por encima de las cifras de arriba; lo que vale es la proporción):

| Actores | sin LOD | ultra | medium | minimum |
|---:|---:|---:|---:|---:|
| 32 | 4,55 ms (100 % de poses) | 3,89 ms (100 %) | 1,49 ms (46 %) | 0,57 ms (26 %) |
| 128 | 19,0 ms | 12,8 ms (96 %) | 4,57 ms (34 %) | 3,52 ms (20 %) |

Con `medium` una multitud repartida hasta 90 m cuesta **unas 4 veces menos**. En el modo de juego del editor (`play temperate 5 3 2`, ventana oculta de 1600×617, esta máquina
cargada) la IA y los actores juntos cuestan menos de 1 ms por fotograma (`stats ai`: tribus 0,02–0,5 ms, actores 0,14–0,3 ms), así que los 3–7 fps de ese modo no vienen de
aquí: antes 180 a 300 ms por fotograma, después 140 a 190 ms, diferencia dentro del ruido de la máquina. Ver [INFORME_CALIDAD.md](INFORME_CALIDAD.md) (sección 11).

## Presupuestos que se derivan

- A 60 Hz hay 16,7 ms por fotograma. Con un presupuesto de animación y movimiento de **2 ms** caben unos **40 actores animados completos por hilo** (45 µs c/u); **128 actores cuestan 5,9 ms**, así que más allá de unas decenas de personajes en pantalla hace falta **nivel de detalle**: lejos, sin IK ni mezcla completa (solo un clip y su fase) o sin animación (posición y pose congeladas), y fuera de pantalla solo el controlador (≈ 1 µs).
- Para H6: la simulación social de un agente fuera de cámara no debe pasar de **~10 µs por actualización** si se actualiza a 60 Hz; lo normal es actualizar su cerebro a 1–5 Hz y su movimiento a la frecuencia de su nivel de detalle.
- Los actores son independientes entre sí (cada uno solo lee un instantánea de posiciones de los demás al empezar el fotograma), así que `ActorWorld::Update` se puede repartir en hilos sin cambiar el resultado cuando H16 lo pida.
