# Grafo de prioridades

Un **grafo de decision** editable que puntua las opciones de accion de un personaje (comer, beber, dormir, recolectar, cazar, explorar...).
Es opt-in: por defecto el grafo `builtin` no cambia nada y la simulacion con la misma semilla da exactamente los mismos resultados.
Se puede asignar un grafo distinto por **rol** o por **personaje**, guardarlo en JSON, duplicarlo, usarlo como plantilla e incluir un grafo dentro de otro.

Todo el calculo vive en el motor y se ejecuta sin ventana (`GB/AI/PriorityGraph.*`, `GB/AI/PriorityLibrary.*`, `GB/Game/TribePriorities.*`). El Editor solo dibuja
(`Editor/src/EditorPriorityGraph.cpp`).

## 1. Que puntua el grafo (y que no)

La IA actual (`Tribe::Decide`) lista opciones (`UtilityOption`) con una puntuacion que ya es un producto de consideraciones
(`Factors`: hambre, distancia, rol, memoria...), las escala por la agenda del dia (`AgendaFactor`), el tablon de trabajos, la sociedad y la piramide de Maslow,
y `ChooseOption` elige con histeresis. El grafo se aplica **justo antes de `ChooseOption`**, cuando ya existen todas las opciones (incluidas hunt, make, farm,
haul, talk...) y cada una trae su puntuacion final y sus factores. Por eso:

* El grafo **no crea opciones**: puntua las que la tribu ya genero (si no hay comida, no existe la opcion `eat`; ningun grafo la inventa).
* Puede **reemplazar**, **multiplicar** o **sumar** a la puntuacion de cada tipo de opcion (`mode` de la salida).
* Las entradas incluyen la puntuacion actual (`option.score`) y los factores que la componen (`factor.<nombre>`), asi que "ampliar" es multiplicar por una
  curva nueva (p. ej. "de noche explorar vale la mitad") y "sustituir" es construir la formula desde las necesidades en bruto.

## 2. Modelo de datos

`PriorityGraph { id, name, description, author, tags[], template, version (esquema), revision, nodes[], edges[] }`

`PgNode { id, type, label, x, y, source, fn, params{} }`, `PgEdge { from, to, port, weight }`.

Una arista lleva `valor(from) * weight` al nodo `to`. Cada nodo combina las aristas que le llegan, en el orden de la lista:

| tipo | `source` / `fn` | valor |
| --- | --- | --- |
| `input` | `source` = nombre de entrada (seccion 4) | `x * scale + offset` (`params`; por defecto 1 y 0). Si la entrada no existe en este contexto: `default` (0) |
| `const` | | `params.value` |
| `curve` | `fn`: `linear`, `ramp`, `sigmoid`, `inverse`, `threshold`, `urgency`, `falloff` | funcion de la suma de entradas `x` (ver abajo) |
| `combine` | `fn`: `mul`, `sum`, `min`, `max`, `weighted` | producto / suma / minimo / maximo / media ponderada (`sum(v*w)/sum(w)`) de las entradas. Sin entradas: `params.default` (1 en `mul`, 0 en los demas) |
| `clamp` | | suma de entradas limitada a `params.lo`..`params.hi` (0..1) |
| `output` | `source` = tipo de opcion (`eat`, `gather`... o `*`), `fn` = modo `replace`/`multiply`/`add` | suma de entradas (el modo lo aplica quien consume la salida) |
| `subgraph` | `source` = id de otro grafo | valor del nodo `return` del grafo incluido; cada arista que entra indica en `port` el nodo `param` que alimenta |
| `param` | (solo en fragmentos) | suma de entradas, o `params.default` si el padre no conecta nada |
| `return` | (solo en fragmentos) | suma de entradas: lo que devuelve el fragmento |

Curvas (`params`): `linear` a*x+b (a=1, b=0); `ramp` `Ramp(x, lo=0, hi=1)`; `sigmoid` 1/(1+e^(-k(x-mid))) (k=10, mid=0.5); `inverse` 1-`Ramp(x,lo=0,hi=1)`;
`threshold` x>=`at`(0.5) ? `high`(1) : `low`(0); `urgency` `Urgency(x, from=60, power=1)`; `falloff` `Falloff(x, half=30)`.
`ramp`, `urgency` y `falloff` llaman a las mismas funciones que usa la IA (`GB/AI/Utility.h`), no a copias.

Un fragmento (grafo incluible) tiene nodos `param` y un `return`, y no puede tener `output`. Puede leer entradas globales (`input`).

## 3. Evaluacion (funcion pura)

```cpp
CompiledPriorityGraph c;  CompilePriorityGraph(graph, resolver, c, &issues);   // valida, expande subgrafos, orden topologico (una vez)
PgResult r = EvaluatePriorityGraph(c, inputs);   // inputs: mapa nombre->float (+ resolutor opcional para nombres dinamicos)
r.Outputs[i] = { kind, mode, value, node };  r.Values = valor de cada nodo (para pintarlo y explicar)
```

Sin `Tribe`, sin reloj, sin azar: mismas entradas, mismas salidas (aristas y nodos en orden de lista, sin contenedores no ordenados). `ApplyPgMode` aplica
`replace/multiply/add` a una puntuacion.

## 4. Entradas disponibles

`option.score`, `option.distance` (m al lugar de la opcion), `option.commit`, `option.affinity` (afinidad con el objetivo si es una persona), `factor.<nombre>`
(cualquier factor registrado: `factor.hunger`, `factor.role`, `factor.distance`...), `role.weight`, `need.hunger|thirst|rest|warmth|safety|company|health|vigor`
(0..100), `danger` (= (100 - safety) / 100), `time.hour` (0..24), `time.night` (0/1), `season` (0 primavera .. 3 invierno), `agent.age` (anos), `agent.skill`
(aptitud de su oficio), `skill.<familia>`, `trait.<rasgo>`, `store.<objeto>` (existencias del almacen), `clan.members`, `fire.burning`.
El editor las lista con su ayuda (`PriorityInputCatalog()`).

## 5. JSON

`assets/data/priority_graphs/<id>.json`:

```json
{ "kind": "priority_graph", "version": 1, "id": "ejemplo", "name": "...", "description": "", "author": "", "tags": ["x"], "template": false, "revision": 1,
  "nodes": [ { "id": "s", "type": "input", "source": "option.score", "x": 0, "y": 0 },
             { "id": "o", "type": "output", "source": "*", "fn": "replace", "x": 200, "y": 0 } ],
  "edges": [ { "from": "s", "to": "o", "weight": 1.0 } ] }
```

`version` es el del esquema (hoy 1; uno mayor se rechaza con un mensaje claro, uno menor se migra al cargar). `x`/`y` guardan la disposicion del lienzo.
`assignments.json` (misma carpeta): `{ "kind": "priority_assignments", "version": 1, "default": "builtin", "roles": { "forager": "otro" }, "members": { "Ana": "otro" } }`;
manda el personaje, luego el rol, luego el defecto. Se cargan todos los `*.json` de la carpeta con `"kind": "priority_graph"`.

Grafos de serie: `builtin` (pasa la puntuacion tal cual: es el defecto y es exacto por construccion), `builtin_formulas` y `builtin_raw` (plantillas que
reconstruyen las formulas de comer, beber, dormir, calentarse, reagruparse y recolectar a partir de factores / necesidades en bruto; son un punto de partida, no el defecto:
no incluyen agenda, trabajo ni sociedad), `cautious` (ejemplo que usa el fragmento `fragment_need_urgency`).

## 6. Biblioteca (`PriorityLibrary`)

Guardar/cargar carpeta, `Duplicate`, `SaveAsTemplate`, `Instantiate` (una plantilla da un grafo nuevo), `ExportJson`/`ImportJson`, `Remove` (rechaza borrar lo que
esta asignado o incluido por otro grafo), asignaciones por rol / personaje / defecto, y `ValidateAll`.

Validacion (`ValidatePriorityGraph`, mensajes con el id del nodo): ids vacios o repetidos, tipo o funcion desconocidos, aristas a nodos que no existen,
entradas que no existen (`input 'x': no existe la entrada 'need.hungre'`), salidas de tipo desconocido, dos salidas del mismo tipo, **ciclos** (se nombra el camino
`a -> b -> a`), subgrafos inexistentes o recursivos (`A -> B -> A`), puertos que el fragmento no declara, fragmentos sin `return`; avisos para nodos sin uso o salidas sin entradas.

## 7. Enganche en la simulacion

Dos lineas marcadas `PRIORITY-GRAPH HOOK`:

1. `Engine/src/GB/Game/Tribe.h`, miembro publico `std::shared_ptr<PriorityLibrary> Priorities;` (vacio = sin grafos, no hace nada).
2. `Engine/src/GB/Game/Tribe.cpp`, en `Tribe::Decide`, justo antes de `// choose, and fall back...`: `if (Priorities) ApplyPriorityGraphs(*this, m, *Priorities, opts);`.

Si otro cambio mueve `Decide`, basta con conservar la llamada: entre el ultimo generador de opciones (`MaslowOptions`) y `ChooseOption`.
`ApplyPriorityGraphs` (`GB/Game/TribePriorities.cpp`) usa solo la API publica de `Tribe` (`NeedLevels`, `Hour`, `CurrentSeason`, `Store`, `People`, `RoleWeight`...).
Solo toca una opcion si su nueva puntuacion es distinta de la anterior (y entonces anade el factor `graph`), y el grafo identidad (`builtin`) ni se evalua,
salvo que `PriorityLibrary::ForceEvaluate` este activo (tests).

## 8. Editor: ventana "Priority Graph"

Pasos exactos:

1. Abre el Editor (`Editor.exe`) y el menu **View > Priority Graph** (o `panel` no hace falta: es una ventana flotante que puedes acoplar donde quieras).
   Al abrirla por primera vez carga `assets/data/priority_graphs/` y activa la biblioteca en la tribu que estas mirando (con `builtin` no cambia nada).
2. Crea la tribu como siempre (`tribe build`, o carga una partida) y selecciona un personaje en el panel **Tribe** (o con un clic en el visor).
3. Columna izquierda, **Library**: elige un grafo. Escribe un id en el cuadro y pulsa **New** (copia de `builtin`), **Duplicate**, **Rename**, **As template** (marca el grafo
   como plantilla), **From template** (crea un grafo desde la plantilla elegida), **Delete**, **Export** (muestra el JSON y lo copia al portapapeles), **Import** (pega JSON).
   **Save graph** guarda `<id>.json`; **Save all** guarda todo y `assignments.json`; **Reload folder** relee la carpeta.
4. **Who uses it**: con un grafo elegido, **Default** (todos los que no tengan otro), **Character <nombre>**, **Role <rol>** o escribe un rol y **Assign to role**.
   Las asignaciones aparecen debajo con una `x` para quitarlas. Un grafo con errores se rechaza al asignarlo, y si lo rompes despues la tribu lo ignora hasta arreglarlo.
5. Lienzo (centro): rueda = zoom; arrastrar el fondo (o boton central) = desplazar; arrastrar un nodo = moverlo; arrastrar desde el **punto azul derecho** de un nodo
   hasta otro nodo = conectar; **clic derecho** en el fondo = anadir nodo (Input con ayuda por entrada, Constant, Curve, Combine, Clamp, Output, Include a graph...);
   clic en un nodo o una conexion = seleccionarlo; **Supr** = borrarlo. Debajo del lienzo salen los errores y avisos de validacion con el nodo implicado.
6. Panel derecho: **Graph** (nombre, descripcion, autor, plantilla), **Selected** (propiedades del nodo: entrada, curva y sus parametros, combinador, opcion y modo de la
   salida, grafo incluido; o el peso y el puerto de la conexion) y **Preview**:
   * la tabla de las opciones del personaje: lo que la tribu les dio (`tribe`), lo que daria el grafo que estas editando (`this graph`), el valor de la salida, `>` lo que eligio
     y `*` lo que elegiria este grafo (sin histeresis); pasa el raton por una opcion para ver su nota y sus factores (el *por que*), y haz clic para ver en el lienzo
     el valor de cada nodo con las entradas de esa opcion;
   * **What if (sliders)**: marca la casilla y aparecen deslizadores para cada entrada del grafo (precargados con los valores del personaje) y `option.score`;
     el lienzo y la lista de resultados se recalculan al momento.
7. Prueba rapida: asigna `cautious` a un rol, deja correr la simulacion y mira en Preview las opciones `explore` / `regroup` y el factor `graph` de sus factores.

Por consola no hay comandos nuevos: el motor expone `PriorityLibrary` para scripts y tests (`GBPriorityGraphTests`).

## 9. Que no cubre

* Un grafo no crea opciones nuevas ni cambia como se ejecutan; solo puntua las que genera `Tribe::Decide`. Los pesos por rol (`Roles`), la agenda y el tablon de trabajos
  siguen existiendo y ya estan dentro de `option.score`; el grafo se aplica despues.
* Las asignaciones no se guardan dentro de la partida (`save`): viven en `assignments.json`.
* La histeresis (`ChooserConfig`) sigue siendo comun.
