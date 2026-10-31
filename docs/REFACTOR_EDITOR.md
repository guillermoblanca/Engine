# Guía de refactor


## Objetivo

Estas son las notas que he generado de pendientes antes de irme el fin de semana. Actualmente he generado la base del proyecto, el problema es que esta base esta generada sin haber desacoplado realmente los sistemas y utilizado una capa de abstracción. Esto genera una fricción con código monolítico, falta de separación de responsabilidad, no se siguen los principios solid. 

Una de las cosas que esta ocurriendo es que se ha perdido el seguimiento de qué hace cada sistema y no se muy bien ni abrir el editor ni qué sistemas dependen de cuales. 

Una de las tareas que tengo pendientes es entender como funciona el sistema, desacoplar lo máximo posible todo y centrarme en resolver los siguientes problemas que listaré a continuación. 

## Editor

El editor debe separar ser una capa de abstracción para poder debuggear el juego, generarlo y añadir modificaciones. Debe tener una capa de abstracción tanto para los menus, que, ahora mismo no existiria o no estoy seguro que este principio se este cumpliendo. 

### Mapa técnico actual

El editor no es sólo una ventana de ImGui. Es una aplicación que coordina una escena editable, un mundo generado, sistemas de gameplay, actores, simulación, persistencia y varias vistas de diagnóstico. El punto de entrada público es `EditorApp`, que todavía posee la mayoría de los sistemas y registra sus comandos.

#### Cambios recientes integrados

El último pull añadió cambios de interacción que afectan a las fronteras entre mundo, actores y presentación:

- cada nodo interactivo puede exponer seis puestos alrededor suyo;
- el sistema de interacción conserva la información necesaria para presentar el puesto o punto de uso;
- el modo juego muestra el aviso `Interact [E]` y el texto de la interacción bajo el nodo cercano;
- los modelos FBX relativos se buscan desde la raíz del repositorio, reduciendo la dependencia del directorio de trabajo del ejecutable.

Estos cambios no deben duplicarse en la futura capa de UI. La interacción debería entregar un `InteractionPrompt` o snapshot equivalente con posición, tecla, texto y objetivo; la presentación sólo debe dibujarlo. La selección de puesto, reserva y ejecución deben permanecer en el dominio de interacción.

#### Capas y responsabilidades

| Capa | Archivos principales | Responsabilidad actual | Riesgo de acoplamiento |
|---|---|---|---|
| Aplicación y ciclo de frames | `EditorApp.cpp`, `ImGuiLayer.cpp` | Inicialización, ventana, input, `OnUpdate`, `OnRender` y cierre | Alto: el ciclo conoce UI, simulación y guardado |
| Escena | `SceneEditor.cpp`, `SceneEditor.h`, `WorldInstance.cpp` | Entidades, jerarquía, inspector, undo/redo, guardado de escenas y picking | Medio: la escena dispara sincronizaciones de gameplay |
| Comandos | `Commands.cpp`, `Commands.h`, registro en `EditorApp.cpp` y módulos gameplay | Superficie textual para consola, scripts, tests y automatización | Alto: algunos flujos internos construyen strings y llaman `Execute` |
| Gameplay | `src/Gameplay/EditorGameplay.cpp` y módulos vecinos | Navegación, recursos, ecología, interactivos, jardín, abilities y estado del mundo | Alto: los handlers acceden directamente a muchos miembros de `EditorApp` |
| Tribus y sociedad | `Gameplay/EditorClans.cpp`, `EditorSocial.cpp`, `EditorExplore.cpp`, `EditorEconomy.cpp` | Clanes, tribus, relaciones, conocimiento, regiones, economía y campos | Alto: creación de actores, persistencia y UI están mezcladas |
| Combate | `Gameplay/EditorConflict.cpp` | Combate, hostilidad, raids, heridas y restauración del estado de combate | Medio/alto: usa actores, tribus, escena y save/load |
| Simulación y QA | `Gameplay/EditorQA.cpp`, `Gameplay/EditorSave.cpp` | Ticks fijos, invariantes, replays, telemetry y autosave | Alto: replay y simulación dependen del dispatcher de comandos |
| Visuales | `EditorVisuals.cpp`, `GamePanels.cpp`, `GameMode.cpp` | Visualización de entidades, tribus, HUD y modo juego | Medio: paneles consultan datos internos del dominio |
| Animación | `EditorAnimCommands.cpp`, `EditorAnimPanels.cpp`, `EditorAnimBindings.cpp`, `AnimBindingsUi.h` | Timeline, montajes, grafo, bindings contextuales y pruebas sobre actores | Medio: la UI ya tiene una superficie propia, pero aún vive en `EditorApp` |
| IA | `EditorPriorityGraph.cpp`, overlays de IA/navegación en gameplay | Grafo de prioridades, depuración de navegación, memoria y coste por frame | Medio: falta una consulta estable de diagnóstico independiente de ImGui |

#### Flujo de ejecución

1. `EditorApp` se construye y registra propiedades, comandos, comandos de guardado, animación, juego, QA y visuales.
2. `OnUpdate(dt)` procesa líneas de script pendientes y input del editor.
3. Las órdenes pasan por `Execute` y `CommandRunner`; una orden puede modificar la escena.
4. Si cambia la escena, `SyncGameplay` reconstruye los sistemas de abilities, jardín, recursos, ecología, interactivos y combate a partir de las entidades.
5. `StepSimulation` acumula tiempo y avanza la simulación en ticks fijos de 60 Hz. En ese flujo se actualizan mundo, recursos, actores, tribus, combate, QA y telemetry.
6. `OnRender` construye la vista 3D y los paneles de ImGui. La UI puede emitir comandos, por lo que presentación y dominio todavía están conectados.
7. El guardado serializa escena y estado de gameplay; el replay vuelve a ejecutar las órdenes compatibles y compara el resultado determinista.

#### Dependencias que deben conservarse

- La escena es la fuente persistente de entidades, pero los sistemas de gameplay mantienen estado derivado en memoria.
- `SyncGameplay` debe ejecutarse después de cargar una escena o modificar entidades que contienen componentes de gameplay.
- El replay no debe registrar órdenes de edición de escena que no sean reproducibles como simulación.
- El tick de simulación debe continuar siendo fijo a 60 Hz.
- Los paneles y los scripts deben seguir usando los mismos nombres públicos de comandos mientras dure la extracción.
- Los actores deben buscarse por nombre en las adaptaciones de tribu; no se deben conservar punteros que puedan quedar inválidos al reconstruir la escena.

#### Árbol de dependencias del editor

El siguiente árbol describe el flujo principal actual. Una flecha significa que el sistema de la izquierda conoce, invoca o consume datos del sistema de la derecha. No implica propiedad de memoria en todos los casos; la propiedad debe aclararse durante la extracción.

```text
EditorApp
├── Application / ventana / input
│   └── ImGuiLayer
├── CommandRunner
│   ├── SceneEditor
│   │   ├── Scene / EntityWorld
│   │   ├── Properties
│   │   └── undo / redo / picking
│   ├── WorldInstance
│   │   ├── WorldDesc / seed
│   │   ├── Terrain / water / weather
│   │   └── procedural generation
│   ├── Gameplay coordination
│   │   ├── ActorWorld
│   │   │   ├── Actor
│   │   │   ├── Character / Animator
│   │   │   └── NavGrid
│   │   ├── GameplayWorld
│   │   │   ├── AbilitySystemComponent
│   │   │   ├── GameplayTag / AttributeSet
│   │   │   └── abilities / effects / tasks
│   │   ├── GardenSystem
│   │   ├── ResourceSystem
│   │   ├── EcologySystem
│   │   ├── InteractiveSystem
│   │   ├── BuildingSystem
│   │   ├── CombatSystem
│   │   └── NavGrid
│   ├── Tribe / ClanWorld
│   │   ├── Tribe
│   │   │   ├── members / roles / jobs
│   │   │   ├── needs / schedule / society
│   │   │   ├── economy / fields / settlement
│   │   │   └── ITribeBody -> ActorWorld
│   │   ├── ClanWorld
│   │   ├── diplomacy / hostility
│   │   ├── regions / exploration / knowledge
│   │   └── TribeVisuals
│   ├── GameMode / PlayerMode
│   │   ├── ActorWorld
│   │   ├── Tribe / ClanWorld
│   │   ├── Camera / GameView
│   │   └── Game HUD / GamePanels
│   ├── Animation tools
│   │   ├── AnimManifest / Model
│   │   ├── AnimBindings
│   │   ├── montage / timeline
│   │   ├── priority graph
│   │   └── Actor::Animator
│   └── QA / telemetry
│       ├── SimMonitor
│       ├── replay
│       └── TelemetrySink
├── Save / Load / Replay
│   ├── SceneEditor -> scene data
│   ├── GameplayWorld -> gameplay components
│   ├── ActorWorld -> actor state
│   ├── Tribe / ClanWorld -> social state
│   ├── Combat / hostility -> combat state
│   └── replay -> CommandRunner + fixed simulation
└── Presentation
    ├── viewport / renderer
    ├── hierarchy / inspector / properties
    ├── gameplay panels
    ├── animation panels
    ├── tribe / clan panels
    ├── QA / tests panels
    └── debug overlays
```

#### Dependencias por fase de ejecución

```text
Entrada de usuario o script
        │
        ▼
CommandRunner
        │
        ├── comando de escena ───────► SceneEditor
        │                                  │
        │                                  └── cambio de entidad
        │                                          │
        │                                          ▼
        ├── comando de mundo ───────► WorldInstance
        │
        ├── comando de gameplay ────► sistemas gameplay
        │                                  │
        │                                  └── cambios persistentes en Scene
        │
        ├── comando de tribu ───────► Tribe / ClanWorld / ActorWorld
        │
        ├── comando de animación ───► AnimBindings / Animator / Model
        │
        └── comando de save/replay ─► serialización / replay
                                           │
                                           ▼
                                    SyncGameplay cuando procede
                                           │
                                           ▼
OnUpdate(dt)
        │
        ├── procesa scripts pendientes
        ├── input del editor o GameMode
        ├── StepSimulation
        │   ├── tick fijo de mundo
        │   ├── recursos / ecología / jardín
        │   ├── actores / navegación
        │   ├── tribus / clanes / combate
        │   ├── QA / telemetry
        │   └── autosave / replay
        └── actualización de cámara y visuales
                                           │
                                           ▼
OnRender()
        │
        ├── viewport y escena
        ├── paneles
        └── overlays de debug
```

#### Dependencias de datos y dirección deseada

| Sistema | Depende actualmente de | Dirección deseada |
|---|---|---|
| Paneles ImGui | `EditorApp`, sistemas internos y propiedades | Snapshots de consulta + dispatcher de acciones |
| CommandRunner | Lambdas que capturan `EditorApp` | Handlers por dominio con servicios estrechos |
| Gameplay runtime | Escena, mundo, actores y recursos de `EditorApp` | `GameplayServices` no propietario |
| Tribe / ClanWorld | Actores, navegación, gameplay, escena y contenido | `TribeServices` + consultas de tribu |
| Save / Replay | `EditorApp::Execute` y estado completo | Interfaces explícitas de serialización y simulación |
| Animación | Actor, modelo, manifest y paneles | `AnimationServices` + modelo de preview |
| QA / telemetry | pasos de simulación y comandos | Eventos o snapshots de diagnóstico |
| Debug overlays | cámara, terreno y sistemas de dominio | Datos de debug preparados fuera de ImGui |

#### Fronteras que no deben cruzarse

Durante el refactor, estas dependencias deben considerarse señales de diseño incorrecto:

- Un panel no debería llamar a otro comando construyendo una cadena si puede invocar una acción tipada.
- Un sistema de dominio no debería depender de `ImGui`, `EditorApp` completo ni de una ventana.
- La simulación no debería leer estado visual para decidir reglas de gameplay.
- El parser no debería crear actores, entidades ni modificar sistemas.
- Save/Load no debería depender del texto mostrado por un panel.
- El overlay de IA no debería construir tribus, cambiar prioridades ni ejecutar comandos.
- `ActorBody` no debería conservar un `Actor*` entre ticks o reconstrucciones.

### Guía paso a paso para separar las capas

Esta guía propone el orden de extracción y el resultado esperado de cada paso. La regla general es no mover código sólo porque un archivo sea grande: primero se identifica el flujo, después se crea una frontera pequeña, se migra una responsabilidad y finalmente se valida con las mismas pruebas. Cada paso debería poder revertirse sin cambiar los formatos de escenas, saves o replays.

#### Paso 0: preparar la línea base

Antes de separar capas:

1. Comprobar el árbol de trabajo y conservar los cambios locales.
2. Construir `GBEditor`, `GBEditorTests` y `Editor`.
3. Ejecutar el test `editor`.
4. Ejecutar los replays del editor y anotar qué pruebas se omiten por falta de OpenGL.
5. Registrar qué comandos se prueban manualmente y qué mensajes devuelven.
6. Activar `GB_WARNINGS_AS_ERRORS=ON` y anotar warnings preexistentes sin mezclarlos con el refactor.

El objetivo es poder distinguir una regresión de una limitación que ya existía.

#### Paso 1: separar aplicación y ciclo de frames

**Archivos actuales:** `EditorApp.cpp`, `EditorApp.h`, `ImGuiLayer.cpp`, `ImGuiLayer.h`.

**Problema que resuelve:** `EditorApp` conoce simultáneamente la ventana, ImGui, input, cámara, simulación, scripts, render, paneles y persistencia. Como consecuencia, cambiar el ciclo de un frame obliga a recompilar y entender todo el editor, y no existe una forma clara de avanzar la simulación sin crear la UI.

**Objetivo de la separación:**

```text
Application
  └── EditorHost
        ├── EditorFrameLoop
        ├── EditorCommandService
        ├── EditorSimulation
        └── EditorPresentation
              └── ImGuiLayer
```

##### 1.1. Documentar el ciclo actual

Antes de editar, marcar en `EditorApp.cpp`:

- inicialización de ventana y renderer;
- registro de comandos y propiedades;
- lectura de scripts pendientes;
- procesamiento de input;
- acumulación del tiempo;
- `StepSimulation`;
- actualización del modo juego;
- preparación de la escena;
- render de viewport;
- render de paneles;
- cierre y liberación de recursos.

No mover todavía métodos. Esta lista será el contrato del ciclo nuevo.

##### 1.2. Crear el estado de frame

Crear `Editor/src/EditorFrame.h`:

```cpp
namespace GB::Editor
{
    struct EditorFrame
    {
        float DeltaSeconds = 0.0f;
        double ElapsedSeconds = 0.0;
        uint64_t SimulationTick = 0;
        bool ViewportHovered = false;
        bool InputCaptured = false;
    };
}
```

El estado debe contener datos del frame, no referencias a `EditorApp`. El tick fijo de gameplay debe seguir siendo responsabilidad de la simulación, no del render.

##### 1.3. Crear el coordinador del ciclo

Crear `Editor/src/EditorFrameLoop.h/.cpp` con una interfaz similar a:

```cpp
class EditorFrameLoop
{
public:
    void BeginFrame(float deltaSeconds);
    void ProcessInput();
    void ProcessCommands();
    void UpdateSimulation(float deltaSeconds);
    void Render();
    const EditorFrame& Frame() const;
};
```

En la primera extracción puede recibir interfaces estrechas:

```cpp
struct EditorFrameServices
{
    CommandService& Commands;
    SimulationService& Simulation;
    EditorPresentation& Presentation;
    InputService& Input;
};
```

No debe recibir `EditorApp&`. Si necesita más de cuatro o cinco dependencias, la frontera está ocultando un problema.

##### 1.4. Reducir `EditorApp::OnUpdate`

`EditorApp::OnUpdate` debe quedar como una fachada:

```cpp
void EditorApp::OnUpdate(float dt)
{
    m_FrameLoop.Update(dt);
}
```

La implementación se mueve en este orden:

1. procesamiento de líneas de script;
2. input de escena y cámara;
3. input del modo juego;
4. acumulador de simulación;
5. actualización visual no renderizada;
6. telemetry y QA.

Después de cada movimiento ejecutar el test `editor` y un replay.

##### 1.5. Separar `OnRender`

Crear `EditorPresentation.h/.cpp` para coordinar:

- viewport;
- menú;
- propiedades;
- jerarquía;
- inspector;
- consola;
- paneles de gameplay;
- paneles de animación;
- overlays.

`EditorApp::OnRender` sólo debería preparar el frame y delegar:

```cpp
void EditorApp::OnRender()
{
    m_Presentation.Draw(m_RenderContext, m_Frame);
}
```

`EditorPresentation` puede depender de ImGui. Los sistemas de simulación no.

##### 1.6. Limitar `ImGuiLayer`

`ImGuiLayer` debe encargarse exclusivamente de:

- crear y destruir el contexto ImGui;
- iniciar y terminar un frame ImGui;
- conectar GLFW/OpenGL;
- traducir eventos de ventana;
- exponer el estado de captura de mouse/teclado.

No debe conocer tribus, abilities, saves, actores ni comandos de gameplay. La UI concreta debe estar en `EditorPresentation` y en paneles especializados.

##### 1.7. Validación de la capa de aplicación

Crear una prueba headless de `EditorFrameLoop` con servicios falsos:

- un frame con `dt = 1/60` procesa una orden;
- un frame sin ventana puede avanzar la simulación;
- varios frames acumulan correctamente ticks fijos;
- el render no cambia el número de ticks;
- input capturado por ImGui no llega al modo juego.

**Resultado esperado:** `EditorApp.cpp` coordina construcción y dependencias; `EditorFrameLoop` coordina tiempo; `ImGuiLayer` sólo gestiona ImGui; `EditorPresentation` sólo compone vistas.

#### Paso 2: separar comandos y acciones

**Archivos actuales:** `Commands.cpp/.h`, registro de comandos en `EditorApp.cpp` y handlers en `Gameplay/*.cpp`.

**Problema que resuelve:** los comandos son a la vez parser, validación, ejecución y API interna. Los paneles y sistemas construyen strings como `"ability activate ..."`, lo que introduce errores de escape, mensajes inconsistentes y dependencia circular con `EditorApp`.

**Qué crear:**

- `EditorGameplayParsing.h/.cpp` para números, límites y argumentos;
- `EditorCommandContext.h` para dependencias de un handler;
- `EditorAction.h` para intenciones tipadas;
- handlers por dominio: navegación, recursos, tribus, ecología, abilities y escena.

**Orden:**

1. Mantener `CommandRunner` como adaptador textual.
2. Parsear una línea a una request tipada.
3. Validar la request sin modificar estado.
4. Ejecutar la request mediante un servicio.
5. Formatear `CommandResult` en el borde textual.
6. Migrar primero `NavCommand` y después los demás comandos de fase 1.
7. Prohibir gradualmente llamadas internas a `EditorApp::Execute`.

**Resultado esperado:**

```text
texto -> CommandRunner -> request tipada -> servicio de dominio -> resultado
UI    -> acción tipada --------------------^
```

#### Paso 3: separar escena y edición

**Archivos actuales:** `SceneEditor.cpp/.h`, `Properties.cpp`, `WorldInstance.cpp`.

**Problema que resuelve:** selección, propiedades, undo/redo, picking y construcción de gameplay están mezclados. Una modificación de inspector puede requerir conocer recursos, renderer y sistemas de abilities.

**Qué crear:**

- `SceneSelectionContext`;
- `SceneEditService`;
- `SceneQuery`;
- `SceneChangeEvent` o un mecanismo equivalente de notificación.

**Orden:**

1. Mantener `SceneEditor` como dueño de `Scene` y del historial.
2. Extraer selección y foco a `SceneSelectionContext`.
3. Extraer operaciones `create`, `delete`, `duplicate`, `parent` y `set` a `SceneEditService`.
4. Hacer que undo/redo registre operaciones del servicio.
5. Emitir una notificación cuando cambie una entidad con componentes de gameplay.
6. Hacer que runtime escuche esa notificación en lugar de llamar a `SyncGameplay` desde muchos comandos.

**Resultado esperado:** la escena no conoce reglas de tribu, economía o combate; sólo entidades, componentes, transformaciones e historial.

#### Paso 4: separar mundo y generación procedural

**Archivos actuales:** `WorldInstance.cpp/.h` y código de terreno, agua, clima y generación del motor.

**Problema que resuelve:** generar un mundo, modificar su representación visual y sincronizar navegación son operaciones diferentes, pero actualmente se coordinan desde `EditorApp`.

**Qué crear:**

- `WorldGenerationRequest`;
- `WorldSnapshot`;
- `WorldGenerationService`;
- `WorldVisualService`;
- `NavigationSyncService`.

**Orden:**

1. Mantener `WorldInstance` como dueño del mundo generado.
2. Extraer la descripción de seed, biome, recursos y calidad.
3. Separar generación de datos de creación de entidades visuales.
4. Separar actualización de clima de aplicación de color, nieve y luces.
5. Hacer que navegación consuma un snapshot del terreno.
6. Probar la misma seed dos veces y comparar la salida.

**Resultado esperado:** el mundo puede generarse y probarse headless sin ImGui ni paneles.

#### Paso 5: separar runtime de gameplay y simulación

**Archivos actuales:** `Gameplay/EditorGameplay.cpp`, `EditorSave.cpp`, `EditorQA.cpp`, `GameMode.cpp`.

**Problema que resuelve:** sincronización, ticks, recursos, estaciones, actores, combate, QA y autosave se coordinan desde métodos de `EditorApp`.

**Qué crear:**

- `GameplayServices`;
- `SimulationTick`;
- `EditorGameplayRuntime`;
- `SimulationPhase`;
- `SimulationDiagnostics`.

```cpp
struct SimulationTick
{
    uint64_t Index = 0;
    float DeltaSeconds = 1.0f / 60.0f;
    double ClockSeconds = 0.0;
};
```

**Orden:**

1. Extraer `SyncGameplay` sin cambiar su orden actual.
2. Separar `LoadGameplayFromScene`, `BindGameplayServices` y `SyncGameplayVisuals`.
3. Extraer `StepResources` y `StepWorld`.
4. Añadir `SimulationTick` sin cambiar la frecuencia.
5. Definir fases: mundo, recursos, actores, tribus, combate, QA y telemetry.
6. Registrar duración y número de operaciones de cada fase.
7. Hacer que `EditorApp` sólo envíe `dt` al runtime.

**Resultado esperado:** un tick de gameplay puede ejecutarse sin crear una ventana y los replays siguen siendo deterministas.

#### Paso 6: separar actores, tribus y clanes

**Archivos actuales:** `Gameplay/EditorClans.cpp`, `EditorGameplay.cpp`, `EditorSocial.cpp`, `EditorExplore.cpp`, `EditorEconomy.cpp`.

**Problema que resuelve:** `tribe build`, restauración, `ActorBody`, persistencia, actores, economía y UI comparten el mismo contexto mutable.

**Qué crear:**

- `EditorTribeController.h/.cpp`;
- `TribeServices`;
- `TribeBuildRequest`;
- `TribeBuildPlan`;
- `TribeSummary`;
- `ActorBody.cpp` privado del controlador.

**Orden:**

1. Mover `ActorBody` sin cambiar su búsqueda por nombre.
2. Separar parseo y validación de `tribe build`.
3. Crear `TribeBuildPlan` antes de modificar la escena.
4. Validar modelo, nombres, workers, posición y recursos.
5. Aplicar el plan en un commit explícito.
6. Mover `RestoreTribe`, `AiCommand` y consultas de estado.
7. Exponer `TribeSummary` para los paneles.
8. Mantener la serialización actual hasta tener pruebas de compatibilidad.

**Resultado esperado:** un fallo de construcción no deja actores, entidades o miembros parcialmente creados.

#### Paso 7: separar animación y herramientas de IA

**Archivos actuales:** `EditorAnimCommands.cpp`, `EditorAnimPanels.cpp`, `EditorAnimBindings.cpp`, `EditorPriorityGraph.cpp`.

**Problema que resuelve:** la UI de animación y el grafo de prioridades conocen directamente actores, modelos, comandos y estado del editor.

**Qué crear:**

- `AnimationPreviewService`;
- `AnimationBindingService`;
- `AnimationQuery`;
- `PriorityGraphQuery`;
- `AnimationPanelActions`.

**Orden:**

1. Mantener `AnimBindingsUi` como estado de edición de la vista.
2. Mover carga y validación de bindings a `AnimationBindingService`.
3. Hacer que el panel reciba una consulta de acciones, clips y contexto.
4. Separar preview de animación de animación de gameplay.
5. Exponer el grafo de prioridades como datos consultables.
6. Añadir pruebas headless para bindings generales, contextuales y por modelo.

**Resultado esperado:** cambiar un panel o preview no cambia la simulación ni obliga a `EditorApp` a conocer detalles de resolución de clips.

#### Paso 8: separar persistencia, replay, QA y telemetry

**Archivos actuales:** `Gameplay/EditorSave.cpp`, `Gameplay/EditorQA.cpp`, `TelemetrySink`, `Replay`.

**Problema que resuelve:** save/load y replay atraviesan `EditorApp::Execute`, mientras QA y telemetry dependen de detalles del ciclo y pueden quedar desincronizados.

**Qué crear:**

- `EditorSaveService`;
- `EditorReplayService`;
- `SimulationSnapshot`;
- `QaSnapshot`;
- `FrameDiagnostics`.

**Orden:**

1. Mantener los formatos JSON existentes.
2. Separar captura de estado de escritura en disco.
3. Separar restauración de escena, gameplay, actores, tribus y combate.
4. Definir qué comandos son grabables y cuáles son sólo de edición.
5. Hacer que replay invoque acciones tipadas o un adaptador controlado.
6. Comparar snapshots, no mensajes de UI.
7. Emitir QA y telemetry desde fases de simulación.

**Resultado esperado:** guardar/cargar no depende de un panel, y replay puede ejecutarse headless con resultados reproducibles.

#### Paso 9: separar presentación y paneles

**Archivos actuales:** `EditorApp.cpp`, `GamePanels.cpp`, `EditorVisuals.cpp`, paneles de gameplay y animación.

**Problema que resuelve:** los paneles leen directamente `m_Gameplay`, `m_Actors`, `m_Props`, `m_SceneEditor` y construyen acciones mediante strings.

**Qué crear:**

- `EditorPresentation`;
- `WorldPanel`;
- `ScenePanel`;
- `GameplayPanels`;
- `TribePanel`;
- `AnimationPanel`;
- `DebugOverlayRenderer`;
- snapshots de consulta por panel.

**Orden:**

1. Extraer primero paneles que sólo leen datos.
2. Sustituir accesos directos por snapshots.
3. Sustituir comandos construidos como strings por acciones tipadas.
4. Dejar ImGui únicamente en la capa de presentación.
5. Mantener overlays de debug separados de reglas de dominio.
6. Probar que cerrar un panel no detiene la simulación.

**Resultado esperado:** la UI muestra el estado, pero no contiene reglas de gameplay ni es necesaria para ejecutar el mundo.

#### Paso 10: consolidar CMake, documentación y pruebas

Después de cada extracción:

1. Añadir los nuevos `.cpp` a `Editor/CMakeLists.txt`.
2. Mantener headers públicos mínimos y headers privados junto a su implementación.
3. Actualizar el árbol de dependencias y los enlaces a archivos.
4. Ejecutar build del editor y tests CPU.
5. Ejecutar replays afectados.
6. Ejecutar tests de animación si se toca Actor/Animator/Model.
7. Revisar `git diff --check`.
8. Confirmar que no se añadieron builds, saves, escenas generadas o binarios.

La secuencia recomendada de commits es:

```text
1. Extract gameplay parsing
2. Validate gameplay command arguments
3. Introduce editor frame state
4. Extract editor frame loop
5. Extract gameplay runtime
6. Extract tribe controller
7. Separate typed actions from commands
8. Extract animation and AI queries
9. Extract presentation panels
10. Add focused tests and update documentation
```

### Estado del refactor

- [x] Agrupación física inicial de los módulos de gameplay en `Editor/src/Gameplay/`.
- [x] `EditorGameplayParsing.h/.cpp` creado como parser común.
- [x] `NavCommand` migrado para rechazar números inválidos, argumentos sobrantes y `load` fuera de rango.
- [x] Pruebas iniciales de floats, enteros, argumentos ausentes, errores y uso.
- [ ] Migrar `TribeCommand`, `EcologyCommand`, `InteractivesCommand`, `ResourcesCommand` y `GardenCommand`.
- [ ] Crear un contexto de servicios estrecho para evitar que los módulos conozcan `EditorApp` completo.
- [ ] Extraer runtime de sincronización y simulación.
- [ ] Extraer controlador de tribus y persistencia.
- [ ] Separar handlers de comandos de las operaciones tipadas de dominio.
- [ ] Centralizar selección de actor, entidad y foco de cámara.
- [ ] Extraer paneles de gameplay y depuración visual.

La fase actual debe priorizar cambios mecánicos y reversibles. No conviene mover UI ni modificar serialización hasta que el parser y el runtime tengan fronteras claras.

#### Adaptación pendiente por los cambios de interacción

Antes de extraer los paneles del modo juego:

1. localizar dónde `GameMode` calcula `PromptPos`, `Prompt` y `HasPromptPos`;
2. encapsular esos datos en una consulta de interacción, sin exponer el estado completo de `GameMode`;
3. distinguir el nodo cercano, el puesto elegido y la acción disponible;
4. mantener la tecla configurable mediante `ControlRules`, con `E` sólo como valor por defecto visual;
5. comprobar que el prompt desaparece si el nodo, actor o puesto dejan de ser válidos;
6. probar los seis puestos y la selección cuando hay varios agentes;
7. mover el dibujo del prompt a presentación sólo después de estabilizar la consulta.

Esto conecta directamente con el pendiente de ocupación de nodos: añadir puestos visuales no resuelve todavía reservas, concurrencia ni selección alternativa de un agente.


### Comandos y flujos que deben verificarse

El editor tiene dos superficies: comandos directos y acciones de UI que en varios casos terminan llamando a `Execute`. No se debe declarar un comando roto sólo porque falte una escena o un recurso; cada caso debe probarse con una escena mínima y registrar el mensaje devuelto.

1. Guardar partida
2. Cargar partida
3. Reanudar
4. Guardar escena
5. Cargar escena
6. Preset de partida
7. Alt + P no inician modo editor

#### Matriz mínima de comandos

| Área | Comandos que deben conservarse | Validación |
|---|---|---|
| Escena | `scene new`, `scene save`, `scene load`, `entity create`, `entity delete`, `undo`, `redo` | Crear, guardar, cargar y comprobar selección/jerarquía |
| Mundo | `generate`, `time`, `weather`, `quality` | Generar sin ventana y comprobar estado determinista |
| Navegación | `nav status`, `nav rebuild`, `nav sync`, `nav path` | Números válidos, texto inválido, carga fuera de rango y escena sin navegación |
| Gameplay | `resources`, `interactives`, `garden`, `ability`, `ecology` | Construcción, consulta de estado y sincronización tras guardar/cargar |
| Tribus | `tribe`, `clan`, `ai`, `knowledge`, `region`, `economy`, `field` | Actores, miembros abstractos, restauración y eliminación |
| Combate | `combat`, `hostile`, `raid` | Daño, muerte, hostilidad, persistencia y replay |
| Animación | `player`, `montage_*`, `graph_*`, `anim_bindings`, `anim_try` | Binding contextual, modelo seleccionado y guardado de configuración |
| Juego | `play`, `game`, `camera game` | Inicio, HUD, acciones del jugador, guardar/cargar y reanudar |
| Diagnóstico | `qa`, `telemetry`, `test`, `screenshot` | Ejecución headless y mensajes reproducibles |

#### Pruebas existentes relevantes

El build del editor genera una biblioteca reutilizable y una prueba CPU (`GBEditorTests`). Además existen scripts de ejecución del editor que cubren replays, cámara, clanes, demos de gameplay y modo juego. Los nuevos cambios de animación añaden pruebas de bindings generales y bindings sobre un `ActorWorld`.

La validación de cada extracción debe seguir este orden:

1. `cmake --build build --config Debug --target GBEditorTests`.
2. `ctest --test-dir build -C Debug -R editor --output-on-failure`.
3. Ejecutar los replays afectados.
4. Ejecutar el conjunto completo si cambia persistencia, simulación o registro de comandos.
5. Repetir con `GB_WARNINGS_AS_ERRORS=ON`; si falla por warnings preexistentes, documentar archivo y línea sin mezclar esa corrección con el refactor.



### Gameplay

Esto hay que separarlo en otro feedback diferente pero lo añado ahora mismo aqui para tener un solo archivo donde modificarlo, agente, si ves esto desacoplalo para dividir las tareas en dos grupos.


1. La UI del juego superpone el botón de interaccion con el menú inferior de opciones en la pantalla. Cambiar esto subiendo el botón de interacción un poco mas arriba para evitar superposicionamiento. 
2. Falta una vista de arbol para ver las prioridades en las tareas o acciones de mundo de los personajes. La inteligencia artificial ahora mismo presenta dificultades para debuggear que esto podría solucionar
3. Construir edificios esta limitado a que acepten los otros miembros del clan construir un edificio. Esto fue una limitación de diseño pero lo vamos a cambiar. Si el jugador indica que quiere construir algo eligirá donde quiere ponerlo y lo puede construir. 
4. Eliminar restricción de construir un solo edificio
5. Priorizar terminar edificio
6. Llevar recursos a los edificios que quieren ser construidos.
7. Las tareas de un agente cuando construye un edificio deberían ser: llevar todos los recursos al punto, construir, en caso de que haya empezado a construir gasta todos los recursos que tenga el jugador y luego transporta los pendientes al punto de construcción. cuando se inicia una tarea debería priorizarse terminarla. 


## Pase de juego:

Voy a hacer un pase y tomar anotaciones de todo lo que veo que debería cambiarse. 

**Los cambios que se han tocado son del sistema de animaciones. Asi que priorizamos dar feedback de este punto.**


## Sistemas a revisar

- **Animaciones:** bindings por acción, contexto de nodo/herramienta/variante, locomoción, montajes, timeline y grafo. El editor ya tiene paneles y comandos; falta separar la consulta de datos de la presentación.
- **Árbol de comportamiento:** el sistema actual está representado principalmente por el grafo de prioridades (`EditorPriorityGraph.cpp`). Hay que distinguir ese grafo de un árbol de comportamiento clásico antes de crear otra UI.
- **Horario y necesidades:** pertenecen al dominio de tribus y actores; el editor debe mostrar snapshots y no recalcular reglas desde ImGui.
- **Sonidos:** revisar selección de clips, audio del editor, pasos y diferencias entre preview y simulación.
- **Gizmos de navegación:** separar datos de navegación, picking y render del overlay. `nav status` y `nav debug` deben funcionar sin depender de un panel abierto.
- **UI del juego:** separar HUD, paneles de juego, acciones y estado del modo `play`.
- **Nodos y sistema de interacción:** documentar ocupación, reservas, alcance, consumo de recursos y liberación al cancelar o fallar.

Falta hacer que un mismo nodo pueda ser usado por varios agentes y crear un sistema para que, en el caso de que el nodo este ocupado un agente use otro. Falta un modo director

Antes de cambiar esta regla hay que definir:

- si la ocupación es exclusiva por nodo, por punto de interacción o por recurso;
- si la reserva caduca y cómo se libera cuando muere un actor;
- cómo se elige el nodo alternativo;
- cómo se evita que todos los agentes seleccionen el mismo reemplazo;
- qué parte debe aparecer en `ai status`, telemetry y el overlay de depuración.

- Director o GameMode
- Debugging: falta poder saber qué sistema esta consumiendo muchos recursos, al menos debug en modo editor

El editor ya expone parte del coste de IA y navegación. La siguiente mejora debería ser un `FrameDiagnostics` o snapshot equivalente con tiempos de:

- simulación;
- actores y planificación;
- tribus y diplomacia;
- navegación;
- combate;
- sincronización visual;
- render y UI.

El snapshot debe poder consultarse desde consola y telemetry sin depender de ImGui.

## Entorno procedural

La generación procedural ya alimenta varias capas del editor; no debe tratarse como una única operación opaca.

| Sistema | Salida que consume el editor | Preguntas para el refactor |
|---|---|---|
| Ríos y agua | Terreno, navegación, clima y selección de asentamientos | ¿Qué datos son persistentes y cuáles se regeneran por seed? |
| Montañas y terreno | Alturas, obstáculos, cámara y picking | ¿Existe una consulta headless reutilizable para tests? |
| Recursos | Nodos salvajes, parches y economía | ¿Se separa planificación de recursos de creación de entidades? |
| Navegación | NavGrid, rutas, coste y debug | ¿Se puede reconstruir sin UI y con presupuesto determinista? |
| Fertilidad y cultivos | Garden, fields y ecología | ¿Qué snapshot necesita el panel y qué pertenece al dominio? |
| Exploración | Regiones, puntos de interés y memoria | ¿Las regiones son derivadas de seed y configuración? |
| Clima | Estaciones, lluvia, nieve y visuales | ¿Se separan efectos simulados de su representación? |
| Carreteras | Futuro sistema de conectividad y logística | ¿Debe ser entidad, dato procedural o ambos? |
| Editor procedural | Futuro panel para seed, capas y regeneración | Debe permitir inspeccionar cada capa sin modificar savegames accidentalmente |

Regla de diseño: la generación debe producir datos reproducibles a partir de una descripción (`WorldDesc`, seed y configuración). La UI puede pedir una vista previa, pero no debería duplicar las reglas de generación ni escribir directamente en los sistemas de simulación.