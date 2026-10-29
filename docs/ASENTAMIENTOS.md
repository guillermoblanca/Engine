# Construcción y asentamientos (H8, E9)

El clan elige dónde vivir, planea qué construir según lo que le falta, lleva los materiales a la obra y la levanta fase a fase con la habilidad `Build`.
Todo es CPU y determinista; los edificios son datos y las obras, entidades con su estado en etiquetas y atributos, así que se guardan con la partida.

[← Volver al README](../README.md) · relacionado: [TRIBU.md](TRIBU.md) (la tribu y su tablón), [RECURSOS_MUNDO.md](RECURSOS_MUNDO.md) (nodos repartidos),
[PAQUETES.md](PAQUETES.md) (secciones y validador), [GUARDADO.md](GUARDADO.md)

## Qué hay

| Pieza | Dónde | Qué es |
|---|---|---|
| `BuildingDef`, `LoadBuildings`, `DefaultBuildings` | `GB/Gameplay/Buildings.h` | El edificio como datos: huella, fases (materiales, trabajo y aspecto), lo que da y una tecnología opcional |
| `BuildSite`, `ReadSite`, `DeliverToSite`, `WorkOnSite` | `GB/Gameplay/Buildings.h` | La obra: una entidad con sistema de habilidades; una pieza visible por fase que crece |
| `BuildingSystem` | `GB/Gameplay/Buildings.h` | Escucha `Build` en cada obra (también las que llegan con una carga) y mueve las piezas |
| `ScoreSite`, `RankSites`, `BestSite` | `GB/Gameplay/SiteChoice.h` | La elección del sitio del campamento, sobre `ResourceGround` (sin escena ni GL) |
| `ClanSiteWeights` | `GB/Game/Tribe.h` | Lo que un clan quiere tener cerca: el peso medio de sus roles por tipo de nodo; la comida, la mitad más |
| `SettlementSiteChooser` | `GB/Game/Tribe.h` | El elegidor de sitio de la migración (H12, `Tribe::SetSiteChooser`) con la misma puntuación y lo que el clan conoce |
| El planificador, las opciones `deliver` y `build` y los trabajos del tablón | `GB/Game/Settlement.cpp` | Miembros de `Tribe` fuera de `Tribe.cpp`, que solo los llama desde unos pocos ganchos |

## Edificios como datos

La sección `buildings` de `assets/data/era1.json` (la lee la sección `items` del paquete, que los comprueba junto con los objetos; ver [PAQUETES.md](PAQUETES.md)).
`DefaultBuildings()` dice lo mismo y un test obliga a que coincidan.

```json
"shelter": {
  "name": "Shelter", "footprint": [ 3.0, 3.0 ],
  "phases": [
    { "name": "foundation", "work": 6.0, "needs": { "stone": 3 }, "part": "slab" },
    { "name": "frame", "work": 12.0, "needs": { "branch": 6 }, "part": "posts" },
    { "name": "roof", "work": 10.0, "needs": { "reed": 4, "fiber": 3 }, "part": "roof" }
  ],
  "shelter": 3, "rest": 0.2, "warmth": 0.3
}
```

- `phases`: en orden; cada una pide sus materiales (`needs`), sus segundos de trabajo (`work`) y su aspecto (`part`: `slab`, `posts`, `walls`, `roof`, `table`, `rack`).
- Lo que da cuando está en pie: `shelter` (camas), `rest` y `warmth` (descanso y abrigo por segundo a quien duerme dentro), `store` (un tipo de almacén secundario de la economía, H18: `stockpile`)
  y `station` (las recetas con esa estación se pueden hacer allí).
- **Tecnologías (H11).** Un edificio está bloqueado por las tecnologías que lo nombran en su lista `buildings` (`TechDef::Buildings`, por id o por nombre) y por su propio
  `tech` (opcional). El clan puede planearlo cuando un miembro vivo conoce una de ellas (`ClanKnowledge`), o si nada lo bloquea. Sin tecnologías cargadas decide
  `Tribe::SetTechCheck` (sin ella, uno bloqueado nunca se planea). El validador, con tecnologías, solo da por construible un edificio cuyo desbloqueo se puede descubrir
  (y es un problema una tecnología que desbloquea un edificio que no existe); sin ellas, un `tech` es solo una nota. Los edificios de la era 1 no están bloqueados.
- Cargar es atómico: un edificio mal escrito rechaza el documento entero (también sus objetos). Se rechazan fases sin nombre o repetidas, cantidades no positivas,
  piezas desconocidas e ids que no pueden ser etiqueta.

| Edificio | Fases (materiales, trabajo) | Da |
|---|---|---|
| `shelter` (refugio) | cimiento (3 piedra, 6 s), armazón (6 ramas, 12 s), cubierta (4 juncos y 3 fibra, 10 s) | 3 camas, +0,2 descanso y +0,3 abrigo por segundo durmiendo dentro |
| `storage` (almacén) | suelo (4 piedra y 2 madera, 8 s), paredes (6 ramas, 10 s), techo (4 juncos, 8 s) | un almacén secundario (`stockpile` de H18) junto al campamento |
| `workbench` (banco de trabajo) | banco (3 madera y 2 piedra, 8 s) | estación `workbench` |
| `drying_rack` (secadero) | armazón (4 ramas, 6 s), rejilla (3 fibra, 6 s) | estación `drying_rack` (bayas secas, adobe de `era1_ampliado`) |

Los costes de la propuesta (tablones, ladrillos, cordel) se han cambiado por materiales que la tribu de la era 1 ya recoge sin fabricar nada (piedra, madera, ramas,
juncos y fibra): la tribu todavía no fabrica productos intermedios. Corral, taller y horno (E10) son unas líneas más de datos cuando haya qué hacer con ellos.

## La obra

Una obra es una entidad `<Nombre> <n>` (`Shelter 1`) con su punto de interacción `work` delante y una pieza por fase (oculta hasta que empieza su fase). Su estado:

- etiquetas `Building`, `Building.Kind.<id>`, `Building.Site` mientras se construye y `Building.Done` cuando está en pie, y `Building.Ready` cuando la fase actual tiene todos sus materiales;
- atributos `Phase` (0..fases), `Work` (segundos de trabajo en la fase actual) y `Delivered.<objeto>` (lo que ya se ha traído de la fase).

`DeliverToSite` acepta solo lo que le falta a la fase. `Build` (habilidad: objetivo con `Building.Site` y `Building.Ready`, 2 s, montaje `plant`) envía `Event.Building.Work`; el
`BuildingSystem` suma `WorkPerUse` (2 s) y, al completar el trabajo de la fase, empieza la siguiente. La pieza de la fase actual crece con lo traído (un quinto) y con el trabajo.
Todos los actores tienen `Build` concedida (como `Stoke` o `DrawWater`).

## Qué construye el clan y cómo

Cada `PlanSeconds` (10 s), con la hoguera hecha y las herramientas listas (como antes), y si hay menos de `MaxSites` (1) obras abiertas, `BuildingNeeds()` puntúa:

| Necesidad | Urgencia |
|---|---|
| Camas para todos (cuentan también los refugios a medias) | 3 × fracción de miembros sin cama |
| El almacén principal está lleno de algo (`StoreCap() - 2`, H18) y no hay ya un almacén | 2 |
| Una receta necesita una estación que no hay (la herramienta de un rol del clan, o una cuyas entradas tiene el almacén) | 1,5 |
| El campamento solo tiene su banco improvisado | 0,5 |

Solo cuenta lo que el clan puede pagar (cada material está en el almacén o lo da algún nodo). El sitio: anillos alrededor del campamento (`SiteRadius` 7 m, cada 3 m), cada edificio en su ángulo,
lejos del centro, de la hoguera, de los demás edificios y de los nodos de recurso, y donde un miembro puede estar (también las esquinas y la entrada). El registro dice
`the clan starts Shelter 1 at x, z (3 stone, ...): beds for 0 of 4`.

Los miembros tienen dos opciones nuevas (`SettlementOptions`):
- **`deliver`** (llevar a la obra; `haul` es el acarreo de H18 entre almacenes): coge del almacén lo que le falta a la fase (lo que nadie lleva ya, hasta `HaulAt` = 6 unidades) y lo lleva a la obra; al llegar lo entrega.
- **`build`**: con la fase lista, `ability Build <obra>` tres veces; como mucho `Builders` (2) a la vez.

Puntúan con el peso del rol en `build` (o 0,6 si es menor: todos ayudan; un rol constructor puede nombrar `build` más alto) y la agenda del trabajo (la de `gather`: de noche menos).
La recolección de lo que una obra necesita recibe un empujón (`helps with a building`), igual que lo que le falta a una herramienta.

**En el tablón (H7).** `PostJobs` publica `deliver <obra>` (huecos por cada 6 unidades) y `build <obra>` (hasta `Builders`), con prioridades de datos (`work.jobs.priority`: `deliver` 1,1 y `build` 1,3)
y la familia de aptitud `building` (`work.skills.families`: `build` y `deliver`). Trabajar una fase entrena `building` (2 por uso; acarrear 0,5 por unidad) y quien más construye
más lo prefiere. Lo que falta en las obras se suma a lo que piden los trabajos `gather`. Consultas para el tablón u otros sistemas: `BuildingNeeds()` («el clan necesita un edificio y por qué»)
y `SiteDemand()` («las obras necesitan N de X que nadie trae»).

**Con la economía (H18).** Un almacén terminado se convierte en un almacén secundario (`AddStockpile` con el tipo de su `store`; la economía lo guarda): los miembros dejan allí lo
que recogen cerca y los acarreadores lo llevan al principal. El tope del almacén principal es el de la economía (`StoreCap()`, `economy.logistics.stores.main.cap`). Las estaciones de
los edificios terminados se dan a la economía (`SetStations`: delante del edificio), así que sus cadenas de producción pueden usar el secadero; las del campamento
(`economy.production.campStations`: el banco) van primero. Lo que les falta a las obras abiertas se pasa como demanda (`SetDemand("site <obra>", objeto, unidades)`, cada
`PlanSeconds` y retirándola al completarse): el plan de producción hace lo que puede y los trabajos de recolección piden el resto. El campo de H18 sigue a su desplazamiento fijo
del campamento (`economy.farm.offset`): no se elige con el sitio.

**Lo que dan.** Al dormir, cada miembro tiene su cama
(`SleepSpot`: los refugios por nombre, las camas a los vivos por orden) y, dentro, recibe el descanso y el abrigo del edificio. Para la sociedad (H9), un refugio terminado
cuenta como abrigo para los nacimientos igual que la hoguera (`Tribe::Beds() > 0`); un clan que crece pide más refugios porque faltan camas.

**Estaciones (decisión).** Se mantiene el comportamiento de antes: el campamento cuenta como banco de trabajo (`Config.Econ.Production.CampStations = { "workbench" }`, H18; el paquete `era1` sigue
declarando `stations: [ "workbench" ]`). Cuando hay un banco construido las herramientas se fabrican **en él** (el miembro va allí); una estación que solo da un edificio
(`drying_rack`) existe cuando el edificio está en pie (`StationAvailable`, `CraftStation`). Así no cambia nada para una tribu sin edificios y no hay un periodo en que la
tribu no pueda hacer su primera hacha.

Sin edificios (`Tribe::SetBuildings` no llamado) nada de esto ocurre: la tribu se comporta como antes (lo comprueba el test).

## La elección del sitio

`ScoreSite(ground, recursos, pesos, punto)` (devuelve un `SiteRating`) suma, cada parte entre 0 y 1 con su peso (`SiteWeights`):

| Parte | Peso | Cómo |
|---|---:|---|
| Agua | 1 | lo mejor a `WaterBest` (20 m); más cerca baja un poco; nada a `WaterFar` (120 m) |
| Recursos | 1,5 | por tipo de nodo en `ResourceRadius` (40 m), contando más los cercanos; un tipo cuenta entero con `Enough` (3); ponderado por lo que el clan quiere (`Want`) |
| Llano | 0,8 | 1 − pendiente media / `MaxSlope` sobre el suelo del campamento (centro y dos anillos hasta `FlatRadius`, 6 m) |
| Bioma | 0,6 | pradera 1, bosque 0,7, orilla 0,5, dunas 0,25, roca 0,2 |
| Abrigo | 0,3 | árboles y rocas del mundo a menos de 18 m (6 = abrigo completo) |

Se descarta: en el agua o a menos de `WaterNear` (4 m), agua dentro del campamento, pendiente mayor que `MaxSlope` (14°) en algún punto, un bioma que no está en la tabla (nieve,
agua), un árbol, roca o nodo a menos de `ClearRadius` (4 m), o a menos de `Edge` (16 m) del borde. `RankSites` recorre una rejilla de `Spacing` (8 m) y ordena por puntuación (empates
por x y z): misma entrada, mismo sitio. Con un mundo de 192 m cuesta unos milisegundos.

**`tribe build` sin coordenadas** en un mundo generado elige el mejor sitio con los nodos de la escena y los roles que el clan tendrá (`PlanRoles`); con coordenadas, o sin
mundo, como antes. Y **no pone su parche de recursos** si hay 8 o más nodos (los `wild` de `resources world`) a menos de 40 m del campamento: el parche queda para los mundos
vacíos y planos de las pruebas.

**Migración (H12).** El editor da a la tribu `SettlementSiteChooser` (`Tribe::SetSiteChooser`) cada vez que le da las regiones del mundo: cuando el clan decide mudarse,
la misma `ScoreSite` puntúa el mapa, pero con los nodos que **el clan conoce** (su memoria compartida) y no con todos los de la escena; se descartan los sitios a menos de un
radio de casa, sin nada conocido cerca o a los que nadie llega, y decide la medida de H12 (`Tribe::SiteScore` debe superar la de casa en `MinGain`), así que la regla de «vale la
pena mudarse» sigue siendo la de H12. Al fundar (`tribe build`) el clan aún no conoce nada y se usan todos los nodos de la escena.

**Varios clanes (H13).** Cada clan construye (el editor da a cada tribu sus edificios, su suelo y el elegidor de migración). Sus edificios terminados alrededor del
campamento se cuentan en `Clan::Buildings` (`Tribe::BuildingCount()`, cada tic del editor) y hacen más fuertes sus reclamaciones de territorio. Un clan nuevo (`clan new`
sin sitio) se asienta donde dice `SettlementSiteChooser` con todos los nodos de la escena (`ClanWorld::SetSiteChooser`), si queda lo bastante lejos de los demás campamentos.

**Los edificios se quedan atrás (decisión).** Solo cuentan como del clan los edificios y obras a menos de `SettlementRadius` (40 m) del campamento. Al mudarse, los de antes
quedan en el mundo (como ruinas que se guardan con la partida), dejan de dar camas y estaciones (su almacén secundario sigue en la economía hasta que se vacía), y una obra a medias se abandona; el planificador vuelve a pedir un
refugio en el sitio nuevo y construye allí (`Shelter 2`...). Trasladar materiales o desmontar queda para más adelante. Si el clan volviera, sus edificios contarían otra vez.

## Editor

```
site best | score [x z] | list [n]          dónde se asentaría un clan (y por qué: agua, recursos, llano, bioma, abrigo, nodos cerca)
settlement status                           obras (fase, trabajo, lo que falta), edificios en pie, lo que el clan quiere después y el tope del almacén principal
settlement start <edificio> [x z]           pone una obra a mano; el clan la construye como las suyas
settlement buildings                        los edificios cargados
```

El panel **Tribe** tiene la sección «Buildings (settlement)» y `tribe status` termina con el mismo informe. 

## Guardado

Las obras y los edificios son entidades del usuario: van en `user-entities` con su componente de juego (fase, trabajo, entregas, etiquetas) y sus piezas (visibles y escaladas).
Al cargar, `BuildingSystem` vuelve a escuchar cada obra. La tribu guarda además el reloj del planificador (`plan`, opcional: una partida anterior carga sin él) y cada miembro su
trabajo en curso (`deliver`/`build` con la obra), que se vuelve a ordenar como cualquier otro. No cambia `kFormatVersion`.

## Pruebas

**QA de la simulación (H20).** `SimInvariants()` tiene el invariante `buildings`: fase y trabajo dentro de sus datos, nada entregado por encima de lo que pide la fase,
etiquetas según el estado, progreso que nunca retrocede y ninguna obra en agua profunda. `SimSetup::Buildings` hace que el arnés sin ventana (`SimRun`) construya; el test
`settlement` corre dos días en dos mundos generados con edificios y sin ninguna violación. La demo del editor `editor_demo_h8` elige sitio, pone una obra, guarda y carga a
mitad y pasa `qa check`.

`tests/settlement_test.cpp` (CPU): los datos (`era1.json` = código, ida y vuelta, errores sin cambios), una obra fase a fase con `Build` (rechazado sin materiales, usos exactos,
piezas), la elección del sitio en un terreno sintético (no en el lago, ni en su orilla, ni en la ladera, cerca de lo que el clan quiere) y en mundos generados (templado y
alpino, tres semillas: determinista, junto al agua, llano y con recursos), una tribu de cuatro que construye su refugio sola (y una segunda ejecución idéntica; sin edificios no
construye nada), el tablón con `deliver` y `build`, y una obra guardada y cargada a mitad. `packs` comprueba en estricto que `era1` y `era1_ampliado` siguen limpios con los edificios
(el secadero da la estación `drying_rack`, así que las bayas secas y el adobe ya no son huecos), que un edificio impagable es un problema y, con tecnologías, que uno cuya
tecnología no se puede descubrir también; `knowledge` carga los paquetes con tecnologías y edificios a la vez. El test también muda el campamento y comprueba que el refugio se
queda atrás y el clan quiere otro.

## Límites y siguientes pasos

- Una obra cada vez (`MaxSites`); sin territorio ni mapa de influencia; los edificios no son obstáculos de la navegación ni se nivela el suelo (E12).
- Una repetición del editor (`replay record` y `replay play` en el mismo mundo) de una tribu que trabaja 300 tics no termina idéntica, con o sin obra y en suelo plano o generado
  (comprobado en esta rama con la tribu sola): no es de H8, pero por eso la demo `editor_demo_h8` guarda y carga la partida con la obra a medias y comprueba los invariantes
  (`qa check`, con el de edificios) en vez de grabar una repetición.
- Al migrar no se llevan materiales ni se desmonta nada; las regiones de H12 no se usan aún como candidatos (la rejilla de 8 m cubre el mapa).
- El aspecto son primitivas; no hay animaciones de obra (se usa `plant`).
- El campo de H18 no se coloca con la elección de sitio; el almacén secundario no cambia su conservación por ser un edificio (lo dice su tipo en la economía).
- E10 (corral, taller, horno, hogar comunal) es solo datos cuando haya animales, forja y brasas.
- La aceptación de H8 (refugios, almacén y campo en 30 días con cinco semillas) queda pendiente del campo (E7) y de un barrido largo (H20).
