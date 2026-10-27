# Percepción, necesidades y cerebro de utilidad (H5 + H6)

Módulo `GB/AI` (`Engine/src/GB/AI/`), puro CPU: se ejecuta sin ventana ni contexto GL y es determinista (sin azar: los empates se rompen por orden y el único "dado" es un hash
del nombre). Lo usa la tribu ([TRIBU.md](TRIBU.md)); sus tests son `tests/perception_test.cpp` (cada pieza por separado) y `tests/ai_test.cpp` (un agente entero).
La navegación (pathfinding y evitación) es otra parte de H5 y va aparte: los agentes se mueven con las tareas de siempre (`goto`, `ability ...`), que cuando llegue la
navegación serán caminos sin que el cerebro cambie.

## Qué hay

| Pieza | Fichero | Qué es |
|---|---|---|
| Vista | `Perception.h` | Cono (alcance 28 m, ±60°), conciencia total a 3 m y oclusión por obstáculos estáticos (círculos: troncos, rocas). `CanSee`, `InCone`, `SegmentHitsCircle` |
| Oído | `Perception.h` | `SoundBus`: eventos con radio (`chop`, `steps`, `scream`), cada agente pide los que no ha oído (por número de secuencia); el propio ruido no se oye |
| Memoria | `Memory.h` | `SpatialMemory`: qué cosas conoce (nombre, tipo, dónde, cuánto quedaba, cuándo lo vio, si solo lo oyó). Caduca (`Lifetime`, 2400 s), no se fía de un nodo visto vacío hasta pasado `RecheckSeconds` (90 s), y se mezcla con la de otros (`Merge`) |
| Necesidades | `Needs.h` | Hambre, sed, descanso, abrigo, seguridad y compañía como **atributos** del sistema de habilidades (0..100), más `Health` y `Vigor`. Efectos `Need.*` |
| Decisión | `Utility.h` | Opciones con puntuación y factores con nombre; `ChooseOption` con histéresis |
| Mente | `Mind.h` | `AgentMind`: memoria, decisión en curso, hacia dónde mira, opciones que sopesó. Se guarda y se carga (`ToJson`/`FromJson`) |

## Percepción

- **Vista.** Cada medio segundo (`ThinkSeconds`) cada miembro mira: lo que cae dentro del cono y no está tapado por un obstáculo pasa a su memoria con la hora. Mira hacia donde
  camina; **parado gira la cabeza** (1 rad por mirada) y así descubre lo que hay a su alrededor sin dar un paso. Lo que recuerda en un sitio que ahora ve y ya no está (talado) lo olvida.
- **Oído.** Un leñador que golpea un árbol hace ruido (`chop`, 28 m): quien lo oye aprende que hay un árbol ahí aunque no lo vea (marcado "por oído": la posición puede fallar un poco
  y la próxima vez que lo vea queda corregido). Quien camina hace `steps` (8 m), y el grito (`Tribe::Shout`, `ai scream <nombre>`) llega a 30 m y baja la **seguridad** de quien lo oye.
- **Memoria de clan.** En el campamento (a menos de 5 m) cada miembro cuenta lo que sabe al clan y aprende lo que el clan sabe (lo más nuevo gana). Por eso el que descubre una zona
  de bayas hace que los demás sepan de ella al volver a casa.
- **Coste.** La lista de lo percibible (nodos de recurso, pozos, miembros) se arma una vez por mirada y las posiciones se guardan en caché; cada miembro prueba todo lo percibible
  (O(miembros × cosas)). Con 100 agentes y unos cientos de nodos son decenas de miles de pruebas por segundo; una rejilla espacial es lo siguiente si hace falta (límites abajo).

## Necesidades

Seis necesidades, atributos de 0 a 100 donde 100 es "satisfecho". Bajan con el tiempo y con la estación y la hora (`SeasonClock`, hora del día a partir del reloj de simulación; un día
dura 1200 s, el reloj por defecto del motor):

| Necesidad | Baja | Sube |
|---|---|---|
| `Hunger` | de 100 a 0 en 900 s en primavera (x1,05 en otoño, x1,2 en invierno, x0,5 dormido) | comer una baya (+25) |
| `Thirst` | 1500 s (x1,3 en verano) | beber agua (+35); una baya da +6 |
| `Rest` | 1000 s despierto | dormir en el campamento (+0,4/s) |
| `Warmth` | invierno 0,06/s, otoño 0,015/s, la noche suma 0,02/s; en verano se recupera solo | junto a una hoguera encendida (+0,5/s) |
| `Safety` | un grito cercano (-35) | 0,4/s en el campamento o con compañía |
| `Company` | 0,02/s a solas | 0,3/s con alguien a menos de 6 m |

Efectos sobre atributos: bajo ciertos umbrales (con histéresis, para que no parpadeen) se aplican los efectos `Need.Hungry`, `Need.Starving`, `Need.Thirsty`, `Need.Tired`, `Need.Cold`,
`Need.Afraid`, `Need.Lonely`: conceden la etiqueta del mismo nombre y multiplican `Vigor` (1 = a pleno rendimiento). El cerebro usa `Vigor` para dar menos golpes por viaje y el cuerpo de
prueba de los tests camina más despacio con él.

**Morir.** Si hambre, sed o abrigo llegan a 0, `Health` baja (0,33/s por hambre, 0,45/s por sed, 0,04/s por frío); con las necesidades cubiertas se cura. A 0 el agente muere:
etiquetas `State.Dead` y `Death.<Necesidad>` (`CauseOfDeath`), el miembro deja de pensar y el registro lo anota. Un agente sin comida en el mundo muere de hambre en unos 1200 s de simulación.

## Decisión por utilidad

Cada vez que un miembro queda libre, la tribu lista lo que podría hacer **ahora** y puntúa cada opción; el cerebro elige una y la ejecuta con tareas. Las puntuaciones salen de
**consideraciones** multiplicadas (cada una queda registrada con su nombre para el inspector):

| Opción | Cuándo existe | Puntuación (resumen) |
|---|---|---|
| `eat` | hay bayas en la mochila o el almacén | `2,2 × urgencia(hambre < 60) × cercanía` |
| `drink` | hay agua en la mochila o conoce un pozo | `2,0 × urgencia(sed < 60) × cercanía` (`DrawWater` en el pozo) |
| `sleep` | siempre | cansancio (`1,7 × urgencia(descanso < 70)`) más la noche (`1,3` si descanso < 95); dura 20 s por tanda |
| `warm` | la hoguera arde | `1,5 × urgencia(abrigo < 50)` |
| `regroup` | siempre | `1,5 × urgencia(seguridad < 60)`: volver al campamento |
| `socialize` | compañía < 45 y conoce a alguien | `urgencia × cercanía`: ir junto al más cercano |
| `tool` | le falta su herramienta y el almacén puede pagarla | 1,25 |
| `gather <nodo>` | por cada tipo de recurso que recuerda (el más cercano), con la herramienta que pida | `peso del rol × (0,4 + 0,7 × necesidad) × cercanía × confianza de la memoria`; suma si ayuda a hacer su herramienta y si tiene hambre y no hay comida |
| `deposit` | lleva algo | 1,1 con 4 o más cosas en la mochila |
| `fire` | peso de rol > 0, herramientas listas y 4 de madera y 3 de piedra | 1,2 × peso |
| `explore` | no conoce nada de lo que su oficio (o su estómago) pide | 0,8 × peso (más si tiene hambre); va a un punto de un anillo en torno al campamento que cambia en cada viaje (ángulo áureo) |
| `rest` | siempre | 0,1: espera mirando alrededor |

**Los roles son pesos** (`TribeConfig::Roles`, `DefaultRoles()`): `forager` quiere bayas y fibra, `woodcutter` árboles y la hoguera, `miner` rocas; todos ayudan con lo que falta, con
un peso bajo. Nadie planea con el mundo entero: `gather` solo considera lo que hay en su memoria, y si lo recuerda de hace más de 6 s va primero a mirarlo (`approach`) en vez de
planear desde una cantidad vieja.

**Contra el comportamiento oscilante** (`ChooseOption`): lo que ya está haciendo conserva una bonificación (0,05), un rival debe superarlo por un margen (0,15), se mantiene al
menos 8 s (40 s el sueño) y solo una emergencia (puntuación ≥ 1,6) lo interrumpe antes. El test cuenta los cambios de trabajo: menos de 400 al día.

## En el editor

```
ai debug [on|off]            dibuja conos de visión, obstáculos y la memoria del miembro seleccionado
ai status [nombre]           qué decide cada uno y por qué; con nombre, sus necesidades y las puntuaciones
ai memory <nombre>           lo que sabe: tipo, sitio, cuánto quedaba, hace cuánto lo vio
ai scream <nombre>           ese miembro grita (los que estén a 30 m lo oyen y se sienten menos seguros)
```

- **Inspector.** En el panel **Tribe**, al seleccionar un miembro (clic en su nombre o en el personaje; `ai status <nombre>` también lo selecciona) aparecen: la decisión en una frase, las
  seis necesidades con barras (rojo, ámbar, verde), el `Vigor`, la estación y la hora, **todas las opciones que sopesó con su puntuación y los factores de cada una** (la elegida marcada
  con `>`), lo que ve y oye, y su memoria.
- **Depuración visual** (`ai debug on` o la casilla del inspector): el cono de cada miembro (amarillo el seleccionado), un círculo a 3 m, los obstáculos que tapan la vista (rojo) y, del
  seleccionado, un círculo por cada sitio que recuerda con su nombre y lo que quedaba (más tenue cuanto más vieja la noticia; discontinuo si solo lo oyó).
- `tribe build` ahora también pone un **pozo** cerca del campamento (los miembros tienen sed).

## Guardado

- **Necesidades**: son atributos y etiquetas del sistema de habilidades de cada actor, así que viajan con la partida como todo el estado de habilidades.
- **Memoria y decisión**: la sección `ai` (versión 1) del guardado lleva `Tribe::ToJson()`: almacén, hoguera, reloj de la tribu, memoria del clan, y de cada miembro su trabajo, sus
  contadores, su memoria, hacia dónde mira, su decisión y las órdenes que ejecutaba. Al cargar (`EditorApp::RestoreTribe`) se recrean los miembros con sus actores y se les **vuelven a dar
  las órdenes** (las tareas de un actor no se guardan).
- `tests/ai_test.cpp` guarda un agente a mitad de partida (escena, relojes de las habilidades, tribu y cuerpo), lo carga en un mundo nuevo y comprueba que, dos días simulados después,
  el resultado es **idéntico byte a byte** al del que no se detuvo.

## Tests

- `perception`: cono y oclusión, oído (alcance, secuencia, poda), memoria (caducidad, confianza, mezcla, JSON de ida y vuelta), necesidades (estaciones, fuego, muerte), selector (histéresis).
- `ai`: un agente solo en un mundo con recursos y un pozo vive **10 días simulados** (12 000 s: invierno incluidos) sin que ninguna necesidad se acerque a vaciarse, y seis agentes cada uno
  en su mundo también; en un mundo sin comida muere de hambre antes del segundo día tras explorar cada vez más lejos; la partida cargada continúa igual; dos ejecuciones acaban idénticas;
  un grito se oye, se recuerda y asusta, y uno fuera de alcance no; el agente sabe decir por qué hace lo que hace.

## Límites conocidos

- **Sin navegación**: se mueven en línea recta con `goto`; los obstáculos solo tapan la vista. Cuando llegue la navegación (H5, otra parte) los `goto` serán caminos.
- El cuerpo de un actor puede quedarse atascado contra un árbol u otro actor y fallar el viaje (se cuenta en `Failures` y decide de nuevo).
- La percepción es O(miembros × cosas): hace falta una rejilla espacial antes de unos cientos de agentes. Nada de nivel de detalle todavía (actor completo cerca de la cámara y
  agente abstracto lejos: queda para el resto de H6).
- La **agenda diaria** es implícita (la noche sube el sueño y baja el abrigo); no hay horarios. Faltan decisiones colectivas y roles que cambian (H7, H9 a H12).
- Las herramientas siguen ligadas al rol por código (`stone_axe` del leñador, `stone_pickaxe` del minero); los pesos solo mandan en el resto.
- Un recurso que no se regenera (las rocas) deja al minero esperando en el campamento; si no conoce ninguna roca sale a explorar. No hay noción de "lugares seguros" ni depredadores
  (la seguridad solo baja con gritos).
- `SyncGameplay` del editor (por ejemplo `garden build` después de `tribe build`) recarga los sistemas de habilidades desde la escena y devuelve las necesidades a su valor de la última vez que se guardó la escena.
- La memoria de clan se sincroniza solo en el campamento: dos miembros que se encuentran fuera no intercambian lo que saben.
