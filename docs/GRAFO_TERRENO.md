# Grafo de terreno

Un **grafo de nodos** para la generación procedural del mundo (ruido, erosión, ríos, biomas, dispersión de vegetación/rocas), pensado para sustituir la
generación imperativa fija de `WorldGenerator::GenerateData` (`GB/World/WorldGen.*`) por una red de pasos reutilizables, inspeccionables y editables, al
estilo de los *heightfield SOPs* de Houdini o del framework PCG de Unreal. Calcado deliberadamente del grafo de prioridades de la IA
(`GB/AI/PriorityGraph.*`, ver [GRAFO_PRIORIDADES.md](GRAFO_PRIORIDADES.md)): mismo modelo de nodos/aristas, mismo ciclo validar → compilar → evaluar, misma
filosofía de datos puros sin motor, reloj ni azar.

Todo el cálculo vive en el motor y se ejecuta sin ventana (`GB/World/Graph/*`). No depende de `GB/World/Terrain.h` salvo en el único adaptador que escribe
el resultado sobre un terreno real (`HeightmapTerrainTarget`) y en el nodo de erosión (que reutiliza `GB::HydraulicErosion::Run`, el único punto que
necesita construir un `Heightmap` de verdad).

## 1. Por qué

`WorldGenerator::GenerateData` (`WorldGen.cpp:355-577`) es una función de ~230 líneas que encadena ruido fBm, escultura algebraica de lago/río (mezclas
`smoothstep`, no un proceso físico), una cascada de `if` para decidir el bioma de cada celda y la colocación de árboles/rocas. Investigando el código se
encontraron varios huecos de realismo, de mayor a menor impacto:

1. **La erosión hidráulica existe y está probada (`GB::HydraulicErosion::Run`, `GB/World/Erosion.h/.cpp`, cubierta por `tests/world_test.cpp`) pero nunca se
   llama desde la generación real de un mundo.** Es el hueco de mayor impacto y menor esfuerzo: solo se usa hoy en la lluvia interactiva del Sandbox y en
   tests aislados.
2. No hay erosión térmica (desprendimiento por ángulo de reposo): no existe en absoluto.
3. El río se decide con Dijkstra sobre una rejilla *coarse* de 65×65 **antes** de tallar el terreno final, con un coste ad-hoc; no hay un cálculo de
   acumulación de flujo (D8/D∞) sobre el heightmap resultante.
4. La clasificación de bioma es una cascada de `if` escrita a mano (`Classify`, `WorldGen.cpp:84-102`): añadir un bioma nuevo significa editar esa función.
5. El ruido está duplicado: `Terrain::Hash/ValueNoise/Fbm/TileFbm` y `WorldGen.cpp::Hash2/Noise2/Fbm2` son dos implementaciones distintas del mismo concepto.
6. `Terrain` es una *God Class* que mezcla heightfield puro, escultura de agua, refinamiento de malla y *build* de GPU/materiales; su header arrastra
   `Renderer/Mesh.h`, `Renderer.h` y `Texture.h` incluso para el heightmap puro.

## 2. Qué se adopta de Houdini/UE, y qué no

| Concepto | Houdini / UE | En GB |
| --- | --- | --- |
| Nodos sobre heightfield | SOP `heightfield_noise`, `heightfield_erode`, `heightfield_flow`... | Catálogo abierto de `ITerrainNode` en C++ compilado, registrado en `TerrainNodeRegistry` (sección 4). Sin VEX: cada nodo es una clase, no hace falta un lenguaje embebido. |
| Reutilización / composición | HDAs (Houdini), subgrafos de PCG | El mismo mecanismo de subgrafo que ya usa `PriorityGraph` (`PgResolver`) se puede aplicar igual a `TerrainGraph` cuando haga falta (no necesario para los 3 nodos de la Fase 1). |
| Comparación determinista | *geodiff* de Houdini | Ya existe: `GB::Qa::GoldenData`/`Fingerprint` (`GB/Qa/Qa.h`, su propio comentario ya cita a Houdini como inspiración). Se reutiliza sin cambios. |
| NavMesh dinámico | `RecastNavMesh` + `dtTileCache` | Ver [NAVMESH_INCREMENTAL.md](NAVMESH_INCREMENTAL.md) (Fase 4, no implementada todavía). |
| Editor de nodos | Red de nodos Houdini / PCG graph editor | La base común de interacción está en `Editor/src/NodeGraphEditor.h` y la UI de prioridades la hereda en `Editor/src/EditorPriorityGraph.cpp`; el editor visual de terreno y su modelo específico siguen pendientes de la Fase 1. |

## 3. Modelo de datos

```cpp
struct TgField      // GB/World/Graph/TgField.h — lo que fluye entre nodos: una altura, una máscara, un id de bioma...
{
    uint32_t N; float Size; glm::vec2 Center;
    std::vector<float> Values;   // N*N, fila a fila
};

struct TgNode { std::string Id, Type, Label; float X, Y; std::string Source; std::map<std::string, double> Params; };
struct TgEdge { std::string From, To; std::string Port = "in"; };
struct TerrainGraph { std::string Id, Name; int Version; std::vector<TgNode> Nodes; std::vector<TgEdge> Edges; };
```

`Type` es la clave bajo la que un nodo está registrado en `TerrainNodeRegistry` (sección 4); no hay una lista cerrada como en `PriorityGraph` porque el
catálogo de nodos de terreno está pensado para crecer con el tiempo (erosión térmica, flow-accumulation, máscaras, *scatter*...).

`ValidateTerrainGraph` comprueba ids duplicados, aristas a nodos inexistentes, tipos desconocidos (consultando `TerrainNodeRegistry`) y ciclos (DFS
coloreado que nombra el camino, igual que `ValidatePriorityGraph`). `CompileTerrainGraph` reduce un grafo válido a un orden topológico (Kahn) más, por
nodo, qué nodos compilados alimentan cada uno de sus puertos de entrada, en el orden de sus aristas. `EvaluateTerrainGraph` recorre ese orden llamando a
`ITerrainNode::Evaluate` de cada nodo a través del registro, y junta los campos de los nodos `"output"` en un `TgResult` por nombre de `Source`
(`"height"`, `"biome"`...). Todo es puro: nada toca el motor, el reloj ni el azar más allá de la semilla que trae el `TgContext`.

## 4. Arquitectura SOLID

| Tipo | Archivo | Principio | Por qué |
| --- | --- | --- | --- |
| `TgField`, `TgNode`/`TgEdge`/`TerrainGraph` | `TgField.h`, `TerrainGraph.h` | SRP | Son datos puros: ni evalúan, ni validan, ni saben de E/S. |
| `ITerrainNode` | `ITerrainNode.h` | ISP | Un único método (`Evaluate`); un nodo no implementa nada que no necesite. |
| `TerrainNodeRegistry` | `ITerrainNode.h/.cpp` | OCP | Un nodo nuevo es una clase nueva registrada; `TerrainGraph.cpp` no cambia para añadir un tipo. |
| `ITerrainTarget` / `HeightmapTerrainTarget` / `InMemoryTerrainTarget` | `ITerrainTarget.h/.cpp` | DIP | El núcleo del grafo no conoce `Heightmap`/`Terrain` concretos (calcado de `IGraphTarget` en `GB/Animation/AnimGraph.h`); un test usa un target en memoria de cuatro líneas. |
| `EvaluateTerrainGraph` → `ApplyTerrainGraph` | `TerrainGraph.cpp`, `ITerrainTarget.cpp` | — (dos fases) | Evaluar es puro; aplicar solo escribe si evaluar tuvo éxito y las dimensiones casan (mismo espíritu que `ISaveHost::Validate`/`Commit`, `GB/Save/SaveWorld.h`). |

`TerrainNodeRegistry` es un valor corriente, no un singleton: quien construye un grafo es dueño del registro y se lo pasa a `ValidateTerrainGraph`,
`CompileTerrainGraph` y `EvaluateTerrainGraph`. Así dos mundos o dos tests pueden tener catálogos distintos y un plugin cargado en ejecución puede añadir
(`Register`) o quitar (`Unregister`) tipos sin estado global. Los nodos propios del motor (`output`, `heightfield_noise`, `erosion_hydraulic`, ...) se
registran desde un único punto, `GB/World/Graph/Nodes/BuiltinNodes.cpp` (`RegisterBuiltinTerrainNodes`), y `MakeDefaultTerrainNodeRegistry()` devuelve un
registro nuevo con todos ellos. No hay auto-registro por estáticas de fichero: `GB` es una única biblioteca estática (`Engine/CMakeLists.txt`) y el
`.cpp` de un nodo no exporta ningún símbolo que otra unidad de traducción referencie, así que una estática en él podría quedar fuera del binario. Un nodo
añadido fuera del motor (Editor, Sandbox, un mod) se registra explícitamente en el registro de su dueño, p. ej. desde una función exportada
`GB_RegisterTerrainNodes(TerrainNodeRegistry&)` que invoca el cargador.

## 5. Estado actual (Fase 1, ya implementada)

Prueba de concepto end a end, de riesgo cero sobre el código existente: no toca `WorldGen.*` ni ningún test/golden previo, solo añade código nuevo bajo
`Engine/src/GB/World/Graph/` y dos entradas de `CMakeLists.txt`.

- `TgField.h` — el campo 2D.
- `TerrainGraph.h/.cpp` — datos, validar, compilar, evaluar; el nodo de *plumbing* `"output"` vive en `Nodes/OutputNode.h/.cpp`.
- `ITerrainNode.h/.cpp` — la interfaz y el registro.
- `ITerrainTarget.h/.cpp` — las dos implementaciones de destino y `ApplyTerrainGraph`.
- `Nodes/NoiseNode.h/.cpp` (`"heightfield_noise"`) — ruido fBm propio y autocontenido (deliberadamente una tercera implementación aislada: no toca el
  ruido de `Terrain.cpp` ni el de `WorldGen.cpp`, que siguen siendo responsabilidad de la Fase 6).
- `Nodes/ErosionNode.h/.cpp` (`"erosion_hydraulic"`) — construye un `Heightmap` temporal, llama a `HydraulicErosion::Run` sin tocarlo, copia el resultado.
- `Nodes/BiomeClassifyNode.h/.cpp` (`"biome_classify"`) — tabla de reglas (`Water`/`Shore`/`Snow`/`Rock`/`Forest`/`Grass`) evaluada en orden, en vez de la
  cascada de `if` de `Classify`; con una entrada de humedad opcional (un ruido barato la sustituye si no se conecta nada).
- `Nodes/BuiltinNodes.h/.cpp` — el punto de registro explicado en la sección 4.
- `tests/terrain_graph_test.cpp` (`ctest -R terrain_graph`) — validación (ciclo, id duplicado, tipo desconocido), compilación, un grafo
  `noise → erosion → biome_classify` evaluado y aplicado a ambos targets, determinismo por semilla vía `Qa::Fingerprint`.

## 6. Propuesta: una biblioteca aparte para la lógica pura (`GB_WorldGen`)

Hoy todo el motor es una única biblioteca estática `GB` (`Engine/CMakeLists.txt`): no hay una frontera de *link* entre "lógica de mundo en CPU" y
"render". El grafo de terreno ya separa esto a nivel de *archivo* (ninguno de `TerrainGraph.h/.cpp`, `ITerrainNode.h/.cpp` incluye `Terrain.h`), pero se
puede dar un paso más y convertir esa frontera en un **target de CMake** propio, para que "esto no depende del renderer" sea una garantía que comprueba el
propio sistema de build, no solo una convención de código.

**Qué se movería a `GB_WorldGen` (CPU puro, sin GLFW/OpenGL/Renderer):**

- `GB/World/Graph/*` (todo lo de la Fase 1 y lo que venga en las fases 2-3).
- `GB/World/Erosion.h/.cpp`.
- `GB/World/RiverData.h` (ya es pura data, sin cambios).
- `Heightmap`, hoy definida dentro de `Terrain.h` (`Terrain.h:17-45`) junto a includes de `GB/Renderer/Mesh.h`, `Renderer.h`, `Image.h`, `Texture.h` que
  no le hacen falta para nada. **Prerrequisito**: extraerla a un `GB/World/Heightmap.h`/`.cpp` nuevo (la clase no usa nada de esos headers, es un
  recorte mecánico: mover la declaración y sus cuatro métodos no inline de `Terrain.cpp` sin cambiar una línea de su lógica). `Terrain.h` pasaría a
  incluir `GB/World/Heightmap.h` en vez de declarar la clase, así que **ningún consumidor actual de `Heightmap` vía `#include "GB/World/Terrain.h"` se
  entera del cambio** (sigue compilando igual); solo `Erosion.h`, que hoy incluye `Terrain.h` entero solo para llegar a `Heightmap`, pasaría a incluir el
  header nuevo y más pequeño.
- La parte de `WorldGen.cpp` que ya está separada en código (`WorldGenerator::GenerateData`, CPU puro) de la que no (`BuildGpu`) — la Fase 2 (wiring
  directo del grafo en `GenerateData`) ya va a tocar esa función, así que es el momento natural de mover `GenerateData` sin `BuildGpu` a este target.
- `GB/World/Regions.h/.cpp` (ya es un consumidor puro del `BiomeMap` horneado, sin dependencia de render).

**Qué se queda en `GB`:** `Terrain` (el `Heightmap` que incluye, más la escultura de lago/río que todavía no pasa por el grafo, el refinamiento de malla
cerca de la orilla, el *build* de GPU y materiales), `Trees`/`Rocks`/`Grass`/`Water`/`WaterRipples`/`GroundInteraction`/`WorldTiles` (todos construyen o
dibujan geometría/GPU), y el resto del motor sin tocar.

**Forma del CMake** (`Engine/CMakeLists.txt`):

```cmake
add_library(GB_WorldGen STATIC
    src/GB/World/Heightmap.cpp
    src/GB/World/RiverData.h       # header-only, se lista por claridad
    src/GB/World/Erosion.cpp
    src/GB/World/Regions.cpp
    src/GB/World/Graph/TerrainGraph.cpp
    src/GB/World/Graph/ITerrainNode.cpp
    src/GB/World/Graph/ITerrainTarget.cpp
    src/GB/World/Graph/Nodes/NoiseNode.cpp
    src/GB/World/Graph/Nodes/ErosionNode.cpp
    src/GB/World/Graph/Nodes/BiomeClassifyNode.cpp
    src/GB/World/Graph/Nodes/BuiltinNodes.cpp)
add_library(GB::WorldGen ALIAS GB_WorldGen)
target_include_directories(GB_WorldGen PUBLIC src)
target_compile_features(GB_WorldGen PUBLIC cxx_std_20)
# sin glad/GLFW/glm-render: solo glm (header-only) como dependencia externa

add_library(GB STATIC ...)       # como hoy, menos los .cpp que se movieron arriba
target_link_libraries(GB PUBLIC GB::WorldGen)   # GB sigue exponiendo todo lo de siempre a quien lo use
```

Un test o una herramienta que solo necesite generación de mundo (p. ej. `tests/terrain_graph_test.cpp`, o un futuro `tools/worldgen_cli`) enlazaría
`GB::WorldGen` directamente, sin arrastrar GLFW/OpenGL/ImGui — más rápido de enlazar y, sobre todo, **headless por construcción** en vez de headless por
convención (hoy, según el README, "todo el motor enlaza GLFW y OpenGL incluso para la librería estática `GB`").

**Coste y riesgo**: tocar `Terrain.h`/`Terrain.cpp` (841 líneas, cubiertas por `tests/terrain_test.cpp`, `tests/world_test.cpp`,
`tests/golden_test.cpp`/`golden_data_test.cpp`) para extraer `Heightmap` es mecánico pero invasivo sobre código ya en producción; no lo he hecho todavía
sin confirmación explícita. Es un buen primer paso de la **Fase 6** (limpieza) o se puede adelantar como su propio paso antes de la Fase 2, a elegir.

## 7. Fase 2, ya implementada (con un alcance más preciso que el previsto)

Al leer `WorldGenerator::GenerateData` línea a línea para engancharla, quedó claro que "sustituir toda la rama imperativa por el grafo" en un solo paso
no es realista todavía ni honesto llamarlo "wiring directo": la generación de dunas, la escultura algebraica de lago/río (fusionada con el propio bucle
de ruido dentro de `Terrain::CreateHills`, no una pasada aparte), el camino del río por Dijkstra sobre la rejilla *coarse*, la clasificación de bioma
(`Classify`/`BakeBiomes`) y la colocación de árboles/rocas con sus recetas (`TreeSpecies`, `RockParams`) están todas entrelazadas y dependen las unas de
las otras dentro de esa única función. Sustituirlas todas de golpe por los 3 nodos de la Fase 1 (que ni siquiera cubren río/lago/scatter todavía) habría
sido una reescritura a ciegas de código delicado y ya probado — justo lo contrario de "código limpio" cuando se hace deprisa.

En su lugar, la Fase 2 entregó **la pieza de mayor impacto y menor riesgo real: conectar `HydraulicErosion::Run` (ya implementada, ya probada) a la
generación real de mundos**, que hasta ahora solo la usaba la lluvia interactiva del Sandbox sobre un terreno ya construido. `WorldDesc` gana un campo
`bool Erosion = true` (`WorldGen.h`); `WorldGenerator::GenerateData` (`WorldGen.cpp`) erosiona el heightmap justo después de `Terrain::CreateHills`
(lago y río ya tallados) y antes de que nada más lo lea — línea de nieve, bioma horneado, punto de aparición, árboles y rocas ven todos el terreno ya
erosionado, igual que un jugador que deja llover antes de construir. No afecta a los desiertos (la erosión hidráulica no tiene sentido sin agua; las
dunas siguen con su propio perfil eólico). El número de gotas escala con `Resolution²` para mantener una densidad pareja sea cual sea el tamaño del
mundo (los tests usan mundos mucho más pequeños que el de 641×641 por defecto). Verificado con la suite completa en Windows/MSVC
(`cmake -S . -B build -DGB_WARNINGS_AS_ERRORS=ON` + `ctest`): `worldgen`/`world` pasan sin tocar sus invariantes (línea de nieve, reparto de biomas, punto
de aparición en seco, colocaciones válidas...); `golden_data` se actualizó con `GB_UPDATE_GOLDEN=1` — el diff es exactamente lo esperable de erosión real
(el mínimo de altura sube, se rellenan cuencas; el reparto de `rock` sube un poco donde la erosión expone pendientes nuevas) y se revisó a mano antes de
aceptarlo, no se regeneró a ciegas.

La sustitución de la **rama imperativa completa** (dunas, lago/río, bioma, *scatter*) por nodos del grafo queda, correctamente, para cuando existan los
nodos que de verdad la cubren sin regresión — erosión térmica, acumulación de flujo para el río, *scatter* — que es exactamente lo que entrega la
Fase 3 (sección 9), ya completa: el catálogo está listo, pero el enganche a `WorldGen.cpp` en sí sigue sin hacerse (sección 10). `biome_classify` ya
escribe `GB::Biome` (`GB/World/Biome.h`, extraído de `WorldGen.h` para que el nodo no arrastre `Terrain.h`) en vez de un enum propio de prueba, así que
ya puede alimentar directamente `GeneratedWorld::Maps`/`BiomeAt` el día que se enganche.

## 8. Malla adaptativa por curvatura (resolución variable, ya implementada)

Aparte de la generación de alturas, se pidió explícitamente revisar por qué la malla base "se ve regular": `WorldDesc::Resolution` es un número fijo por
mundo (641 por defecto), y el único refinamiento que existía (`Terrain::BuildShoreRefinement`, con *stitching* sin grietas ya probado) solo se activaba
junto al agua. La solución no es subir `Resolution` globalmente (encarece ruido/erosión/física/IA por igual, sin necesidad) sino **separar la resolución
de los datos (el `Heightmap`, barata) de la resolución de la malla de render (que puede variar)**, generalizando el mecanismo de refinamiento que ya
existía en vez de inventar uno nuevo:

- `Terrain::FlagDetailCells` (nueva, junto a `FlagShoreCells`): compara, por celda, la altura bilineal (la de un *quad* plano, lo que se dibuja sin
  refinar) contra la superficie cúbica que `SmoothHeight` ya reconstruye; si divergen más de `HillParams`/`DuneParams::DetailThreshold` (metros), la
  celda se marca. `Terrain::BuildMeshRefinement` (renombrado desde `BuildShoreRefinement`) junta ambas estrategias con un simple OR bit a bit — ninguna
  conoce a la otra (SRP/OCP: la pila de reconstrucción/costura de vértices no cambió ni una línea).
- Expuesto como `WorldDesc::MeshDetail` (por defecto 0.08 m) → `HillParams`/`DuneParams::DetailThreshold`, y como quinto argumento opcional del comando
  del Editor `generate <tipo> [seed] [trees] [rocks] [mesh_detail] [erosion]` (el sexto argumento, `0`/`1`, activa `WorldDesc::Erosion`), para poder comparar sin recompilar.
- **Resultado del barrido pedido** (mismo seed, misma cámara, umbral ÷10 cada paso: 0.08 → 0.008 → 0.0008): en `temperate` (colinas suaves, `Flatten`
  alto) el refinamiento por curvatura casi no añade nada sobre el de la orilla (ya lo medía la Fase 2, ~310 k → ~310 k vértices). En `alpine`
  (pendiente fuerte + erosión) sí escala mucho: 586 k → 3,9 M → 5,9 M vértices. **Pero, a la distancia de cámara habitual, la diferencia visual entre los
  tres niveles es imperceptible** en ambos casos — el *normal map* de alta resolución ya cubre el sombreado a esa distancia; los vértices de más solo se
  notarían muy cerca del suelo. **Conclusión**: la densidad de malla no era el factor dominante detrás del aspecto "regular" (o lo era muy poco); el
  umbral por defecto (0.08, "x1") se queda como el de producción por su coste/beneficio, y los niveles más agresivos quedan disponibles para depuración o
  una futura vista en primera persona, no como valor por defecto.

**Hallazgo sin resolver, documentado, no arreglado hoy**: investigando una regresión de `tests/settlement_test.cpp` causada por la erosión (un árbol
movido de sitio expone un caso donde `GB::AbstractAgent::Walk`, `GB/AI/Abstract.cpp`, no reubica a un miembro de la tribu al punto de trabajo correcto si
ya llega "bastante cerca" del objetivo, dejándolo a veces dentro del propio obstáculo) se intentó un arreglo localizado que a su vez rompió un invariante
distinto de `tests/sim_sweep_test.cpp` (migración de campamento) por una razón no identificada del todo. Se revirtió el intento y se dejó comentado en el
propio archivo: es un problema real, preexistente, de otro subsistema (IA de movimiento abstracto), expuesto — no causado — por la erosión; no se intentó
de nuevo bajo presión de tiempo para no cambiar un fallo conocido por otro sin verificar.

## 9. Fase 3, completa: catálogo de nodos

Los cuatro nodos que le faltaban al catálogo (docs/GRAFO_TERRENO.md los daba como pendientes) ya están todos, con el mismo patrón de las Fases 1-2:
el algoritmo vive como primitivo reutilizable en `GB/World/*`, el nodo es un adaptador fino que solo convierte `TgField` ↔ `Heightmap` en los bordes.

- **`erosion_thermal`** (`GB/World/ThermalErosion.h/.cpp` + `Nodes/ThermalErosionNode`): hueco de realismo #2 (no había erosión térmica en absoluto).
  Grid de talud/ángulo de reposo, 8-conectado: cada celda mueve una fracción de su exceso de pendiente (por encima de `TalusAngleDegrees`) hacia su
  vecino más bajo. Actualización **secuencial en el sitio (Gauss-Seidel)**, no un búfer de deltas aplicado de golpe (Jacobi): con varias celdas
  empujando al mismo vecino en la misma pasada, un búfer aparte permite que ese vecino acabe, brevemente, por encima de la cumbre original — se
  encontró exactamente ese fallo al escribir `tests/world_test.cpp` (la cumbre no bajaba, la varianza subía en vez de bajar) y se corrigió antes de
  dar el nodo por bueno. Determinista.
- **`flow_accumulation`** (`GB/World/FlowAccumulation.h/.cpp` + `Nodes/FlowAccumulationNode`): hueco de realismo #3. D8 — cada celda drena a su único
  vecino más bajo (8-conectado), recorrido de mayor a menor altura para que la acumulación de cada celda esté cerrada antes de pasarla aguas abajo.
  Calcula la hidrología sobre el terreno **ya final** (con erosión incluida), a diferencia del Dijkstra sobre una rejilla *coarse* que decide el
  trazado del río **antes** de tallar las colinas. Sin relleno de depresiones (*pit-filling*): una cuenca cerrada se queda con su caudal en vez de
  seguir aguas abajo — limitación documentada, no un *bug*. Salida sin normalizar (recuento de celdas en cuenca, no 0..1): un nodo futuro de
  máscara/umbral decide qué cuenta como "esto es un río".
- **`scatter`** (`Nodes/ScatterNode`, reutiliza `GB::ScatterTrees` tal cual): coloca árboles/rocas por *dart-throwing* con espaciado mínimo. Necesitaba
  extender el grafo más allá de "todo puerto es un `TgField`" — una colocación es una lista de posiciones, no un campo escalar. Se hizo de forma
  puramente aditiva: `IScatterNode` (`ITerrainNode.h`), una segunda interfaz opcional (ISP) con un único método `Scatter`; `ITerrainNode` gana
  `AsScatterNode()` (devuelve `nullptr` por defecto, así que `heightfield_noise`/`erosion_*`/`biome_classify` no cambiaron ni una línea); `TgResult`
  gana `Placements` (mapa por id de nodo), rellenado por `EvaluateTerrainGraph` justo después de evaluar un nodo que también es `IScatterNode`.
  **Prerrequisito**: `GB::ScatterTrees` pedía un `const Terrain&` pero solo leía `terrain.GetHeightmap()` — se estrechó a `const Heightmap&`
  (cero cambio de comportamiento, los 4 puntos de llamada existentes en `WorldGen.cpp`/`Sandbox/SandboxAPP.cpp` ahora pasan `.GetHeightmap()`), para
  que el nodo no tenga que levantar un `Terrain` completo (con su malla y materiales) solo para colocar unos árboles.

Con el catálogo completo, la sustitución de la rama imperativa de `WorldGenerator::GenerateData` por un `TerrainGraph` evaluado de principio a fin ya
es viable sin regresión — sigue sin hacerse en este PR (es un cambio de mayor riesgo sobre código en producción, mejor en su propia iniciativa con
tiempo para verificarlo con calma, no apresurado al final de esta). `biome_classify`, `erosion_thermal`, `flow_accumulation` y `scatter` están
probados de forma aislada (`tests/terrain_graph_test.cpp`, `tests/world_test.cpp`) pero **no** conectados todavía a `WorldGen.cpp`.

## 10. Próximas fases

4. **NavMesh incremental** — [NAVMESH_INCREMENTAL.md](NAVMESH_INCREMENTAL.md).
5. **Editor visual**: `EditorTerrainGraph.cpp`/`TerrainGraphLibrary`, clon de `EditorPriorityGraph.cpp`/`PriorityLibrary`.
6. **Limpieza**: sustituir de verdad la rama imperativa de `WorldGenerator::GenerateData` por el `TerrainGraph` (sección 9), extraer `GB_WorldGen`
   (sección 6) si no se adelantó antes, unificar las tres implementaciones de ruido (`Terrain`, `WorldGen.cpp`, el nodo de ruido) en una sola, arreglar
   `GB::AbstractAgent::Walk` (sección 8).

**Guardado y versionado**: por decisión explícita, esta iniciativa no prioriza la compatibilidad con partidas guardadas ni introduce una rama de
`GeneratorVersion` paralela. El sistema de nodos sustituye directamente la generación imperativa; el versionado del generador y la compatibilidad de saves
para el nuevo pipeline se revisan en una iniciativa posterior, una vez el sistema esté completo.

## 11. Agua: un módulo propio (`GB/Water`)

El agua estaba repartida: `Terrain`/`HillParams` mezclaban lago y río con la altura, `WorldGen.cpp` repetía la aritmética del río "a la misma
precisión de bit", `WorldInstance` tenía una tercera copia, y `World/Water.h` arrastraba la clase GL `Mesh`. Ahora cada cosa tiene un dueño:

| Pieza | Archivo | Principio | Qué hace |
| --- | --- | --- | --- |
| `WaterField`, `LakeBody`, `RiverSample` | `Water/WaterField.h/.cpp` | SRP | Datos del agua y las preguntas que se le hacen (distancia, nivel, corriente, "nivel que moja alrededor"). **Único** origen: `Terrain`, `WorldGen` y el editor preguntan aquí. |
| `ApplyLakeBasin`, `CarveRiver` (`LakeShape`, `RiverShape`) | `Water/WaterCarver.h/.cpp` | SRP | La excavación, movida desde `Terrain::CreateHills`/`Terrain::CarveRiver`. `Terrain` solo guarda alturas; los números que eran literales (boca de 16 m, 0,45 m de ribera...) son campos con nombre. |
| `WaterGeometry`, `MakeRiverGeometry` | `Water/WaterGeometry.h/.cpp` | SRP / DIP | La cinta del río y la malla del lago como **datos CPU** sin OpenGL. `WaterSurface` ya no posee ningún `Mesh`; `WaterRenderer` sube la malla la primera vez que la dibuja y la cachea por geometría. |
| `WaveSetGlsl` | `Water/WaterWaves.h/.cpp` | DRY | Las constantes de las olas del shader se generan desde `GB::WaveSet` (marcador `@WAVESET@` en `Shaders::WaterWaves`): CPU y GPU no pueden divergir. |
| `WaterGenParams`, `WaterLook`, `MakeLakeSurface/MakeRiverSurface` | `Water/WaterPresets.h/.cpp` | OCP | Tamaño del lago y ancho del río por tipo de mundo, y el aspecto del agua, antes literales en `WorldGen.cpp` y `WorldInstance.cpp`. |
| `kWaterParamFields` | `Water/WaterParamInfo.h` | OCP | Nombre, miembro y rango de cada parámetro de `WaterParams`: el panel del editor se construye de esta tabla en vez de nueve literales en `EditorApp.cpp`. |

`Terrain` conserva su API pública (`GetRiver`, `DistanceToRiver`, `HasLake`...) como reenvíos a su `WaterField` (`Terrain::Water()`), así que ningún
llamador cambió. `HillParams` sigue teniendo los campos del lago y del río (es la "receta" del terreno y la usan generador, tests y Sandbox); lo que
se movió son los algoritmos. Las constantes de alcance que difieren entre consumidores (`Classify` usa 1,3/1,2/1,8; las rocas mojadas 1,5/3,0) se
dejaron distintas a propósito y ahora son parámetros con nombre (`WaterField::LevelNear`), no copias de la geometría.

### 11.1 El agua sale del grafo (generador 3)

Hasta el generador 2 el río era un Dijkstra sobre una rejilla de 65x65 trazado antes de que las colinas estuvieran terminadas, con un ancho fijo, y el
lago un disco perfecto. El generador 3 (`kWorldGeneratorV3`, `WorldGenVersion.h`; las versiones 1 y 2 conservan su código bit a bit) pregunta a un grafo
de terreno, construido por `MakeWaterGraph` (`Water/WaterPlanner.h`) y evaluado sobre el terreno base:

```
height_input ──► lake_site ───────────────────────────────────────────────┐
     │               │                                                    ▼
     └──► lake_basin ──► depression_fill (salidas: el lago) ──► flow_accumulation ──► river_trace (hasta el lago)
```

| Nodo | Qué hace | Matemática |
| --- | --- | --- |
| `height_input` | Trae al grafo un campo de fuera (`TgContext::Inputs`). | — |
| `lake_site` | La cuenca más baja lejos de los bordes (máscara de salida y punto). | Media 3x3 sobre una rejilla gruesa: un valle ancho gana a un hoyo ruidoso. |
| `lake_basin` | Excava el lago en el campo con la misma función que el carver (`ApplyLakeBasin`). | Un lago es un hueco con bancos: sin excavar, el agua de alrededor pasa de largo. |
| `depression_fill` | Rellena los sumideros para que toda celda drene. | Priority-Flood con épsilon (Barnes, Lehman y Mulla, 2014): O(N log N), deja una pendiente mínima hacia la salida. |
| `flow_accumulation` | Área de drenaje de cada celda (ya existía; ahora sobre el terreno relleno). | D8: cada celda va a su vecino más empinado. |
| `river_trace` | El río: la línea de flujo más larga que acaba en el lago. | Sigue las direcciones D8, suaviza la escalera con media móvil y adelgaza a un punto cada 6 m con el área de drenaje de cada uno. |

`IPathNode` es una tercera interfaz opcional (como `IScatterNode`): `lake_site` y `river_trace` devuelven caminos (`TgResult::Paths`).
`ApplyWaterPlan` escribe el plan en la receta del terreno; si el grafo fallase, el mundo se construye igualmente con el método antiguo.

**Un río con física** (`Water/RiverHydraulics.h`). Cada punto del río lleva su caudal relativo q = área de drenaje / área en la desembocadura:

- Geometría hidráulica de Leopold y Maddock (1953): ancho `W ∝ Q^0,5`, profundidad `d ∝ Q^0,4`, velocidad `v ∝ Q^0,1` (los exponentes suman 1 por continuidad
  Q = W·d·v). El río nace con un 30 % del ancho de la desembocadura y se ensancha aguas abajo; `RiverData::HalfWidths` guarda el ancho de cada punto y todas
  las consultas (`WaterField`), la excavación, la malla del agua y el bioma leen el ancho local.
- Manning (1891): `v = (1/n) R^(2/3) S^(1/2)`, con `R = W·d / (W + 2d)` y `n = 0,04`. Sustituye a la regla empírica anterior (`0,9 + 18·pendiente`); se acota a
  0,6–4,5 m/s para que el río no se pare ni corra como una cascada.

**Un lago con orilla propia** (`Water/LakeOutline.h`). El radio depende del ángulo, `r(θ) = R·(1 + a·N(θ))`, con `N` una suma de armónicos bajos (2, 3 y 5) de fases
distintas en cada semilla; `|N| ≤ 1`, así que la orilla se queda en `R·(1 ± a)` y no tiene esquinas. Con `a = 0` es el círculo de siempre. Todas las
fórmulas escritas para un círculo (cuenca, bancos, desembocadura) usan la distancia efectiva `d / r(θ)/R`.

**Erosión.** La erosión hidráulica de después rellenaba el cauce estrecho y poco profundo, así que el generador 3 devuelve el corredor del río (hasta 6 anchos
medios) tal y como lo excavó el carver, con una transición suave hacia el terreno erosionado.

Otros ajustes que llegaron con esto: `AbstractAgent::Plan` termina la ruta en el objetivo con su alcance cuando ésta acaba dentro de él, y el cuerpo simulado
de los tests (`SimBody`) sale de los obstáculos como hace el controlador de un personaje real.
### Comparativa antes/después

`docs/img/water/before/` y `docs/img/water/after/` (no se versionan: se regeneran) guardan las capturas (semillas 5 y 77: vista cenital de todo el mapa y primeros planos con `camera lake` y
`camera river`) del generador 2 y del 3; `docs/img/water/compare_*.jpg` las pone lado a lado (`tools/water_compare.ps1`). Se reproducen con (el séptimo argumento de
`generate` elige el generador; un guion salta un argumento):

```
Editor.exe --hidden --quality=medium "--exec=generate temperate 5 - - - - 2; time 12; camera lake; wait 4; screenshot docs/img/water/before/seed5_lake.png; quit"
Editor.exe --hidden --quality=medium "--exec=generate temperate 5; time 12; camera lake; wait 4; screenshot docs/img/water/after/seed5_lake.png; quit"
powershell -File tools/water_compare.ps1
```