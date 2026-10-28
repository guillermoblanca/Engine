# Recursos repartidos por el mundo generado

Hasta ahora los nodos de recurso (árboles talables, rocas, bayas, arcilla, juncos, pedernal, cobre) solo existían en el **parche** de `resources build`: unas filas
planas alrededor de un punto, sin relación con el mundo generado. En un mapa con relieve los nodos flotaban o se hundían (solo el origen tomaba la altura del terreno),
podían caer en el lago o encima de un árbol del mundo, y los 420 árboles y 70 rocas de `generate` eran solo decorado. Este cambio reparte los nodos **por todo el mapa
según el bioma**, guiado por datos, determinista por semilla y sin GL.

[← Volver al README](../README.md) · relacionado: [OBJETOS.md](OBJETOS.md) (nodos y parche E1), [mundo.md](mundo.md) (generación del mundo)

## Qué hay

| Pieza | Qué es |
|---|---|
| `ResourceSpawnDef` (`ResourceNodeDef::Spawn`, JSON `spawn`) | Dónde crece cada tipo: `biomes`, `perHectare`, `spacing`, `maxSlope`, `waterWithin`, `clump` (0..0,99, bosquetes y vetas), `max` |
| `ResourceGround` | Lo que el planificador sabe del suelo: límites, altura, bioma, pendiente, distancia al agua y obstáculos (árboles y rocas del mundo). Son funciones, así que un test le da cualquier suelo |
| `PlanWorldResources(ground, nodes, seed, density)` | Devuelve los sitios (`ResourceSpot`: tipo, posición sobre el suelo, giro). Solo CPU, sin escena |
| `ResourceGroundOf(GeneratedWorld)` | El suelo de un mundo generado, con sus árboles y rocas como obstáculos. El bioma y la distancia al agua salen del mapa horneado con el mundo (`GeneratedWorld::Maps`) |
| `BuildWorldResources` | Crea los nodos bajo la raíz `World resources`, con nombres `wild <tipo> <n>` que no chocan con los del parche (`tree 1`) |
| `SnapToGround` | Asienta cada nodo de un parche sobre el terreno (el editor lo aplica a `resources build`) |

Editor: `generate temperate 5`, `resources world [densidad]` (1 por defecto; 0,5 un mundo pobre, 2 uno rico) → "N resource nodes spread over the world: …"; `resources status` los
agrupa (`wild tree`, `wild reed bed`…). La tribu, las habilidades y el `ResourceSystem` los encuentran por sus etiquetas como a cualquier otro nodo.

## Cómo reparte

1. Los tipos con `spawn` se ordenan de **más raro a más común** (`perHectare`, luego id): el cobre y el pedernal se colocan antes y los comunes no los tapan.
2. Por tipo, una rejilla de celdas de `spacing` metros; cada celda da un candidato con desplazamiento aleatorio y se queda con la probabilidad que da `perHectare` de media.
   Las celdas que quedan se recorren en el orden de su hash (no fila a fila), para que un tipo con `max` se reparta por todo el mapa y no llene el borde sur.
   El azar es **hash entero** de (celda, semilla, tipo): no depende de la plataforma.
3. Un candidato se descarta si: cae en un campo de ruido por debajo de `clump` (así hay bosquetes y vetas), su bioma no está en `biomes`, la pendiente pasa de `maxSlope`,
   está a menos de 1 m del agua o, con `waterWithin`, a más de esa distancia, choca con un árbol o una roca del mundo, o está a menos de `spacing` de otro nodo (de cualquier tipo:
   gana el `spacing` mayor de los dos).
4. `max` limita el total del tipo; `density` multiplica `perHectare` y `max`.

Valores de la era 1 (en `RegisterEraOneItems` y en `assets/data/era1.json`, que un test obliga a que coincidan):

| Nodo | Biomas | /ha | Separación | Pendiente máx. | Agua | Agrupado | Máx. |
|---|---|---:|---:|---:|---|---:|---:|
| `tree` | Forest, Meadow | 6 | 8 | 30° | | 0,3 | |
| `old_tree` | Forest | 8 | 9 | 28° | | 0,2 | |
| `young_tree` | Forest, Meadow | 8 | 5 | 30° | | 0,35 | |
| `berry_bush` | Meadow, Forest | 6 | 5 | 25° | | 0,5 | |
| `fiber_grass` | Meadow, Shore | 8 | 5 | 25° | | 0,3 | |
| `rock` | Rock, Meadow, Shore, Dunes | 4 | 7 | 45° | | | |
| `flint_outcrop` | Rock, Shore, Dunes | 4 | 6 | 45° | | 0,55 | 16 |
| `copper_vein` | Rock, Snow | 2 | 8 | 50° | | 0,7 | 5 |
| `clay_bank` | Shore, Meadow | 15 | 6 | 15° | ≤ 9 m | 0,4 | 12 |
| `reed_bed` | Shore, Meadow | 25 | 4 | 15° | ≤ 6 m | 0,2 | 24 |

`perHectare` es la **media sobre el suelo que encaja** (el agrupado no la reduce: deja fuera una parte `clump` del suelo y reparte esa densidad en el resto); la
separación y los obstáculos la recortan donde el suelo está lleno.

Medido en el editor sin ventana (`generate …; resources world`, mapa de 360 m):

| Mundo | Nodos | Reparto |
|---|---:|---|
| templado 5 | 305 | 71 bayas, 67 árboles jóvenes, 59 árboles, 44 árboles viejos, 32 rocas, 21 fibra, 6 arcilla, 3 pedernal, 2 juncos |
| alpino 3 | 209 | 57 jóvenes, 46 bayas, 38 árboles, 25 fibra, 16 rocas, 13 viejos, 4 arcilla, 4 pedernal, 3 juncos, 3 cobre |
| templado 3, densidad 0,5 / 2 | 169 / 533 | |
| desierto 2 | 62 | 46 rocas, 16 pedernal |

Planificar un mapa entero cuesta menos que generarlo (decenas de miles de consultas de bioma); no se ha medido aparte.

## Pruebas

- `world_resources` (CPU): suelo sintético con bosque, pradera, roca, acantilado y lago. Cada nodo en su bioma, sin pasar su pendiente, fuera del agua, junto al agua si lo pide,
  dentro del mapa, sobre el suelo, fuera de los obstáculos y separado de los demás; topes; misma semilla = mismos nodos y otra semilla = otros; la densidad escala; el `spawn` viaja
  por el JSON y se rechazan biomas desconocidos o `clump` fuera de 0..0,99; los tipos con tope se reparten por todo su suelo; los nodos construidos se pueden recoger; un parche en una ladera queda sobre el suelo.
- `sweep` (CPU, `WorldGenerator::GenerateData`): en todos los mundos del barrido (templado, alpino, desierto, cantidades de bosque, sin río, sin lago) los nodos no caen en el agua ni dentro de árboles o rocas, están
  sobre el terreno y en su bioma, y un mundo verde tiene madera, piedra y bayas.

## Límites y siguientes pasos

- **Hecho: los nodos repartidos forman parte de la línea base de la semilla.** La densidad de `resources world` se guarda en `WorldParams::WildResources` (`world.wild`) y
  `EditorSaveHost::BuildBaseline` los vuelve a planificar; el guardado diferencial solo lleva los que cambiaron. Ver [GUARDADO.md](GUARDADO.md).
- **Los árboles y rocas del mundo siguen siendo decorado**: el paso siguiente es que el árbol decorativo *sea* el nodo (talar el árbol que se ve) en vez de poner un nodo de primitivas al lado.
- **Distancia al agua real (hecho):** `WaterDistance` era la distancia al círculo del lago y al eje del río; ahora es una transformada de distancia euclídea exacta sobre las muestras
  de agua del mapa de biomas horneado (rejilla de 1 m), así que mide la orilla tal como es. Los recuentos de la tabla de arriba se midieron antes de este cambio y con el generador 1.
  La orilla (`Biome::Shore`) sigue siendo una franja de menos de un metro de altura sobre el agua, así que juncos y arcilla siguen siendo pocos: falta medirlo de nuevo.
- **Los nodos son primitivas** (cubo y esfera) junto a árboles y rocas procedurales: se nota en las capturas. Junto con el punto anterior, usar `TreeFactory`/`Rocks` para su aspecto.
- Hecho: `WorldGenerator::GenerateData` genera un `GeneratedWorld` sin GL (la malla y las texturas son un paso aparte, `BuildGpu`); `sweep`, `golden_data` y `worldgen` ya no necesitan ventana.
