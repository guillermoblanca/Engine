# Informe del pase de calidad (PR #39 a #52)

Revisión crítica de lo fusionado hasta `5753b13` (plan, actor, guardado v1, objetos, recursos, datos JSON, interactivos, estaciones, ecología y tribu con paneles). Es una revisión, no una función
nueva: lo pequeño y claro se corrigió con test; lo grande queda descrito con una propuesta. Máquina de las medidas: Intel Core i7-11700K, RTX 3070, Windows 11, MSVC 14.44, Release.

Cómo se hizo todo: `cmake -S . -B build_calidad -DGB_WARNINGS_AS_ERRORS=ON`, `ctest -C Release` con los cuatro packs (`GB_PACKS_DIR`, `GB_MIXAMO_DIR`, `GB_INJURED_DIR`, `GB_FARMING_DIR`) y los guiones del
editor sin ventana (`Editor.exe --hidden --quality=low --script=...`).

## Resumen

| # | Hallazgo | Gravedad | Estado |
|---|---|---|---|
| 1 | Tras `load`, toda la tribu (y cualquier actor guardado con una habilidad en curso) queda bloqueada con `State.Busy` | Alta | **Corregido** (+ test) |
| 1b | Las órdenes `build` y `actor spawn` reiniciaban nodos, conejos y parcelas ya en marcha | Alta | **Corregido** |
| 2 | `replay play` falla en cualquier mundo generado del editor, ya a 0 tics (`REPLAY DIFFERS in entities`) | Alta | **Corregido** después (ver el seguimiento al final) |
| 3 | Buscar una entidad por nombre recorre toda la escena: 120 conejos costaban 5,7 ms por fotograma | Alta | **Corregido** (+ test) |
| 4 | `actor remove` de un miembro de la tribu dejaba un puntero colgante en la tribu (comportamiento indefinido) | Alta | **Corregido** (en el editor) |
| 5 | El estado de la tribu no se guarda: tras `load` el almacén es el "del futuro" | Media-alta | **Corregido** después (sección `ai` del guardado, #65) |
| 6 | `Tribe.cpp`: decisiones sincronizadas con pico de milisegundos, registro sin tope, sin quitar miembros, almacén que pasa del tope | Media | **Corregido** después (`ThinkPerFrame`, `LogLines`, `RemoveMember`) |
| 7 | `Inventory::FromJson` y `LoadContent` aceptaban o lanzaban con JSON dañado | Media | **Corregido** (+ test) |
| 8 | `SeasonClock` dividía por cero con una estación de duración 0 | Baja | **Corregido** (+ test) |
| 9 | Tests débiles o ausentes (guardar/cargar, límites, reclamos) | Media | Parcialmente corregido |
| 10 | Sandbox y los tests se compilan sin avisos activados (un `C4244` real en el Sandbox) | Baja | Corregido el aviso; propuesta |
| 11 | Usabilidad del editor: `panel X on` no trae el panel al frente, subcomandos desconocidos callaban, tabla de la tribu desbordada | Media | **Corregido** |
| 12 | Documentos que ya no decían la verdad (README, plan, INTERACTIVOS, OBJETOS, TRIBU, ECOLOGIA) | Media | **Corregido** |
| 14 | QA de la simulación (H20): el barrido de semillas con tribus encontró que los abstractos no avivaban la hoguera, que un caminante en agua no volvía a planear, clanes sin leñador y la talla de sílex inalcanzable | Alta a media | 2 **corregidos**, el resto abierto (ver 14) |

## 1. Tras cargar, la tribu se queda parada para siempre (corregido)

- **Evidencia.** Guion: `tribe build ... 4`, `timescale 8`, `wait 400`, `save b`, `wait 200`, `load b`, `wait 200`, `tribe status`. Resultado: `Ana ..., 56 failed trips`, `Biel ... 56 failed trips`, etc. con el registro
  repitiendo "could not finish gather ... / goes to ..." cada medio segundo; `ability activate Gather "berry bush 1" Ana` → `Ana cannot Gather: blocked by State.Busy`.
- **Causa.** `AbilitySystemComponent::ToComponent()` guardaba todas las etiquetas del sistema, también `State.Busy` y `Ability.<Nombre>` que pone una habilidad **en curso**. La carga cancela esa habilidad pero la etiqueta ya
  estaba en el componente y nadie la quitaba: toda habilidad con `BlockedTags = { "State.Busy" }` (Chop, Mine, Gather, Dig, Stoke...) queda bloqueada para siempre. El aviso "was in the middle of an ability: it is saved without it" prometía lo contrario.
  Con la tribu es inevitable (siempre hay alguien a mitad de un trabajo), y `GUARDADO.md` solo pedía "guardar con el personaje quieto".
- **Corrección.** `ToComponent` quita la etiqueta y las `ActiveTags` de las habilidades en curso (`Engine/src/GB/Gameplay/AbilitySystemComponent.cpp`). Test en `tests/gameplay_test.cpp` ("el componente de un personaje a mitad de una habilidad no lleva las etiquetas...").
  Comprobado con el mismo guion: sin viajes fallidos tras cargar. Las partidas guardadas **antes** de la corrección conservan `State.Busy` en el archivo y siguen cargándose bloqueadas (hay que volver a guardar con el arreglo o quitar la etiqueta a mano).

## 2. La repetición no funciona en el editor con un mundo generado (abierto)

- **Evidencia.** `generate temperate 5`, `replay record`, `replay stop r`, `replay play r` → `REPLAY DIFFERS in entities: 0 ticks, fingerprint 5d7959ec46ed513b, expected b3dc1f512913aceb`. Pasa también con `desert`, otra semilla,
  después de 200 tics, con el huerto, los recursos, los interactivos o los conejos. Con `new_scene` (sin mundo generado) da `replay identical`. `save`, `load`, `save` produce el mismo archivo (el guardado es correcto: lo que difiere es la huella de
  las entidades del mundo generado antes y después de cargarlo). **No lo causan mis cambios**: el editor compilado de H4 (`build_h4`) da exactamente las mismas dos huellas.
- **Consecuencia.** El criterio de aceptación de H4 ("la repetición de una partida da el mismo estado final") se cumple en `GameSession` (headless, `save_test`) pero no en el editor, y los tests del editor no lo cubren.
- **Propuesta (pequeña, una sesión).** Volcar la huella por entidad (`ComputeDigest`) en el modo de fallo para ver qué entidad del baseline cambia al cargar (sospechosos: datos copiados por `Split`/`CopyData`, `Active`, o la normalización de ids), y
  añadir a `Editor/tests` un caso `generate` + `replay` de 0 y de 100 tics.
- **Además, aunque se arregle**: la ecología no es repetible desde un estado cargado porque el destino de cada conejo no se guarda (ver 5).

## 3. La búsqueda por nombre era el cuello de botella (corregido)

`EntityWorld::FindByName` recorre todas las entidades (en un mundo generado hay miles de árboles). La ecología la llamaba por conejo y por planta candidata en cada fotograma; los nodos, la tribu y los interactivos, por
nombre en cada tic. Medido con `tests/scale_test.cpp` (`GB_SCALE_BENCH=1`; 3.300 a 4.000 entidades, de ellas 120 a 240 con sistema de habilidades):

| Sistema | Antes | Después |
|---|---:|---:|
| `FindByName` entre 1.001 entidades (acierto de caché con un nombre repetido; un nombre que no existe sigue recorriendo la escena) | 6,1 µs | 0,16 µs |
| Ecología, 8 conejos | 0,35 ms/fotograma | 0,026 ms |
| Ecología, 40 conejos | 1,8 ms | 0,055 ms |
| Ecología, 120 conejos | 5,7 ms | 0,12 ms |
| `GameplayWorld::Update` (120 a 240 sistemas, sin habilidades en curso) | 0,002 a 0,006 ms | igual |

Correcciones: `EntityWorld::FindByName` recuerda la última respuesta y la comprueba al usarla (la entidad debe seguir existiendo y llamarse igual: renombrar, borrar, recrear y copiar el mundo no la engañan; tests en `entity_test`),
y la ecología guarda el id del conejo y lista las plantas una vez por fotograma. Resultados idénticos (mismo conteo de nacidos y comidos en el test de determinismo).
A escala del editor, con 6 trabajadores, interactivos y 8 conejos a `timescale 20`, el fotograma bajó de 12,6 ms a 9,5 ms (sin ventana, sin vsync; mayormente actores y dibujo).

**`GameplayWorld::Names()`** copia un `std::vector<std::string>` en cada llamada: 7 µs con 400 sistemas, ~20 ns por nombre. Por tic se llama unas 6 veces (recursos, visuales, interactivos, ecología, huerto...): unos 40 µs por tic con 400
sistemas, ~2 ms por fotograma a `timescale 20`. No es el cuello de botella hoy; con 1.000+ sistemas sí. Propuesta: `ForEach(callback)` o devolver una referencia al mapa para recorrer sin copiar.

## 4. Puntero colgante al quitar un trabajador (corregido)

`ActorBody` (editor) guardaba un `Actor*` obtenido en `tribe build`. `actor remove Biel` destruye el actor y la tribu seguía llamándolo (`Busy()`, `Bag()`...) → comportamiento indefinido. Ahora `ActorBody` busca el actor por nombre en
cada llamada; un trabajador borrado queda "ocupado" para siempre: el resto sigue, pero conserva la reserva de su nodo y sigue comiendo del almacén (falta `Tribe::RemoveMember`; `tribe build` lo rehace). Probado con el guion `actor remove Biel` en mitad de la simulación. Falta un test automático del editor para esto.

Otros punteros revisados sin fallo: los oyentes de eventos (`ResourceSystem::Bind`, `InteractiveSystem::Bind`) capturan el puntero del propio sistema de habilidades que los guarda; `Load` los destruye junto con él y `Bind` se vuelve a llamar
siempre justo después (`SyncGameplay`, `Commit`), así que no hay oyentes duplicados. `EcologySystem::Update` trabaja sobre una copia de los nombres y borra/crea conejos dentro del bucle: los sistemas viven en `std::map` (los punteros
no se invalidan al insertar) y el conejo borrado no se vuelve a usar. `GameplayWorld::Create` con un nombre existente **destruye** el sistema anterior (un actor que lo apuntara quedaría colgando): hoy solo se llama tras `Load`.

## 4b. Las órdenes `build` borraban el estado vivo (corregido)

`resources build`, `interactives build`, `garden build` y `actor spawn` reconstruyen todos los sistemas de habilidades (`SyncGameplay` → `Load`) desde los **componentes**, que solo se actualizan al guardar. Reproducción: `tribe build`, `ecology spawn`, `timescale 20`, `wait 400`,
`interactives build -8 -4`: los arbustos pasaban de 2 a 6 de cantidad, los árboles se rellenaban y `Rabbit 1` volvía de Food 74 / Age 255 / Cooldown 85 a 70 / 120 / 0. Ahora cada una de esas órdenes escribe el estado vivo en los componentes antes de construir. Los enfriamientos y las habilidades en curso siguen sin sobrevivir a una reconstrucción.

## 5. Lo que no se guarda (abierto; la parte de la tribu ya la resolvió #55)

> Escrito antes de #55: la tribu (almacén, hoguera, memoria y necesidades de cada miembro) ahora va en la sección `ai` del guardado y sigue donde estaba al cargar. Siguen sin guardarse los temporizadores de los interactivos y el destino de cada conejo.

Se guarda: atributos y etiquetas de interactivos, nodos y conejos; posición y mochila de los actores; efectos con tiempo. **No se guarda**:

| Qué | Efecto tras cargar |
|---|---|
| Estado de la `Tribe` (almacén, hoguera, reloj, trabajos, reclamos, hambre, contadores) | El almacén y la hoguera conservan lo de **antes** de cargar mientras el mundo y las mochilas vuelven atrás; los trabajos en curso se pierden (tras el arreglo 1, se reanudan bien) |
| Temporizadores de `InteractiveSystem` (`m_Burn`) | El tramo parcial de combustible o de llenado del pozo empieza de nuevo (≤ 6 s y ≤ 20 s) |
| Destino de cada conejo (`Walk`: centro, planta, tiempo comiendo) | Eligen de nuevo desde donde están: la partida cargada no sigue paso a paso igual (repetición, ver 2) |

**Propuesta.** Una sección `tribe` en `ISaveHost::Sections()` (almacén con `Inventory::ToJson`, `m_Fire`/`m_Burning`/temporizadores, `m_Time`, y por miembro nombre, rol y contadores; los trabajos se descartan y se vuelven a decidir) y otra `ecology`
(`Walk` por conejo y los contadores `born/starved/eaten`). Los temporizadores de los interactivos pueden ser atributos (`Burn`, `Refill`) para que viajen con el componente. Va con tests de guardar/cargar/continuar.
La tribu es de la sesión de percepción e IA: se le deja la nota en la carpeta compartida.

## 6. Notas sobre `Tribe.cpp` (de otra sesión; no se tocó)

> **Actualización tras #55 (cerebro de utilidad).** Estas notas se escribieron sobre el código de #52. Reverificado con el nuevo: la tanda de decisiones con 60 miembros mide 1,6 ms de media y 2,6 ms el peor fotograma (antes 3 a 4,5 ms); el registro sigue sin tope en memoria (al guardar solo
> van las últimas 200 líneas); los cuerpos nulos ya se comprueban (`!m.Body`); sigue sin haber `RemoveMember`, de modo que lo del trabajador borrado (hallazgo 4) vale; el resto (`StockCap`, nombres con `_`, las dos hogueras) no se ha vuelto a medir. El estado de la tribu **sí se guarda ya** (sección `ai`): ver la nota del hallazgo 5.

- **Decisiones sincronizadas.** *(Resuelto en la rama `ia-p0p1`: turnos por miembro repartidos entre fotogramas, rejillas de obstáculos y de lo percibible, índice por tipo, anillo de registro, `RemoveMember` y cuerpo nulo; ver [IA.md](IA.md).)* Todos los miembros deciden en el mismo fotograma cada 0,5 s. Con 60 miembros, 120 nodos y 3.300 entidades, el peor fotograma de decisiones costó **2,9 a 4,7 ms** (`scale_test`: 4 miembros 0,4 ms, 16 miembros 1,0 ms, 60 miembros 3 a 4,5 ms);
  el coste es miembros × nodos por tipo × (copia de `Names()` + concatenar `"Resource.Node." + kind`). Para 50 a 100 NPC: repartir la decisión entre fotogramas (un miembro cada N) y un índice de nodos por tipo.
- **Registro sin tope.** `m_Log` crece sin límite: ~0,5 líneas por trabajador y segundo (288 líneas en 600 s con 4). Con 60 miembros, ~25.000 líneas por hora de simulación. Propuesta: anillo de 500 a 1.000 líneas.
- **Sin quitar miembros.** No hay `RemoveMember`; un cuerpo nulo (`AddMember(..., nullptr)`) hace que `Think` desreferencie un puntero nulo (`m.Body->Busy()` se comprueba en `Update`, pero `Consume` llama a `m.Body->Bag()` sin comprobar).
- **El almacén pasa del tope.** `StockCap` = 40 se comprueba al empezar a recoger, no al depositar: 43 de madera y 46 de fibra en el test (`tribe_test`, 600 s).
- **Nombres.** Las órdenes pasan los nombres de nodo con espacios cambiados por `_` y la lista de tareas los devuelve a espacios: un nodo cuyo nombre ya lleve guion bajo (`my_tree`) o una barra `|` no se puede alcanzar.
- **Dos hogueras.** La tribu construye una entidad `Campfire` solo visual; el interactivo `Fire Pit` no está conectado (ver INTERACTIVOS.md).
- **`tribe status` en una línea** sustituye los saltos por `|` porque la consola solo muestra la primera línea; en el panel Tribe sí se lee bien.

## 7. Entrada de datos dañada (corregido)

- `Inventory::FromJson`: aceptaba cuentas 0 o negativas (rompen toda suma) y **lanzaba una excepción** (`type_error`) con `"slots": "muchas"` o `"maxWeight": "pesado"`: una partida con un inventario así tumbaba el programa. Ahora devuelve `false` y
  no toca el inventario. Test (`items_test`): 13 variantes dañadas.
- `LoadContent`: `"tags": "Item.X"` (texto en lugar de lista) se aceptaba sin avisar. Ahora se rechaza. Test: 18 documentos dañados distintos se rechazan con mensaje y sin cambiar nada.
- Sin cubrir aún: que la etiqueta `requires` de un nodo y la habilidad que nombra un objeto existan.

## 8. Estaciones (corregido)

`SeasonClock::At` y `Progress` dividían por `SeasonSeconds`: con 0 daban un `int` a partir de infinito (indefinido). Ahora devuelven primavera y 0, y la cuenta usa `long long`. Test en `ecology_test`.

## 9. Calidad de los tests

Lo que se revisó y lo que se hizo:

| Test | Veredicto | Acción |
|---|---|---|
| `tribe_test` | Mide cosas reales (herramientas, hoguera, invariantes, determinismo con registro incluido). El encabezado decía "nadie reclama dos veces un nodo" sin comprobarlo; `deposited >= 20` es una cifra arbitraria; no probaba hambre, tribu vacía ni sin recursos | Añadidos: invariante de reclamos únicos a cada fotograma, tribu sin construir, sin miembros, y trabajador sin nada que recoger (hambre, motivo, registro sin spam) |
| `ecology_test` | Bueno en conteos y determinismo; `Born > 0` y `Count() >= 4` son débiles; nada de guardar/cargar ni de estaciones raras | Añadidos: guardar/cargar a mitad (mismos conejos y plantas con los mismos números, y sigue funcionando), estación de duración 0 o negativa, reloj negativo y enorme |
| `resources_test` | Cubre agotar, regenerar, invierno y determinismo. El comentario "se guarda con el nodo" no se probaba; `tree 3 <= 1` es holgado | Cubierto indirectamente por el nuevo guardar/cargar de la pradera (`Regrow` y `Amount` se comparan) |
| `interactives_test` | Bien (flujo de un día, límites, determinismo). Sin guardar/cargar | Añadido: fogata encendida y puerta abierta se guardan, cargan y la fogata cargada se apaga a su hora |
| `items_test` | Cubría contenido JSON mal formado solo con 4 casos | Añadidos 18 documentos y 13 inventarios dañados (encontraron 2 fallos reales: ver 7) |
| `gameplay_test` | No cubría guardar con una habilidad a medias | Añadido (hallazgo 1) |
| `entity_test` | No probaba la búsqueda por nombre bajo renombrado/borrado | Añadido (hallazgo 3) |
| `save_test` | Cubre H4 en `GameSession`; no cubre nada de la tribu, la ecología ni los interactivos | Ver 5 y 2 |

Sin tests aún: ordenes del editor (`tribe`, `ecology`, `interactives`, paneles), `actor remove` de un trabajador, y el comando `content load` con un fichero dañado. Estabilidad: **tres ejecuciones completas de `ctest` (55 tests, 13 s con `-j4`) y una serie
secuencial de los tests de simulación pasaron las cuatro veces**, sin tests inestables. Los tests de GL no se saltaron (hay contexto OpenGL 4.5).

Nuevo: `tests/scale_test.cpp` (CPU) comprueba que 4, 16 y 60 miembros, y 8, 40 y 120 conejos, funcionan sin romper límites y, con `GB_SCALE_BENCH=1`, imprime los costes de este informe.

## 10. Compilación y avisos

- Compila entera y limpia con `-DGB_WARNINGS_AS_ERRORS=ON` en MSVC 14.44 (Release). Un único aviso real: `C4244` en `Sandbox/src/SandboxAPP.cpp:1101` (`std::pair<float,float>` inicializado con enteros), que pasaba porque **solo `GB` y `GBEditor` llevan
  `/W4 /WX` (o `-Wall -Wextra -Wpedantic -Werror`); el Sandbox y todos los tests se compilan sin avisos activados**. Corregido el aviso.
- Riesgos para GCC/Ubuntu: no se pudo compilar con GCC aquí. Con el aviso del Sandbox y los tests sin `-Wall -Wextra`, una comparación con signo o una variable sin usar ahí no la detectaría CI en ningún sistema. `EcologySystem::Think` está
  declarada y nunca definida (inofensivo). Propuesta: activar los avisos (sin `-Werror` al principio) en `Sandbox` y `tests` y limpiarlos de una vez.
- En `tests/CMakeLists.txt` se añadió `GBScaleTests`.

## 11. Rendimiento medido

Todas las cifras: Release, una sola hebra, `tests/scale_test.cpp` con `GB_SCALE_BENCH=1` salvo indicación; mundo con 3.300 a 4.000 entidades.

| Qué | Medida |
|---|---|
| Actor completo (`GB_ACTOR_BENCH`, X Bot, 65 huesos, IK) | 16 µs por actor y fotograma con 1 actor, 18 µs con 8, 21 µs con 32, 22 µs con 128 (2,8 ms los 128). `COSTE_ACTOR.md` decía 45 µs (otra compilación/máquina); caído (ragdoll) 88 µs |
| Tribu, tanda de decisiones | 4 miembros 0,4 ms, 16 miembros 1,0 ms, 60 miembros 3 a 4,5 ms en un fotograma (cada 0,5 s) |
| Ecología | 120 conejos 0,12 ms por fotograma (antes 5,7 ms) |
| Sistema de habilidades (`GameplayWorld::Update`, sin habilidades en curso) | 2,6 µs con 128 sistemas, 5,6 µs con 240 (~23 ns por sistema) |
| `GameplayWorld::Names()` | 7 µs con 400 sistemas (copia de cadenas) |
| `EntityWorld::FindByName` | 6,1 µs → 0,16 µs con 1.001 entidades |
| Refinado de orillas (`ShoreSubdivision`) | Por defecto (4): 227 ms y 381 k triángulos en la rejilla 257 (153 ms y 131 k sin refinar); 505 ms y 1,33 M triángulos en la 513. Tabla completa en `mundo.md` |
| Alfa a cobertura del follaje | No distinguible a 790x617: ~5 ms por fotograma (RTX 3070, sin vsync, 735 k hojas, 420 árboles) con cualquier preset de `quality` (el comando los describe como presets de postproceso; no se comprobó si cambian el MSAA). Una medida real necesita un modo que fije MSAA y un tamaño de ventana grande |

### Escala de H16 (test `sim_scale`, `GB_SCALE_BENCH=1`, 3 minutos simulados a 60 Hz)

Clanes en el mundo del editor (templado, semilla 3, 360 m) con cuerpos de prueba que caminan la navegación real (`SimRun` con `Clans`), el visor en el primer campamento
(sus miembros completos, el resto abstractos), presupuesto de búsquedas de 4 por paso y 20.000 celdas, caché de caminos de 128, un presupuesto de pensar compartido
(`ClanWorld::ThinkPerFrame`). Release, una hebra, **la máquina cargada por otras compilaciones**: las colas (p99, máximo) llevan ese ruido.

| Caso | Tribus p50 / p95 / p99 ms | de ellas pensar p95 | abstractos p95 | combate p95 | paso completo p50 / p95 | búsquedas (media) | x tiempo real |
|---|---|---|---|---|---|---|---|
| 100 en 3 clanes | 0,49 / 2,20 / 7,1 | 1,40 | 0,49 | — | 1,6 / 3,4 | 2.022 (1,6 ms) | x8,2 |
| 300 en 5 clanes | 1,65 / **4,93** / 12,2 | 2,64 | 2,96 | — | 3,3 / 6,9 | 6.701 (1,2 ms) | x4,1 |
| 100 en 3, hostiles, combate H14 | 0,67 / 2,35 / 4,5 | 1,82 | 0,43 | 0,12 (+0,10 en las tribus) | 1,9 / 3,7 | 10.981 (0,23 ms) | x7,2 |
| 300 en 5, hostiles, combate H14 | 3,26 / **7,54** / 11,5 | 5,60 | 2,63 | 0,40 (+0,75 en las tribus) | 5,2 / 9,9 | 23.093 (0,25 ms) | x2,8 |

- **El objetivo (p95 de las tribus < 4 ms con 300) no se cumple**: 4,9 ms sin combate y 7,5 ms con él. Con 100 se cumple con holgura. El coste está repartido entre los
  turnos de pensar (unos 0,25 ms por decisión; con 300 hacen falta ~10 por fotograma a 60 Hz porque las órdenes terminan a ese ritmo, no por el reloj de cada uno) y el paso
  de los agentes abstractos (sus búsquedas de camino, dentro del presupuesto). El máximo de unos 60 ms es el primer fotograma (todos deciden a la vez) y algún refresco de
  la vista. Lo siguiente sería abaratar `Decide`/`Look` (lógica de juego, otros carriles) o pensar en paralelo por clan.
- **Lo que se arregló midiendo**: con combate, cada pelea perseguía metas inalcanzables y las búsquedas costaban **8,5 ms de media** (p95 de los cuerpos 37 ms); el tope de
  celdas por fotograma (`NavConfig::MaxExpandedPerFrame`) y la caché de fallos lo dejan en 0,23 ms y 0,29 ms. `StepConflict` comprobaba quién cuida a quién con miembros ×
  miembros cada fotograma (ahora una lista de cuidadores) y `SyncTools` recorría todos los objetos para cada miembro y fotograma (ahora `ItemLibrary::ToolTags`).
- El combate en sí (`CombatSystem::Update`) cuesta 0,1–0,4 ms con 100–300; las amenazas y peleas dentro de las tribus, 0,1–0,75 ms. En 3 minutos no llegó a salir ninguna incursión.
- **Mundo de varias baldosas** (mismo test): 2×2 de 360 m se genera en 0,7 s y su rejilla de navegación (1.436², 39 MB) en 3,9 s; 3×3 en 1,4 s y 10 s (2.156², 89 MB). Un
  viaje de esquina a esquina (962 m y 1.471 m) cuesta 59 y 93 ms y en 3×3 agota `MaxExpansions` (240.000 celdas: camino parcial): los viajes de kilómetros necesitan
  planificar por el grafo de regiones y la búsqueda fina por tramos (pendiente).
- Repetición del editor: `editor_replay`, `_wild` y `_midrun` pasan; `editor_replay_society` difiere también con el editor de `master` sin estos cambios (probado con el
  ejecutable del carril visual: "REPLAY DIFFERS in sections"), y `editor_play` falla igual en la build de H15 en esta máquina (fotogramas lentos → `quality auto`).

**Qué no escala a 50 o 100 NPC**: (1) cada trabajador es un actor completo (~20 µs × 100 = 2 ms por fotograma más el dibujo del skinning, que es de GPU y no se midió), así que hace falta nivel de detalle (ver `COSTE_ACTOR.md`); (2) la tanda de decisiones de la tribu
(4,5 ms con 60, lineal en miembros y en nodos); (3) `Names()` y las cadenas de etiquetas por nodo; (4) cada actor lleva su copia de obstáculos estáticos. La ecología ya no es un problema.

## 12. Usabilidad del editor (corregido lo pequeño)

Capturas con `shot_ui` de una escena con tribu, interactivos y conejos (ver los guiones usados arriba).

- `panel WorldState on` y `panel Abilities on` **no traían el panel al frente** (están en la misma pestaña que otros): la captura salía con el Inspector. Ahora se enfocan igual que Tribe.
- La tabla de miembros del panel Tribe se desbordaba en el ancho por defecto del dock (`Biel woodcutte`, `Clec...`, "Doing" cortado). Columnas fijas para nombre y rol, texto con ajuste de línea en el resto y en el registro.
- `tribe frob`, `ecology frob`, `interactives frob` y `resources frob` se comportaban como `status` sin quejarse. Ahora dan el uso. `panel Nope on` ahora lista los paneles.
- Falta, no corregido: el panel Abilities arranca con el objetivo `Plot 1` (del huerto) aunque no haya huerto; los miembros de la tribu no muestran **qué ven ni por qué eligieron** el trabajo (solo el último motivo de descanso y la orden actual); el panel World state lista cada conejo sin tope (con 100 serán 100 barras);
  `help` de `panel` no menciona los paneles nuevos; seleccionar un actor por el Inspector muestra posición 0,0,0 (la entidad no sigue al actor).

## 13. Documentación frente a código

Corregido: `README.md` (hoja de ruta con lo hecho y sin lo que ya no está en curso), `PLAN_SOCIEDAD_VIVA.md` (cabecera obsoleta, H1 "en curso", y una **tabla de estado** que no existía), `INTERACTIVOS.md` ("pendiente" ya hecho: estaciones, ecología y
panel; lo que se guarda), `OBJETOS.md` (colocar nodos nuevos desde JSON no es posible: `BuildResources` solo conoce cuatro tipos), `TRIBU.md` (qué pasa tras `load` y `actor remove`, tests, límites nuevos), `ECOLOGIA.md` (coste, qué se guarda), `mundo.md` (coste del refinado de orillas) y `COSTE_ACTOR.md`
(nota con la remedición). Comprobado que dicen la verdad: las tablas de roles, estaciones y habilidades, y los valores (`EatSeconds` 40, `FireSeconds` 30, 240 s por estación).

## 14. QA de la simulación (H20)

Barrido de semillas de la **simulación** (tribus que viven días sobre mundos generados, sin ventana, con la navegación real), con una tabla de invariantes y métricas compartida por el
test `sim_sweep`, `qa sim` y el panel QA del editor: ver [QA_SIMULACION.md](QA_SIMULACION.md). Lo que encontró en sus primeras ejecuciones:

| # | Hallazgo | Reproducción | Estado |
|---|---|---|---|
| 14a | Los miembros **abstractos** no podían avivar la hoguera: el agente abstracto busca el destino de `ability Stoke Campfire` en la caché de posiciones de la tribu, que solo guarda lo percibible (nodos, pozos); fallaba "the target is gone" cientos de veces (406 fallos en medio día) y la hoguera se apagaba | `sim_sweep`, templado 3, 4 miembros, abstractos | **Corregido** (`Tribe::StepAbstract` busca en la escena lo que no está en la caché); el caso está en el conjunto de CI |
| 14b | Un caminante en una celda que es muro **para su perfil** (agua algo más honda de lo vadeable en una orilla) no podía volver a planear: `NavGrid::FindPath` solo buscaba la celda libre más cercana si el inicio era un obstáculo. El miembro fallaba cada viaje hasta morir de hambre | full con H7: templado 2, clan de 20, día 4 (`Joel ... in water 1.4 m deep`); alpino 3, 12 miembros (dos miembros en agua de 2,3 m yendo a `wild flint outcrop 1`) | **Corregido** el atasco (el inicio en celda intransitable para el perfil busca la libre más cercana; `navigation` pasa). Queda sin explicar **cómo entra** en esa agua: la profundidad exacta de una orilla empinada difiere de la de la celda de 0,5 m; para el carril de navegación |
| 14c | **Clanes de 3 sin leñador**: `PlanRoles` o la revisión de oficios dejan a un clan de 3 con 2 recolectores y 1 minero; nadie hace el hacha, no hay madera ni hoguera en 3 días | full: templado 2 y 4, alpino 4 (3, 6 y 12 miembros: hecho 0, hoguera ninguna) | Abierto, para H7 (equilibrio de `PlanRoles` y `RoleRules.MinClan`): un oficio que fabrica la herramienta de la que depende la hoguera no debería quedar vacío |
| 14d | **La talla de sílex no se descubre nunca**: en ninguna partida se fabrica cuerda (`cordage`) y `flint_knapping` no aparece; solo la cestería se descubre (por experimentar, entre 110 y 870 s) | todas las partidas con H11 (métrica `made` por objeto y `techs found`) | Confirmado con H11. Con H18 la cuerda ya se hace (trabajos `make`: 2-3 en medio día, métrica `intermediates`), y en el clan de 20 de la semilla 2 se descubre la talla de sílex a los 801 s (11 cuerdas, hachas y picos de sílex). En la semilla 1 no (solo cestería). Resuelto por H18 |
| 14e | **Expediciones que no vuelven a planear**: un miembro de una expedición se queda en agua de 1,1 m fallando 100 viajes en 600 s | full con H12: alpino 3, 3 miembros, desde los 1229 s | Abierto, para H12 (con 14b corregido debería salir; volver a comprobar con `GB_SIM_SWEEP=full`) |
| 14f | Un miembro falla 8 viajes en 10 min yendo a dormir (su sitio del campamento no se alcanza) | full: alpino 2, 12 miembros, a los 358 s | Menor, abierto |
| 14g | Desierto: todo el clan muere de hambre en 3 días (no hay comida en el mapa) | full: desierto 1-4 | Del diseño del mundo; los objetivos de equilibrio solo se exigen en mundos verdes |
| 14h | Equilibrio de oficios en el clan de 20: 15-19 recolectores para 3 leñadores; la fibra se acumula (844 en 5 días) y la aptitud máxima llega a 83 | `GB_SIM_SWEEP=clan` | Para equilibrar en H7 (datos) |

Además, principio 7 del plan: demos con guion, aserciones (`qa expect`, `qa check`) y captura para H2, H3, H5/H6 y H19, registradas en `ctest` (ver QA_SIMULACION.md).

## Seguimiento: repetición y guardado completos (PR posterior)

Causas encontradas al arreglar el hallazgo 2, todas ya corregidas con test (`ctest`: `editor_replay`, `editor_replay_society`, `editor_replay_midrun` y los tests de CPU de `save`, `ecology`, `interactives` y `entity`):
1. **Redondeo a 7 cifras.** Los archivos escribían los `float` con 7 cifras y una coordenada como -155,165 cambiaba de cubeta de la huella al volver a leerse (`Tree 270` de las 492 entidades del mundo de prueba). Ahora se escriben exactos (el texto más corto que lee el mismo `float`); la identidad del mundo generado sigue con 7 cifras para que las partidas viejas casen.
2. **Los actores se movían con el tiempo del fotograma**, fuera del tic fijo: ninguna repetición con actores podía salir igual. Ahora se mueven en `EditorSaveHost::Step` (salvo con un jugador manejando uno).
3. **Cargar no quitaba ni creaba actores**: una repetición cuyas órdenes crean un actor fallaba ("there is already an actor"), y un guardado cargado en un mundo nuevo perdía la tribu. Los actores guardan de qué estaban hechos (`kind`), se recrean al cargar y se quitan los que la partida no tiene.
4. Faltaba guardar los temporizadores de las fogatas, el destino de los conejos y el aspecto de la estación (secciones `interactives`, `ecology` y `season-look`).
5. Las órdenes `ecology`, `interactives`, `tribe`, `actor` y `content` no se grababan en las repeticiones.

Sigue sin ser idéntica paso a paso una partida guardada **a mitad de una habilidad** (se restaura cancelada; ver GUARDADO.md): hay que guardar o grabar con los personajes quietos.

## Tareas abiertas, por prioridad

1. ~~Repetición del editor y estado guardado~~ (hecho: ver el seguimiento). Queda guardar habilidades en curso para que un guardado a mitad de trabajo sea idéntico.
3. **Tribu a 50 o 100 miembros** (6, 11): repartir decisiones, índice de nodos, registro acotado, `RemoveMember`.
4. **Avisos en Sandbox y tests** (10).
5. **Nivel de detalle de actores** (11): ya previsto en H16.
6. **Usabilidad** (12): objetivo por defecto, "por qué" de cada decisión, lista de conejos acotada.

## Decisiones que necesita el usuario

- ¿Se arregla la repetición del editor (2) antes de seguir con H5/H6? (Recomendado.)
- ¿Subir el valor por defecto de `ShoreSubdivision` de 4 a 2 con rejillas de 513 o más (3,9 M triángulos a 8; 1,33 M a 4)?
- ¿Activar `-Wall -Wextra` en Sandbox y tests aunque obligue a limpiar avisos?
