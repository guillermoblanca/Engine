# Plan de jugabilidad: por qué los sistemas "no hacen nada" y cómo lo cambiamos

Este documento recoge lo que se ve jugando (clanes que mueren, casas que no se terminan, niños que trabajan, nada clicable), la causa de cada cosa **según el código actual** y el orden en que proponemos arreglarlo. Cada fase termina con una prueba que se puede ejecutar sin ventana (`GBSim` + laboratorio, `docs/LABORATORIO.md`) para no depender de "parece que funciona".

Estado: propuesta. Las causas marcadas **(verificado)** salen de leer el código; las marcadas **(hipótesis)** hay que confirmarlas con telemetría antes de tocar nada.

## 1. Edificios: por qué hay muchos nodos, pocos edificios y no se completan

Lo que ocurre, en el orden en que falla:

1. **La colonia casi nunca decide construir (verificado).** `Tribe::BuildingNeeds` (`Settlement.cpp`) devuelve vacío mientras no haya hoguera *y* herramientas (`HasFire() && ToolsReady()`), y además solo si hay menos obras abiertas que `SitesFor(vivos)`. Con 4–6 miembros eso es **una sola obra a la vez**. Si esa obra se atasca, no empieza ninguna más: no hay prioridades entre obras, hay una cola de uno.
2. **Solo se planifica la necesidad más urgente (verificado).** `PlanSettlement` coge `needs.front()`. Una despensa urgente puede tapar para siempre la casa que falta.
3. **El material sale del almacén común (verificado).** Las obras piden lo que les falta como "demanda" a la economía, pero quien lo lleva es cualquiera que tenga libre el trabajo `deliver`. Ese trabajo compite con recolectar (el tablón puntúa `gather` 1.0, `deliver` 1.1, `build` 1.3, pero `build` solo existe cuando la fase ya tiene todo el material). Resultado: los recolectores traen comida y madera al almacén, nadie lo traslada a la obra, y la obra espera. **No existe el rol de transportista**: tienes razón.
4. **Construir exige el permiso `Build` (verificado).** Solo quien lo tiene concedido puede ejecutar la fase; si pocos miembros lo tienen y están ocupados o dormidos, la obra no avanza.
5. **No hay casas, hay una "bolsa de camas" (verificado).** `Beds()` suma `Shelter` de los edificios acabados y `ShelterNeeds` asigna el hueco por índice (`slot`). No hay propietario ni familia, y por eso no puedes ver "la casa de la familia X". Dormir dentro ocurre solo si el miembro está cerca de un hueco libre.
6. **GBSim no carga el contenido de la era (verificado).** Por eso en el laboratorio ves 0 edificios: no es que no construyan, es que el simulador no los conoce. Esto hay que arreglarlo **primero** para poder medir el resto.
7. **Horario (verificado).** `AgendaFactor` multiplica las puntuaciones según la hora y el oficio; no es un horario. Nada obliga a dormir ni a ir a casa por la noche, solo hace menos atractivo el trabajo.

### Cómo lo cambiamos

- **F1. Medir.** Cargar `era1.pack.json` en GBSim (edificios, tecnologías, configuración social) y añadir a la telemetría el estado de cada obra por día: fase, material que falta, quién la trabaja y tiempo parado. Prueba: en el laboratorio, "casas empezadas / terminadas / paradas y por qué".
- **F2. Obras con prioridad y varias a la vez.** Una lista de obras con prioridad (casa de la familia sin techo > despensa llena > resto), tope de obras por tamaño del clan *y* por material disponible, y una obra parada más de N días deja paso a otra. Cada obra nace con su "lista de la compra" visible.
- **F3. Rol de transportista.** Rol de datos (`roles.json`) cuyo trabajo único es mover material del almacén a las obras y de los nodos al almacén. Los recolectores dejan lo recogido en el punto más cercano y siguen. Prueba: sin transportista una obra tarda X; con él, Y (comparación en el laboratorio).
- **F4. Casas por familia y mejoras.** `Household`: cada pareja con sus hijos tiene una casa asignada (propiedad, hueco fijo). Si falta casa, la familia aparece como "necesita casa" en el plan de obras. Cadena de mejora (refugio → cabaña → casa) con los `Supersedes` que ya existen: se mejora cuando hay material sobrante.
- **F5. Horario real.** Bloques de datos por edad y oficio: dormir (en su casa), trabajar, comer/socializar. El cerebro de utilidad elige dentro del bloque; fuera del bloque solo las urgencias (sed, peligro) lo interrumpen. Se configura en JSON y se ve en el laboratorio (qué hacían a cada hora).

## 2. Hijos: partos, intervalos y niñez

- **Hoy (verificado):** `BirthRules.Spacing` es un número fijo (2 años entre hijos de una madre) y nace un hijo por parto. En partidas cortas eso son 1–2 hijos por pareja y los clanes no pasan de 6 miembros por el reloj, no por un tope (`MaxMembers` es 200).
- **Cambio:** parto con probabilidad de gemelos y trillizos (datos: `twins`, `triplets`), intervalo mínimo de 3 meses sin máximo, y decisión de concebir según **recursos para mantener** (comida del almacén por habitante, casa con hueco, sin amenaza). El intervalo deja de ser un reloj y pasa a ser una consecuencia.
- **Niños (verificado en parte):** el código trata `IsChild` en combate, diplomacia y conversación, pero el reparto de oficios (`TribeWork.cpp`, comentario "a child keeps its child's trade") les asigna uno. Hay que hacer que un niño juegue, aprenda mirando y no ocupe puestos de trabajo hasta su etapa. Etapas con edades en datos (bebé, niño, adolescente, adulto). Prueba: telemetría de trabajos por edad = 0 para niños.

## 3. Por qué mueren los clanes y sobrevive solo uno

- **Lo medido:** en los barridos de 3 clanes el clan 1 casi siempre queda aniquilado (8–11 muertes) en 4 días de simulación.
- **Hipótesis a confirmar:** (a) hostilidad e incursiones demasiado agresivas desde el primer día, (b) los clanes sin edificios ni despensa dependen de recolectar a diario y el forrajeo se atasca (ya corregidos varios bucles; quedan `envoy`, `sleep` y `deposit`), (c) la generación del mundo deja un clan sin agua o comida cerca.
- **Cómo lo sabremos:** la tabla `deaths` ya guarda causa. El laboratorio mostrará por clan "causa de muerte vs. día". Con eso se decide si se toca la agresividad (datos), el forrajeo (código) o la colocación inicial.
- **Arreglos ya identificados:** los tres bucles pendientes y hacer que la violencia sea una opción con coste (relación, riesgo) y no el valor por defecto.

## 4. Observar y jugar (UX)

Falta casi toda la capa de observación (verificado: los nodos de recursos no son seleccionables desde el juego y no hay panel de relaciones entre clanes).

- **Clic en cualquier cosa:** recurso, edificio, persona, animal → panel con qué es, estado, quién lo usa, qué le falta.
- **Panel de clan:** necesidades, trabajos, obras en curso y por qué se paran, relación con cada otro clan (confianza, hostilidad, comercio).
- **Árbol de decisiones visible** por persona: opciones puntuadas y por qué ganó la elegida (los datos ya están en la tabla `decisions`; falta mostrarlos).
- **Eventos del clan** en una línea de tiempo (nacimientos, muertes, edificios terminados, incursiones).
- Esto se diseña después de F1–F3: no tiene sentido pulir paneles de sistemas que todavía no hacen lo que dicen.

## 5. Vestimenta y personalidad visual

Hoy los personajes son intercambiables. Propuesta: una capa de **aspecto por datos** (`outfits.json`) con prendas, sombreros y colores elegidos por oficio, edad, clan y rasgos del individuo (semilla estable), de modo que se reconozca a primera vista al cazador, al transportista, al líder o al niño. Primero con modelos/colores base ya existentes y luego añadiendo piezas. El aspecto se guarda en la partida para no cambiar al recargar.

## 6. Acoplamiento: ¿está demasiado atado?

Es una preocupación fundada **(verificado)**: `Tribe` es una clase de ~7.000 líneas repartidas en 10 ficheros (`Tribe.cpp` solo tiene más de 2.100) con un único `Tribe.h` de ~870 líneas. Economía, asentamientos, sociedad, conflicto, telemetría y visuales cuelgan de ella y se tocan entre sí (por eso arreglar una cosa rompe un test social).

No proponemos reescribirla de golpe. Se separa **según se toca**, siempre con los tests verdes y con el mismo resultado de `GBSim --seed=2 --days=2 --clans=3 --members=6 --combat --hostile` (se compara un resumen de las tablas de telemetría antes y después de cada módulo).

**Hecho (refactor sin cambio de comportamiento):**

- **Configuración agrupada.** `TribeConfig` ya no es una lista plana: `Simulation` (`DecisionSeconds`, `PerceptionSeconds`, `LodUpdateSeconds`, `NeedSeconds`, `BusyThinkSeconds`, `IdleThinkSeconds`, `ThinkPerFrame`, `LogLines`), `Economy` (lo de comida y stock más `Camp`, `CampY`, `StoreCapacity`, `DepositAt`, `Swings`, `FireSeconds`, `UpkeepSeconds`, `FireWoodRequired`, `FireStoneRequired`), `Exploration` (`ExploreRadius`, `RadiusPerTrip`, `FreshSeconds`), `Community` (`ShareSeconds`, `CompanyRadius`, `NearCampRadius`, `NearFireRadius`, `Hearing`; no se llama `Social` porque `Config.Social` son las reglas de vida social) y `Performance` (`DetailRadius`, `DetailMargin`). Los valores por defecto son los de antes.
- **`SettlementPlanner`** (`GB/Game/SettlementPlanner.h/.cpp`). `PlanBuildingNeeds(const SettlementFacts&)` devuelve la lista de obras que el clan quiere, ordenada por urgencia (empates por id); `PlaceSite(const SitePlacement&)` busca el sitio. `SettlementFacts` es una instantánea (sitios hechos y abiertos, vivos, heridos, comida perdida, almacén lleno, cuántas obras caben a la vez y callbacks `Known`/`Payable`/estaciones) que `Tribe::BuildingNeeds` reúne; `Tribe::BuildingNeeds` y `PlanSettlement` son la fachada. Ya devuelve una lista priorizada y `MaxOpenSites` fija cuántas puede haber abiertas: **F2** consiste en dejar que `PlanSettlement` empiece varias de la lista y en dar a los trabajadores un reparto por prioridad (hoy empieza solo la primera). Test: `settlement_planner`.
- **`Schedule`** (`GB/Game/Schedule.h/.cpp`). `PartOfDay` (día, anochecer, noche según estación y turno de noche), `IsEvening`, `ScheduleFactor` (lo que la agenda hace de una opción; el rol manda sobre el clan) con la forma del día en `DayShape`. `Tribe::AgendaFactor` y las cuatro comprobaciones de "tarde" de la vida social lo usan. **F5** (bloques de horario, niñez/escuela) crece aquí. Test: `schedule`.
- **`NodeClaims`** (`GB/Game/Logistics.h`, solo cabecera): quién va a qué nodo (nadie va dos veces al mismo). Es la primera pieza de `Logistics`; el transportista (**F3**) reclamará con ella almacenes y obras.

**Pendiente (no se ha forzado, por lo que cuesta):**

- **`Logistics` completo**: las demandas de material y los depósitos (`PlanEconomy`, `PostEconomyJobs`, `EconomyOptions`, `DepositPlace`/`DepositAt`, `InFlight`) leen a la vez la despensa, el tablón de trabajos, las mochilas y los cuerpos; separarlos exige antes un contexto de trabajos (`JobBoard`) y de inventarios que hoy no existe. Siguiente paso razonable: sacar `Wants()`/`ProductionPlan` a una función pura como se hizo con `BuildingNeeds`.
- **`Household`/`Lifecycle`**: camas (`SleepSpot`, `Beds`, `ShelterNeeds`), partos (`Born`, `SocietyEvents`) y `IsChild` están repartidos entre `TribeSociety.cpp`, `TribeSocial.cpp` y `Settlement.cpp` y dependen de `Society` y de las construcciones; hacerlo bien es la primera tarea de F4.
- **`RaidPlanner`** (de `TribeConflict.cpp`) y telemetría/visuales como observadores: sin empezar.
- Enganches abiertos de otras ramas (`PRIORITY-GRAPH HOOK` en `Tribe::Decide`) no se han tocado: `Decide` sigue en `Tribe.cpp`.

Cada módulo recibe una interfaz estrecha y se prueba solo, sin una `Tribe` entera.

## 7. Orden de trabajo y prueba de cada paso

| Fase | Qué | Prueba (headless) |
|---|---|---|
| F0 | `master` en verde: arreglar los 7 fallos del test social, subir | `ctest` completo |
| F1 | GBSim con contenido de la era + telemetría de obras | laboratorio: obras por clan |
| F2 | Obras con prioridad y varias a la vez | ≥2 casas terminadas en 1 año simulado, 5 semillas |
| F3 | Transportistas | tiempo medio de obra baja |
| F4 | Casas por familia, mejoras | cada familia con casa asignada; duermen dentro |
| F5 | Horario | gráfica de actividad por hora |
| Hijos | Partos múltiples, intervalos por recursos, niñez | niños con 0 trabajos; clanes > 6 miembros en 3 años |
| Clanes | Causas de muerte y hostilidad | ≥ 2 clanes vivos tras 3 años en ≥ 3 de 5 semillas |
| UX | Paneles y selección | capturas headless del Editor |
| Aspecto | Vestimenta por datos | capturas por oficio |

## 8. Pendiente fuera del plan

- Las 7 comprobaciones del test social que siguen en rojo (nadie conversa porque los trabajos de recolección ocupan todo el tiempo; ver F0).
- Un mensaje concreto de error al lanzar varias tribus, para reproducirlo.
- La malla de personajes que desaparece al salir de pantalla: no se reproduce con dos escenarios headless; hace falta modo de cámara, número de clanes, calidad y modelo.
