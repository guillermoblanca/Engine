# Exploración, expediciones y migración (H12)

Un clan ya no conoce solo los alrededores de su campamento: el mundo generado se corta en **regiones** con nombre, el clan va sabiendo qué hay en cada una
a medida que sus miembros las recorren y lo cuentan en el campamento, **manda expediciones** cuando necesita algo que nadie sabe dónde encontrar y **se muda**
cuando su tierra se agota. Todo es CPU, determinista y sin ventana; las reglas son datos.

[← Volver al README](../README.md) · relacionado: [RECURSOS_MUNDO.md](RECURSOS_MUNDO.md) (los nodos repartidos que dan el motivo), [TRIBU.md](TRIBU.md) (el tablón de trabajos),
[IA.md](IA.md) (memoria y memoria de clan), [mundo.md](mundo.md) (el mapa de biomas horneado)

## Qué hay

| Pieza | Fichero | Qué es |
|---|---|---|
| `RegionMap`, `RegionSummary` | `GB/World/Regions.h` | El corte del mundo en regiones y el resumen de cada una |
| `ExplorationRules` | `GB/Game/Exploration.h` | Las reglas (sección `exploration` de `era1.json`: `regions`, `expeditions`, `migration`) |
| `ClanAtlas`, `RegionLore` | `GB/Game/Exploration.h` | Lo que el clan sabe de cada región |
| `Expedition`, `ExpeditionRecord`, `MigrationRecord`, `ExplorationState` | `GB/Game/Exploration.h` | La expedición en curso, el historial y las mudanzas (se guardan) |
| La parte de la tribu | `GB/Game/TribeExplore.cpp` | Reloj, opciones de los miembros, expediciones, puntuación del sitio y mudanza |
| Editor | `Editor/src/EditorExplore.cpp` | `region list | info <id|nombre> | explore` |

## Regiones

`RegionMap::Build(GeneratedWorld, RegionRules, cosas)` corta el mapa de biomas horneado (`GeneratedWorld::Maps`, 1 m):

1. Baldosas de `tile` metros (30: 12 × 12 en el mapa de 360 m). Cada una toma el bioma de la mayoría de sus muestras (agua si más de `waterShare` es agua).
2. Baldosas vecinas (4-conexas) del mismo bioma dentro del mismo bloque de `blockTiles` × `blockTiles` forman un grupo: un bosque que cubre medio mapa son varias regiones.
3. Un grupo de menos de `minTiles` baldosas se une al vecino con el que comparte más borde (empate: el de id menor), hasta que no queda ninguno. Por eso una
   región puede pasar un poco del bloque.
4. Se numeran por su primera baldosa (fila a fila) y se nombran por dónde caen respecto al centro (`North` es −z, `East` es +x; `Central` cerca del centro) y por su bioma
   dominante con la palabra de `names` (`Forest` → `Woods`): «North Woods», «Central Meadows», «Southeast Lake». Un segundo nombre igual lleva un número.

Cada `RegionSummary` lleva: área, límites, centroide y **ancla** (el suelo seco más cercano al centroide: adonde va una expedición), reparto de biomas, parte de agua,
parte del suelo seco junto al agua (`nearWater`), distancia al agua desde el ancla, **nodos salvajes por tipo** (de `PlanWorldResources`), vecinas y **peligro**:
la media de `danger` por bioma más `predatorDanger` por cada depredador que vive en ella ahora (`SetPredators`; el editor cuenta cada 10 s los animales de las especies
que cazan, ver [ECOLOGIA.md](ECOLOGIA.md)). Además, una rejilla fina de **celdas de exploración** (`scoutCell`, 10 m) es lo que un clan marca como visto.

**No se guardan**: salen de la semilla, la versión del generador y la densidad de los nodos salvajes (`WorldParams`), igual que la línea base. Medido (test `exploration`):
templado 5, alpino 3 y desierto 2 dan entre una docena y una treintena de regiones; cortar un mapa cuesta lo mismo que leer el mapa de biomas un par de veces.

## Lo que el clan sabe (atlas)

Cada `seeEvery` segundos (2) cada miembro anota las celdas que tiene a la vista (alcance de la vista, 28 m) en su lista pendiente. Cuando cuenta lo que sabe en el
campamento (`Share`, la misma ocasión en que mezcla su memoria con la del clan) esas celdas pasan al atlas del clan, y con ellas: la **parte explorada** de cada región
(celdas vistas / celdas de la región), la **última visita** y, de la memoria del clan, **los nodos que conoce allí** por tipo. La memoria de un nodo caduca
(`Lifetime`, 2400 s); el atlas recuerda lo último que supo de la región.

Arreglo de paso: lo que un miembro ve que ya no está (talado, agotado del todo) se olvidaba solo en su memoria y el clan se lo volvía a enseñar. Ahora lo cuenta en el
campamento y el clan también lo olvida (`TribeMember::Gone`).

## Expediciones (la escasez mueve a explorar)

Cada `every` segundos (10) el clan mira su **tablón** ([TRIBU.md](TRIBU.md)): un trabajo `gather <objeto>` que nadie puede coger porque **nadie conoce un nodo de ese objeto**
(ni el clan ni ningún miembro), y que alguno podría recoger (tiene la herramienta que pide el nodo), acumula tiempo. A los `scarcity` segundos (180) el clan elige región:

```
probabilidad = 1 si el atlas ya vio nodos del objeto allí;
               si no, la parte de su suelo con los biomas en que crece el nodo (spawn de los datos) × lo que queda sin explorar
viaje        = 1 / (1 + distancia al ancla / travelScale)          (150 m; más allá de maxDistance, 400 m, no vale)
riesgo       = max(0,05, 1 − riskWeight × peligro)
valor        = probabilidad × viaje × riesgo                       (se descartan las regiones donde ya se buscó en vano hace menos de retry, 900 s)
```

y publica en el tablón **`expedition <objeto>`** (prioridad `priority`, 1,3; `maxParty` plazas). Los miembros la reclaman como cualquier trabajo, por su gusto por recoger ese objeto
(rol y aptitud), su `Vigor` y su salud. Con `minParty` (2) reclamaciones, o con las que haya pasados `gather` segundos (120), **sale**: cada uno coge `food` (2) de comida del almacén
y tiene la opción `expedition` (puntuación `score`, 1,4, más el ×1,6 del tablón): ir a cada uno de los `scoutPoints` (3) puntos de la región (el ancla y las baldosas más alejadas entre
sí, recorridos del más cercano al campamento), mirar alrededor y **volver**. Las necesidades siguen mandando: quien tiene hambre come lo que lleva, y la noche puede traerlo a dormir.
Al volver cuenta lo que vio; cuando ha vuelto toda la partida (o pasados `maxSeconds`, 1500 s, vuelven) el clan registra el resultado en el historial y en el registro:
`the clan hears from the expedition to North Woods: 2 clay_bank (clay)`. Desde ese momento el nodo está en la memoria del clan y el tablón ya se lo ofrece a quien sabe recogerlo.

Si no la reclama nadie en `3 × gather` segundos, se anula. Una expedición en curso se ve en `region explore`, `tribe log` y el inspector (opción `expedition`).

## Migración

Cada `every` segundos (30, en la sección `migration`) el clan juzga su **hogar** (`homeRadius`, 60 m alrededor del campamento), salvo los primeros `settle` segundos (600) de un
campamento nuevo. El valor de un sitio (`Tribe::SiteScore`) sale de lo que el clan conoce: por cada objeto que dan los nodos, cuántos nodos con algo (o que vuelven a crecer)
conoce a menos de `homeRadius` (hasta 3), la comida pesa el doble; +0,3 con agua (un pozo conocido o el agua a menos de `waterLost`, 60 m); −0,5 × peligro de la región.

El hogar **va mal** si no se conoce comida cerca, si no hay agua, o si vale menos de `poor` (0,25) × lo mejor que valió. Ese tiempo se acumula (y se descuenta `recover` veces más
rápido cuando va bien); a los `after` segundos (3600, unas tres cuartas partes de un día) el clan pide un sitio al **elector de sitios**:

- `Tribe::SetSiteChooser(CampSiteChooser)` deja que H8 (elección de lugar) ponga el suyo: recibe `CampSiteRequest` (el campamento, el radio, lo que vale ahora y lo que el clan quiere)
  y devuelve `CampSite` (dónde, cuánto vale, región, por qué).
- Sin él, `ChooseCampSite`: el ancla seca de cada baldosa de las regiones que el clan ha visto, a más de `homeRadius` del campamento y alcanzable, con su `SiteScore`.

Si el mejor vale al menos `minGain` (0,25) más que el hogar, el clan **se muda** (`MoveCamp`): el campamento (y con él el almacén, que es suyo) pasa al sitio nuevo, la hoguera
se vuelve a construir allí con el combustible que tenía, los sitios de cada miembro se eligen de nuevo, una expedición en curso vuelve, y cada miembro tiene la opción `migrate`
(`score`, 1,8) hasta que llega (`arrive`, 8 m). Si no conoce un sitio mejor lo dice y vuelve a mirar más tarde (las expediciones siguen buscando). Entre dos mudanzas pasan al
menos `cooldown` segundos (7200).

## Datos

```json
"exploration": {
  "regions":     { "tile": 30, "blockTiles": 4, "minTiles": 3, "waterShare": 0.5, "scoutCell": 10, "nearWater": 12, "predatorDanger": 0.15,
                   "danger": { "Forest": 0.25, "Rock": 0.35, ... }, "names": { "Forest": "Woods", ... } },
  "expeditions": { "every": 10, "scarcity": 180, "minParty": 2, "maxParty": 3, "gather": 120, "scoutPoints": 3, "travelScale": 150, "maxDistance": 400,
                   "riskWeight": 1, "retry": 900, "maxSeconds": 1500, "priority": 1.3, "score": 1.4, "food": 2, "seeEvery": 2 },
  "migration":   { "every": 30, "homeRadius": 60, "settle": 600, "poor": 0.25, "waterLost": 60, "after": 3600, "recover": 2, "minGain": 0.25,
                   "cooldown": 7200, "arrive": 8, "score": 1.8 }
}
```

`DefaultExplorationRules()` dice lo mismo (un test lo obliga). Cada parte sustituye esa parte entera; un error deja todo como estaba, también dentro de un paquete
(sección `exploration`, `AddExplorationContentSection`, que el editor registra junto a las demás).

## Guardado

Va en la sección `ai` de la tribu, bajo `explore` (todo opcional: una partida anterior carga sin atlas ni expedición): las celdas vistas (hexadecimal), el atlas por región,
lo pendiente de contar de cada miembro, la expedición en curso (partida, por qué punto va cada uno, los puntos), el historial, los relojes de escasez, las búsquedas en vano y el
estado del hogar y de la mudanza. Las regiones no se guardan (ver arriba); si el corte no casa con el guardado (otro mundo), el atlas empieza de cero.

## Editor

```
region list                    una línea por región: id, nombre, bioma, hectáreas, agua, nodos salvajes, peligro
region info <id|nombre>        el resumen entero, y lo que el clan sabe de ella
region explore                 el hogar (y si va mal), la expedición, el historial, las mudanzas y el atlas del clan
```

Las regiones se rehacen solas cuando cambia el mundo, la densidad de `resources world` o las reglas, y se dan a la tribu en cada paso de la simulación.

## Pruebas

`tests/exploration_test.cpp` (test `exploration`, CPU): la sección de `era1.json` dice lo mismo que el código, va y vuelve y un error no cambia nada (también en un paquete); en un
suelo hecho a mano, el bosque y la pradera son regiones de su bioma, con su nombre, sus nodos, el área completa y el peligro de sus depredadores; en templado 5, alpino 3 y
desierto 2 las regiones cubren cada baldosa y cada celda una vez, son de una pieza, tienen nombre único, el ancla en seco y dentro, cuentan todos los nodos salvajes, la misma
semilla da el mismo corte y otra uno distinto; un clan de cuatro en templado 5 manda una expedición de dos o tres que vuelve con lo que faltaba y luego lo recoge, y no se muda;
una partida guardada con la expedición fuera carga igual y el clan cargado la trae de vuelta; una partida sin `explore` carga; un clan con hogar bueno no se muda en media hora con
reglas impacientes, y uno cuyo hogar se ha pelado (sin nodos a 75 m) se muda lejos, con la hoguera, y sus miembros llegan.

## Límites y siguientes pasos

- **Mapa único de 360 m**: las regiones son la base del streaming de H16 pero aún no se cargan por zonas; con mundos de kilómetros habrá que construirlas por trozos.
- El **pozo** no se muda con el campamento (es un interactivo de la escena): el sitio nuevo puntúa el agua cercana y los miembros recuerdan el pozo viejo.
- Fundar un **asentamiento nuevo** con una parte del clan (la aceptación de H12) y mantener el contacto necesita varios clanes o almacenes (H13, E11); hoy se muda el clan entero.
- Los **riesgos del camino** son el peligro medio de la región destino; no hay emboscadas, clima ni otros clanes en ruta. Los depredadores no atacan aún a los miembros.
- Los miembros solo cuentan lo que vieron en el campamento: dos que se encuentran fuera no intercambian mapa (como la memoria, ver [IA.md](IA.md)).
- Sin capa de depuración en el viewport (las regiones solo en texto): sería barato pintar el borde de las baldosas sobre `ai debug`.
