# QA de la simulación (H20)

El barrido de semillas de `sweep_test` comprueba que el **generador** hace mundos sanos. Este documento trata del barrido de la **simulación**: tribus que viven
días enteros sobre mundos generados, sin ventana, comprobadas paso a paso contra una tabla de invariantes, con una línea de métricas por partida. La misma tabla
la usan el test `sim_sweep`, la orden `qa sim` del editor y el panel **QA**, que la comprueba en vivo mientras la escena corre. Además, cada hito tiene su demo con
guion (principio 7 del plan): ver el final.

[← Volver al README](../README.md) · relacionado: [qa.md](qa.md) (QA de imagen y barrido del generador), [TRIBU.md](TRIBU.md), [IA.md](IA.md), [NAVEGACION.md](NAVEGACION.md)

## Qué hay

| Pieza | Dónde | Qué es |
|---|---|---|
| Tabla de invariantes | `SimInvariants()` en `Engine/src/GB/Game/SimQA.h` | Cada invariante es un nombre, una promesa y una función que mira un `SimView` (tribu, sistemas de habilidades, escena, suelo) y anota lo que está mal |
| Tabla de métricas | `SimMetricTable()` | Funciones que añaden métricas al final de una partida (`SimMetrics::Extra`) |
| Monitor | `SimMonitor` | Ejecuta la tabla en cada paso, agrupa los hallazgos por invariante y sujeto (un miembro metido en el lago una hora es un hallazgo visto muchas veces, no miles) y lleva las métricas |
| Cuerpo de prueba | `SimBody` | Un `ITribeBody` que ejecuta el mismo texto de tareas que un actor (`goto`, `wait`, `ability`) con un `AbstractAgent` que sigue los caminos de la **rejilla de navegación real** con el perfil del miembro: rodea el agua y los muros, y una meta inalcanzable falla como la del actor |
| Arnés | `SimRun` | Mundo generado en CPU (`WorldGenerator::GenerateData`), nodos repartidos por los datos (`PlanWorldResources`), rejilla de navegación con los troncos y rocas del mundo, pozo, campamento en el punto llano del centro, y una tribu como la de `tribe build` (nombres de los datos y oficios de `PlanRoles`). Determinista: `Fingerprint()` |
| Test | `tests/sim_sweep_test.cpp` (`ctest -R sim_sweep`) | El barrido: semillas × biomas × tamaños de tribu × días |
| Editor | `qa sim|check|status|reset|expect`, panel **QA** (`Editor/src/EditorQA.cpp`) | La misma tabla sobre la escena actual |

Todo es CPU y determinista: la misma semilla y el mismo arnés dan el mismo final paso a paso (el test lo comprueba repitiendo la primera partida).

## Invariantes

| Nombre | Promesa |
|---|---|
| `counts` | Ningún nodo, almacén ni mochila tiene una cantidad negativa o vacía, y ningún nodo más que su capacidad |
| `claims` | Dos miembros nunca van al mismo nodo (`gather`/`approach` con el mismo destino: las mismas unidades se contarían dos veces), y uno como mucho aviva la hoguera |
| `stuck` | Nadie falla viaje tras viaje (8 fallos en 600 s) ni se queda quieto en el mismo trabajo ocupado más de 900 s (descansar y dormir pueden durar) |
| `deaths` | Todos viven, o murieron de una necesidad (hambre, sed, frío) y su sistema de habilidades dice lo mismo, o de algo de la sociedad (vejez, H9) y la sociedad dice lo mismo |
| `store` | Cada almacén (el principal y cada acopio de H18) cabe en sus huecos y nada pasa del tope de su clase (`EconomyRules::Logistics.Stores[...].Cap`; el principal, `Tribe::StoreCap()`) más una mochila llena por miembro vivo (quien empezó a recoger antes del tope aún puede traer la suya) |
| `fire` | La hoguera de la tribu es el interactivo: combustible dentro de sus límites, encendida exactamente cuando tiene combustible, y la llama se ve exactamente entonces |
| `positions` | Ningún miembro está en una posición NaN (ni el cuerpo ni el agente abstracto) |
| `ground` | Nadie está en agua más honda de la que se vadea, dentro de un tronco o una roca, ni fuera del mapa |
| `board` (H7) | El tablón cumple: ningún trabajo con más reclamos que huecos, ningún reclamo sin progreso más allá de `ClaimSeconds` (más un refresco del tablón de margen), un trabajo por miembro y solo miembros vivos del clan |
| `skills` (H7) | Toda aptitud es un nivel finito en 0..100 |
| `knowledge` (H11) | Nadie fabrica una receta que no sabe hacer (se lee en el registro qué se hizo y se pregunta al conocimiento), y lo que estropean los experimentos se contabiliza: como mucho una unidad por intento fallido |
| `expeditions` (H12) | El grupo de una expedición es del clan y vuelve a tiempo (`MaxSeconds`); al acabar, cada miembro vivo del grupo vuelve al radio del hogar en `ReturnGrace` (900 s) o muere (contabilizado) |
| `society` (H9 + H10) | Los muertos siguen en la lista pero no vuelven a actuar (su trabajo es `dead`, no corre ninguna habilidad suya, no se mueven, no depositan, fabrican ni comen); nadie guarda más de 12 vínculos (`Relations.MaxPerMember`); genes y rasgos en 0..1 |
| `clans` (H13) | Con varios clanes (`SimView::Clans`): cada miembro (y cada persona de una sociedad) está en exactamente un clan, los ids no se repiten, las relaciones y reuniones nombran clanes que existen y cada enviado es de su clan (`ClanWorld::CheckMembers`) |
| `combat` (H14) | Vida y estamina dentro de su rango y números; los muertos ni luchan ni huyen ni son atacados; moral en 0..1 y cada herida en 0..1, nadie vive con heridas letales; y las heridas se curan (su total solo sube con una herida nueva). `deaths` acepta la muerte por `Wounds` ([CONFLICTO.md](CONFLICTO.md)) |
| `territory` (H13) |
 Las reclamaciones son finitas y no negativas, sobre regiones de tierra que existen y de clanes que existen, y avanzar dos veces las mismas con los mismos clanes da el mismo mapa (`ClanWorld::CheckTerritory`) |
| `migration` (H12) | Tras mudarse, el campamento está donde dice la última mudanza, la hoguera está junto a él y cada miembro vivo llega en `ArriveGrace` (1800 s) |

## Métricas

Una línea por partida:

```
temperate seed 1 n4 0.5d | 69 nodes | violations 0 | 7.3 s | alive 4/4 | gathered berry 36, branch 39, clay 22, fiber 12, reed 36, stone 4, wood 10 | idle 0% | failures 0
| deaths 0 | meals 18 | made 3 | deposited 153 | trade changes 0 | fire lit | clan memory 30 | jobs 5 | jobs unheld 3 | role forager 2 | role miner 1
| role woodcutter 1 | skill best avg 22 | skill top 35 | store berry 18
```

`gathered`: unidades que salieron de los nodos a las mochilas, por objeto. `idle`: parte de los pasos de los miembros vivos sin nada que hacer (`rest`). `failures`, `meals`,
`made`, `deposited`: los contadores de la tribu (entre paréntesis, lo fabricado por objeto, leído del registro). `trade changes`: veces que un miembro cambió de oficio (la revisión de H7). `fire`: estado final de la hoguera. De la tabla de
métricas: lo que sabe el clan, los trabajos del tablón y los que nadie tiene, el reparto final de oficios, la media de la mejor aptitud de cada miembro y la más alta.
H11: `experiments` (intentos, fallidos y unidades estropeadas por objeto), `techs found` (quién encontró o aprendió primero cada tecnología, cuándo y cómo, y cuántos la saben) y
`clan learnt` (cuándo la supo el clan). H12: `expeditions` (enviadas y las que encontraron lo que buscaban), `regions known` (regiones del atlas del clan) y `migrations`. H9: `births`, `population` (vivos al final y el máximo), y las muertes por causa (`deaths ... Hunger:3`).
H18: `spoiled` (unidades que se echaron a perder, por objeto), `intermediates` (lo que hicieron los trabajos `make` de las cadenas de producción), `harvests` (cosechas) y `hauled` (lo acarreado de los acopios).
Los recién nacidos reciben en el arnés un cuerpo de prueba como los fundadores (`Tribe::SetSpawner`).

**Objetivos de equilibrio** (una regresión falla en CI): en un mundo verde (templado o alpino) toda la tribu vive la partida, recoge madera y come.

## Cómo se ejecuta

```
ctest -R sim_sweep                       # el conjunto de CI: 5 partidas de medio día (los tres biomas, 3 a 6 miembros, una con agentes abstractos), una mudanza forzada y la repetición; ~40 s (75 s con la máquina cargada)
GB_SIM_SWEEP=clan GBSimSweepTests        # solo los dos clanes de 20 durante 5 días (aceptación de H7)
GB_SIM_SWEEP=full GBSimSweepTests        # el grande: semillas 1-4 × templado/alpino/desierto × 3, 6 y 12 miembros × 3 días, 4 partidas abstractas y un clan de 20 durante 5 días
```

En el editor:

```
qa sim [días] [miembros]      la escena actual sin ventana: su tribu (miembros, necesidades, mochilas, almacén, campamento) o una nueva, los nodos de la escena; falla si algo se rompe
qa check                      la tabla sobre la escena viva, ahora
qa status                     lo que ha visto el monitor en vivo (cada medio segundo de simulación mientras hay tribu), cuántos son abstractos y las métricas
qa reset                      el monitor en vivo empieza de nuevo (también al hacer `tribe build`)
qa expect "<orden>" <regex>   ejecuta la orden y falla si su respuesta no casa: los guiones de las demos aseveran con ella
panel QA on                   el panel: cada invariante con sus violaciones, la lista de hallazgos, las métricas en vivo y la última `qa sim`
```

`qa sim` parte de la escena (nodos con su estado, pozo, miembros con sus necesidades y mochilas, almacén y campamento, las tecnologías y las regiones del editor); la memoria, las
decisiones, el tablón, las aptitudes, el conocimiento, el atlas y la sociedad (edades, parentesco, vínculos: los niños de la escena entran como fundadores) empiezan de cero, y el suelo son el terreno, el agua y los troncos y rocas del mundo generado (no los cuerpos que el usuario haya puesto). Tarda unos 9 s por día con 4 miembros.

## Extenderlo (otros carriles)

Un sistema nuevo (asentamientos, conocimiento...) añade su comprobación sin tocar el barrido ni el editor:

```cpp
SimInvariants().push_back({ "buildings", "ningún edificio tiene más trabajadores que plazas", [](const SimView& v, SimTrack&, SimReport& r)
{
	for (...) if (...) r.Add(nombreDelEdificio, "lo que está mal");
} });
SimMetricTable().push_back({ "buildings", [](const SimView& v, SimMetrics& m) { m.Extra["buildings"] = ...; } });
```

`SimTrack` guarda lo que una comprobación necesita recordar entre pasos (hoy, el historial de fallos y de posición de cada miembro). Si el sistema vive fuera de la tribu, se añade su puntero a
`SimView` y el arnés y el editor lo rellenan.

## Resultados del barrido completo

(28-10-2026, Release, máquina cargada por otros carriles; tiempos de pared. Medio día son 600 s de simulación; un día, 1200.)

**Conjunto de CI** (cabeza con H7, H9-H12, antes de H18; 31 s; con H18 sigue sin violaciones, 49 s con la máquina cargada, y aparecen la cuerda y los cestos como `intermediates`): las 5 partidas de medio día, la mudanza forzada y la repetición, sin violaciones, todos vivos:

| Partida | Nodos | Vivos | Recogido | Inactivo | Comidas | Fabricado | Hoguera | Experimentos | Expediciones | Tecnologías |
|---|---:|---:|---|---:|---:|---|---|---|---|---|
| templado 1, 4 miembros | 69 | 4/4 | bayas 58, rama 18, arcilla 16, fibra 26, junco 12, piedra 4, madera 30 | 0 % | 16 | hacha, pico | encendida | 6 (4 fallidos, 2 ramas estropeadas) | 2 (1 encontró) | cestería a los 191 s |
| templado 2, 6 | 86 | 6/6 | bayas 52, rama 13, fibra 26, sílex 10, madera 18 | 6 % | 16 | hacha, 2 picos | encendida | 0 | 2 (0) | ninguna |
| alpino 1, 3 | 53 | 3/3 | bayas 18, fibra 22, madera 29 | 10 % | 10 | hacha, pico | encendida | 0 | 2 (0) | ninguna |
| desierto 1, 3 | 28 | 3/3 | nada | 52 % | 0 | nada | ninguna | 0 | 0 | ninguna |
| templado 3, 4, abstractos | 92 | 4/4 | bayas 36, rama 15, arcilla 6, fibra 22, piedra 3, madera 55 | 1 % | 16 | hacha, pico | encendida | 0 | 2 (1) | ninguna |

**Clan de 20 durante 5 días** (aceptación de H7, `GB_SIM_SWEEP=clan`, cabeza con H18, 9 min): sin violaciones.

| Semilla | Vivos al final | Nacidos | Fallos | Comidas | Oficios al final | Aptitud (media de la mejor / máxima) | Cambios de oficio | Intermedios (H18) | Expediciones | Tecnologías |
|---|---:|---:|---:|---:|---|---|---:|---|---|---|
| templado 1 | 26/26 | 6 | 0 | 1553 | 15 recolectores, 8 mineros, 3 leñadores | 50 / 83 | 6 | 27 cestos, 3 cuerdas | 15 (0 encontraron) | cestería a los 110 s (26 la saben) |
| templado 2 | 27/27 | 7 | 13 | 1035 | 18 recolectores, 6 mineros, 3 leñadores | 28 / 68 | 6 | 11 cuerdas, 2 hachas y 2 picos de sílex | 27 (7) | talla de sílex a los 801 s (19 la saben) |

Ninguna partida se mudó (`migrations 0`: el hogar no llega a ser pobre en 5 días), no hubo cosechas (no se marcó campo) ni nada se echó a perder.

**Barrido completo** (42 partidas, sobre la cabeza con H7 y antes de las correcciones de abajo; 33 min): 230 de 316 miembros vivos al final. Los 12 del desierto mueren
todos de hambre en 3 días (no hay comida en el mapa: es el mundo, no un fallo, pero el objetivo de equilibrio no lo cubre). Lo que encontró está en
[INFORME_CALIDAD.md](INFORME_CALIDAD.md#14-qa-de-la-simulación-h20); los barridos completos posteriores (con H11 y H12) confirmaron los mismos patrones en las partidas que
llegaron a completar.

## Demos de los hitos (principio 7)

Cada una es un guion del editor real, sin ventana, registrado en `ctest` con su cadena esperada; asevera con `qa expect` y `qa check` (una línea que falla hace fallar el test con
`--strict=1`) y deja una captura `shot_ui` en el directorio de compilación (`<build>/Editor/*.png`, no se versiona).

| Hito | Test | Guion | Qué asevera | Captura |
|---|---|---|---|---|
| H2 | `editor_demo_h2` | `Editor/tests/h2_gather_craft.txt` | La tribu fabrica hacha o pico, deja cosas en el almacén, los invariantes valen, y un día sin ventana de la misma escena (`qa sim 1`) acaba sin violaciones | `h2_gather_craft.png` (panel Tribe) |
| H3 | `editor_demo_h3` | `Editor/tests/h3_fire_fox.txt` | Enciende la hoguera (`Fire Pit`), la aviva con `Stoke`, los zorros cazan conejos (zorros con hambre: `h3_hungry_fox.json`), y los invariantes (la hoguera arde exactamente con combustible) | `h3_fire_fox.png` (World state) |
| H5 + H6 | `editor_demo_h5_h6` | `Editor/tests/h5_h6_agents.txt` | La navegación planea caminos, el cerebro explica sus decisiones, con la cámara lejos los miembros son agentes abstractos y el monitor en vivo no ve ninguna violación | `h5_h6_agents.png` (panel QA) |
| H19 | `editor_game_camera` (ya existía) | `Editor/tests/game_camera.txt` | Seguir, panorámica, observador y vuelta a la cámara del editor; ahora deja `h19_cameras.png` | `h19_cameras.png` |

Con la GPU débil de esta máquina y otros carriles compilando, el editor oculto va a 2 o 3 fotogramas por segundo: H2 tarda ~1,5-2,5 min, H3 ~2-3 min y H5/H6 ~1,5 min. En una máquina
normal, bastante menos. Las esperas (`wait`) están ajustadas a lo que hace falta: herramientas a los ~67 s de la tribu, hoguera a los ~68 s y la primera vez que la avivan a los ~71 s.

## Límites

- El cuerpo de prueba no tiene física ni animación: no se atasca contra otros ni contra un árbol como el actor del editor. El invariante `stuck` en vivo sí ve los atascos de los actores.
- El suelo del arnés son los troncos y rocas del mundo; los nodos de recurso no son obstáculos (tampoco lo son para los actores del editor salvo que tengan cuerpo).
- Las comprobaciones corren en cada paso (0,1 s): el coste es casi todo de la tribu y de los caminos, no de la tabla.
- La ecología y el huerto no están en el arnés (la demo H3 los cubre en el editor).
