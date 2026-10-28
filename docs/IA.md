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
un peso bajo. Desde H7 el peso que usa el cerebro es el del rol **más la aptitud** del miembro en la familia de la opción (`Tribe::MemberWeight`: 0,6 × nivel/100), y el trabajo que
haya reclamado del **tablón del clan** multiplica por 1,6 las opciones que lo hacen (factor `job`); el oficio de cada uno se revisa con las necesidades del clan
([TRIBU.md](TRIBU.md), «Aptitudes, oficios y tablón»). Sigue siendo una sola elección por utilidad con la misma histéresis. Nadie planea con el mundo entero: `gather` solo considera lo que hay en su memoria, y si lo recuerda de hace más de 6 s va primero a mirarlo (`approach`) en vez de
planear desde una cantidad vieja.

**Contra el comportamiento oscilante** (`ChooseOption`): lo que ya está haciendo conserva una bonificación (0,05), un rival debe superarlo por un margen (0,15), se mantiene al
menos 8 s (40 s el sueño) y solo una emergencia (puntuación ≥ 1,6) lo interrumpe antes. El test cuenta los cambios de trabajo: menos de 400 al día.

## Agenda diaria

Un factor por tipo de opción según la hora (`Tribe::AgendaFactor`; se anota como factor `agenda` en el inspector): de noche (desde las 21 h; en otoño una hora antes y en invierno dos, `DuskShift`)
el sueño vale x1,6 y el trabajo (recoger, explorar, fabricar, encender) x0,4; al atardecer (tres horas antes de la noche) vuelve a casa (`deposit` x1,3), se calienta (`warm` x1,5) y busca compañía
(`socialize` x2,5) y no explora (x0,3). Comer y beber no se tocan, y las necesidades mandan: un factor solo escala la opción.

**La agenda es dato.** La de la tribu está en la sección `tribe` de `assets/data/era1.json` (`TribeAgenda`, `LoadTribeRules`; `content load` también la lee): `duskHours` y un factor por
parte del día (`night`, `dusk`, `day`) y tipo de opción. Cada rol puede tener **su agenda** (`TribeRole::Agenda`, clave `agenda` del rol): sus factores sustituyen a los de la tribu
opción por opción (lo que no nombra es lo de la tribu) y `nightShift` corre todo su día esas horas (una guardia nocturna con `nightShift: 8` trabaja mientras el clan duerme). Por
defecto ningún rol tiene agenda propia, así que el comportamiento es el de antes.

```json
"tribe": { "agenda": { "duskHours": 3, "night": { "sleep": 1.6, "gather": 0.4 }, "dusk": { "socialize": 2.5 } }, "food": "Item.Food", "drink": "Item.Food.Water",
           "stock": { "default": 12, "berry": { "min": 6, "perMember": 3 }, "wood": 10, "stone": 8 } }
```

**Comida y existencias también son dato** (`TribeEconomy`): es comida todo objeto con la etiqueta `food` (o debajo) que no tenga la de `drink` (el agua es `Item.Food.Water`: se bebe);
se come primero la de menor id (primero la de la mochila), se estropea la que más hay y la opción de recoger por hambre vale para cualquier nodo cuyo objeto sea comida (el grano de
`era1_ampliado.json` ya cuenta). `stock` dice cuánto quiere el almacén de cada objeto: el mayor de `min` y `perMember` por miembro (las bayas, 6 o 3 por miembro; madera 10; piedra 8; el resto 12).

## Nivel de detalle de simulación

Lejos de la cámara un miembro es un **agente abstracto** (`GB/AI/Abstract.h`): ejecuta las mismas órdenes (texto de tareas: `goto`, `wait`, `ability`) sin cuerpo, por la **ruta que le da su cuerpo**
(`ITribeBody::Route`: en el editor, el camino de la navegación con el perfil del miembro; sin navegación, la línea recta) a la velocidad del actor (con su
`Vigor`) y con las habilidades corriendo por temporizadores sobre su sistema de habilidades, así que la decisión es **la misma** y los recursos, el hambre y el inventario siguen igual. `Tribe::SetViewer`
(el editor le pasa la cámara en cada paso) y `TribeConfig::DetailRadius` (60 m, con 10 m de histéresis) deciden quién es abstracto; sin visor todos son completos. Al pasar a abstracto el actor se **suspende**
(`ActorWorld::SetSuspended`: sin animación, sin dibujo, sin obstáculo para los demás) y el agente empieza donde está; al volver, el cuerpo se coloca donde está el agente (`Teleport`) y se le dan otra vez las
órdenes del trabajo en curso (sin saltos de posición; el trabajo reinicia desde su primera orden). El panel Tribe dice cuántos son abstractos y marca `(abstract)` a cada uno. Medido en `ai_test`: ver Coste.

## Coste y reparto del trabajo

- Cada miembro tiene su propio turno (`Mind.NextThink`): mira y oye cada 0,5 s si está libre, cada 1,5 s si no tiene nada que hacer (`IdleThinkSeconds`) y cada 2 s con órdenes en curso (`BusyThinkSeconds`, x4 si es abstracto); decide en el
  instante en que acaban sus órdenes. Los turnos de una llamada a `Update` están limitados (`ThinkPerFrame`, y los que hagan falta para que nadie se retrase): con muchos miembros el trabajo se reparte entre fotogramas y no hay un pico cada 0,5 s.
- Los obstáculos de la vista y la lista de lo percibible están en rejillas de celdas (`OccluderGrid`, `m_SightCells`): una mirada solo visita lo cercano. El índice de nodos por tipo (`m_ByKind`) responde al "por qué descansa". Contar lo que sabe se hace en una pasada (`ScanMemory`).
  Contar lo que sabe al clan se limita a una vez cada 20 s por miembro.
- **Medido** (`ai_test`, Release, una hebra, 100 miembros de una tribu con un parche escalado, fotogramas de 0,2 s): 7,5 ms de `Tribe::Update` por segundo simulado el primer día y unos 11 ms de media en 10 días (0,2 ms por fotograma a 60 fps, 4 ms a x20), peor fotograma 8 ms; con
  nivel de detalle casi igual (el coste está en mirar y decidir, no en el cuerpo, que el test no tiene). Un día simulado de 100 miembros cuesta unos 10 s de CPU. Los 100 miembros **viven los 10 días** (con 10 pozos: con uno solo morían de sed); el test de CI corre 2 días con nivel de detalle (10 con `GB_AI_CROWD_DAYS=10`, unos 3 minutos).
- **El test `ai` no mide tiempos**: solo comprueba comportamiento, así que pasa igual con la máquina cargada (`ctest -j 6`). El presupuesto de 20 ms por segundo simulado se comprueba con
  `GB_AI_BENCH=1`. La aceptación completa de H6 (100 NPC aislados, 10 días) se corre con `GB_AI_ISOLATED=100` (en CI, 6).
- La percepción sigue siendo miembros × cosas cercanas; el siguiente paso sería que los agentes lejanos del campamento miren aún menos a menudo.

## Registro, miembros y cuerpos nulos

`Tribe::Log` es un **anillo** (`LogLines`, 800: al pasar un cuarto de más se tiran las más viejas). `Tribe::RemoveMember` quita un miembro y sus reservas (`tribe remove <nombre>`). Un miembro con cuerpo nulo no
se mueve, no decide ni hace caer nada (todo el código lo comprueba). Los fallos de viaje se anotan con su motivo (`(failed: could not reach (x, z))`, `State.Depleted`...).

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
- `tribe build` ahora también pone un **pozo** cerca del campamento (los miembros tienen sed), **escala el parche de recursos con los trabajadores** (`resources build x z <trabajadores>`; `ScaledResources`:
  2+2n árboles, 3+2n rocas, 2+n arbustos y 2+n matas) y el almacén inicial (2+2n de madera, 3n de piedra), y **selecciona al primer miembro** para que el panel enseñe su mente.
- El panel **Tribe** lleva las casillas `ai debug` y `nav debug`, un resumen (cuántos descansan, mueren o son abstractos) y, por miembro, qué hace, **por qué** (la decisión o el motivo del reposo) y su necesidad más baja.
- Los puntos a los que se manda a un miembro (sitios del campamento, puntos de exploración) se comprueban contra la navegación (`ITribeBody::CanReach`): no se manda a nadie a un tronco ni a un lago. Si un viaje al campamento
  falla, elige otro sitio la próxima vez; un nodo al que no puede acercarse se da por vacío un rato; que un nodo se vacíe por el camino ya no cuenta como fallo.

## Guardado

- **Necesidades**: son atributos y etiquetas del sistema de habilidades de cada actor, así que viajan con la partida como todo el estado de habilidades.
- **Memoria y decisión**: la sección `ai` (versión 1) del guardado lleva `Tribe::ToJson()`: almacén, hoguera, reloj de la tribu, memoria del clan, y de cada miembro su trabajo, sus
  contadores, su memoria, hacia dónde mira, su decisión y las órdenes que ejecutaba. Al cargar (`EditorApp::RestoreTribe`) se recrean los miembros con sus actores y se les **vuelven a dar
  las órdenes** (las tareas de un actor no se guardan).
- `tests/ai_test.cpp` guarda un agente a mitad de partida (escena, relojes de las habilidades, tribu y cuerpo), lo carga en un mundo nuevo y comprueba que, dos días simulados después,
  el resultado es **idéntico byte a byte** al del que no se detuvo.

## Tests

- `ai` además: nivel de detalle (abstracto lejos, cuerpo colocado donde está el agente al volver, vive 10 días abstracto), anillo del registro, `RemoveMember`, cuerpo nulo, y **100 miembros** de una tribu (parche escalado) vivos tras 2 días con coste medido.
- `perception`: cono y oclusión, oído (alcance, secuencia, poda), memoria (caducidad, confianza, mezcla, JSON de ida y vuelta), necesidades (estaciones, fuego, muerte), selector (histéresis).
- `ai`: un agente solo en un mundo con recursos y un pozo vive **10 días simulados** (12 000 s: invierno incluidos) sin que ninguna necesidad se acerque a vaciarse, y seis agentes cada uno
  en su mundo también; en un mundo sin comida muere de hambre antes del segundo día tras explorar cada vez más lejos; la partida cargada continúa igual; dos ejecuciones acaban idénticas;
  un grito se oye, se recuerda y asusta, y uno fuera de alcance no; el agente sabe decir por qué hace lo que hace.

## Límites conocidos

- En el editor los `goto` de un actor y los del agente abstracto son caminos de la navegación ([NAVEGACION.md](NAVEGACION.md)); en los tests de CPU con cuerpos de prueba siguen siendo líneas rectas
  (salvo el del agente abstracto sobre una rejilla con un lago en `tribe_test`).
- El cuerpo de un actor puede quedarse atascado contra un árbol u otro actor y fallar el viaje (se cuenta en `Failures` y decide de nuevo).
- El nivel de detalle no dibuja ni anima a los lejanos; siguen la ruta de la navegación (rodean el agua y los muros, y una meta inalcanzable falla como la del actor) pero no esquivan a los demás; al volver a ser
  completo el cuerpo aparece en un punto pisable (si el mundo cambió, en el último punto de la ruta) y reinicia la orden en curso. Con unos cientos de miembros haría falta además agrupar a los lejanos.
- La agenda es un factor por parte del día, estación y rol (datos), no un horario con citas. Los oficios ya cambian y hay tablón (H7); faltan decisiones colectivas de verdad (H9 a H12).
- Los sumideros (la fibra que sobra de 12 se gasta en el campamento, una unidad de comida de más de 20 se estropea cada 100 s) son una regla simple, no una economía; el mapa real tiene cientos de árboles que no son nodos de recurso y los miembros no los conocen.
- Las herramientas siguen ligadas al rol por código (`stone_axe` del leñador, `stone_pickaxe` del minero); los pesos solo mandan en el resto.
- Un recurso que no se regenera (las rocas) deja al minero esperando en el campamento; si no conoce ninguna roca sale a explorar. No hay noción de "lugares seguros" ni depredadores
  (la seguridad solo baja con gritos).
- `SyncGameplay` del editor (por ejemplo `garden build` después de `tribe build`) recarga los sistemas de habilidades desde la escena y devuelve las necesidades a su valor de la última vez que se guardó la escena.
- La memoria de clan se sincroniza solo en el campamento: dos miembros que se encuentran fuera no intercambian lo que saben.
