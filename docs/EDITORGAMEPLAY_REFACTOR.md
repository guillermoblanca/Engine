# Guía de refactorización de `EditorGameplay`

## Objetivo

Reducir el acoplamiento de [`EditorGameplay.cpp`](../Editor/src/Gameplay/EditorGameplay.cpp) sin cambiar el comportamiento observable del editor, manteniendo:

- comandos de texto compatibles;
- ejecución headless;
- simulación determinista;
- persistencia y replays compatibles;
- paneles de ImGui funcionales;
- compilación con C++20 y warnings como errores.

El archivo actual contiene comandos, coordinación de sistemas, simulación, selección de actores, persistencia de tribus y UI. El objetivo no es dividirlo sólo por número de líneas, sino introducir fronteras de responsabilidad que puedan probarse de forma independiente.

## Estado actual

Responsabilidades detectadas:

1. Sincronización entre escena y gameplay.
2. Binding de recursos, ecología e interactivos.
3. Simulación de estaciones, lluvia, crecimiento y recursos.
4. Navegación y visualización de navegación.
5. Construcción y persistencia de tribus.
6. Adaptación de actores a `ITribeBody`.
7. Comandos de abilities, garden, recursos, ecología e interactivos.
8. Selección de actores e inventarios.
9. Paneles de estado del mundo, tribu y abilities.
10. Depuración visual de IA y navegación.

La clase `EditorApp` es actualmente el punto de acceso a todos esos recursos. Eso hace que cualquier cambio de dominio requiera tocar y recompilar la capa de UI.

## Resultado final propuesto

La estructura objetivo puede ser:

```text
Editor/src/
  Gameplay/
    EditorGameplay.cpp            # fachada y coordinación mínima
    EditorGameplayCommands.cpp    # registro/adaptación de comandos, si sigue siendo necesario
    EditorGameplayParsing.h       # parseo común de argumentos
    EditorGameplayParsing.cpp
    EditorGameplayRuntime.h       # ciclo de sincronización y simulación
    EditorGameplayRuntime.cpp
    EditorTribeController.h        # tribu, actores y persistencia
    EditorTribeController.cpp
    EditorGameplayPanels.cpp      # paneles de gameplay
    EditorGameplayDebug.cpp        # overlays de IA y navegación
```

No es obligatorio usar exactamente estos nombres. Lo importante es que cada archivo tenga una razón única para cambiar.

## Principios que deben guiar el cambio

### 1. No usar la sintaxis de comandos como API interna

La interfaz textual debe ser un adaptador:

```text
texto -> parser -> acción tipada -> dominio
```

Evitar:

```text
dominio -> Execute("otro comando ...")
UI -> construir string -> Execute(...)
```

La UI puede seguir encolando acciones si el ciclo de frames lo necesita, pero debería encolar una acción tipada o un pequeño objeto de intención, no una cadena con comillas y escapes.

### 2. Mantener el comportamiento antes de mejorar el diseño

Durante la primera fase no cambiar:

- nombres de comandos;
- mensajes públicos, salvo errores nuevos de validación;
- nombres de entidades;
- formato de escenas guardadas;
- formato de replays;
- reglas de simulación;
- frecuencia fija de 60 Hz.

Cada extracción debe ser mecánica y validarse antes de cambiar la siguiente responsabilidad.

### 3. Evitar clases que conozcan todo `EditorApp`

Las clases nuevas deben recibir dependencias estrechas. Si una clase necesita diez miembros de `EditorApp`, la frontera no está bien definida.

Preferir:

```cpp
struct GameplayServices
{
    EntityWorld& scene;
    GameplayWorld& gameplay;
    ActorWorld& actors;
    WorldInstance& world;
};
```

frente a:

```cpp
EditorApp& editor;
```

## Plan paso a paso

## Fase 0: preparar una línea base

### Paso 0.1: revisar el estado del árbol

Antes de comenzar:

```powershell
git status --short
```

No revertir cambios locales que no pertenezcan a este trabajo. Si [`EditorGameplay.cpp`](../Editor/src/Gameplay/EditorGameplay.cpp) tiene cambios propios, integrar las extracciones sobre ese estado.

### Paso 0.2: configurar con warnings como errores

Usar el flujo descrito en [`README.md`](../README.md). La configuración debe incluir:

```text
-DGB_WARNINGS_AS_ERRORS=ON
```

### Paso 0.3: ejecutar la línea base

Ejecutar:

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Si el build actual se llama de otra manera, usar el directorio de build existente. Registrar qué pruebas pasan antes de editar. En particular, conservar la referencia de:

- `editor`;
- `editor_replay`;
- `editor_replay_society`;
- `editor_replay_midrun`.

### Paso 0.4: crear una rama o punto de recuperación

No hacer un commit que incluya archivos generados, escenas locales o salidas de build. Crear un commit de checkpoint sólo si el flujo del repositorio lo permite.

## Fase 1: extraer utilidades puras

Esta fase tiene el menor riesgo y permite probar mejoras sin depender de ImGui ni de `EditorApp`.

### Paso 1.1: crear `EditorGameplayParsing`

Extraer de [`EditorGameplay.cpp`](../Editor/src/Gameplay/EditorGameplay.cpp):

- `Quoted`;
- `Join`;
- `Number`;
- parseo de enteros;
- parseo de floats;
- validación de cantidad de argumentos.

Propuesta de interfaz:

```cpp
namespace GB::Editor
{
    struct ParsedNumber
    {
        bool Ok = false;
        double Value = 0.0;
        std::string Error;
    };

    ParsedNumber ParseFloat(
        const std::vector<std::string>& args,
        size_t index,
        const char* name);

    ParsedNumber ParseInteger(
        const std::vector<std::string>& args,
        size_t index,
        const char* name);

    bool RequireArgumentCount(
        const std::vector<std::string>& args,
        size_t minimum,
        size_t maximum,
        const std::string& usage,
        CommandResult& result);
}
```

El tipo exacto puede adaptarse a las convenciones del proyecto. No usar `std::atoi` ni `std::atof` en comandos nuevos.

### Paso 1.2: sustituir conversiones silenciosas

Actualizar primero:

- `NavCommand`;
- `TribeCommand`;
- `EcologyCommand`;
- `InteractivesCommand`;
- `ResourcesCommand`;
- `GardenCommand`.

Ejemplos de errores que deben ser explícitos:

```text
nav path: x1 must be a number
resources build: workers must be an integer
ecology spawn: count must be greater than zero
```

No aceptar silenciosamente `abc` como `0`.

### Paso 1.3: añadir pruebas unitarias

En las pruebas del editor, cubrir:

- float válido;
- entero válido;
- valor ausente;
- texto no numérico;
- rango inválido;
- argumentos sobrantes;
- mensajes de uso.

### Criterio de terminado

- ningún comando de gameplay depende de `atoi`/`atof`;
- los replays existentes siguen pasando;
- los errores de entrada son reproducibles y descriptivos;
- el build pasa con warnings como errores.

## Fase 2: crear un contexto de servicios estrecho

Antes de extraer clases, definir las dependencias comunes.

### Paso 2.1: identificar dependencias reales

Para cada método de [`EditorGameplay.cpp`](../Editor/src/Gameplay/EditorGameplay.cpp), anotar qué miembros usa. No incluir dependencias sólo “por comodidad”.

Separar como mínimo:

```cpp
struct GameplayServices
{
    EntityWorld& Scene();
    GameplayWorld& Gameplay();
    ActorWorld& Actors();
    WorldInstance& World();
    ItemLibrary& Items();
    RecipeBook& Recipes();
    std::map<std::string, ResourceNodeDef>& NodeDefs();
};
```

Los nombres pueden seguir el estilo existente. Las referencias deben ser no propietarias: `EditorApp` sigue siendo el dueño de los objetos.

### Paso 2.2: no mover todavía la UI

Primero usar el contexto sólo para lógica de dominio. Los paneles se extraerán después, cuando existan interfaces de consulta más estables.

## Fase 3: extraer `EditorGameplayRuntime`

Esta clase debe encargarse de sincronización y simulación, no de comandos ni UI.

### Paso 3.1: mover la sincronización

Extraer gradualmente:

- `SyncGameplay`;
- `BindResources`;
- `StepResources`;
- la coordinación de actualizaciones de `m_Garden`, `m_Ecology`, `m_Resources` y `m_Interactives`.

Separar conceptual y físicamente:

```text
LoadGameplayFromScene()
BindGameplayServices()
SyncGameplayVisuals()
PrepareActorAbilities()
```

No mantener un método genérico que haga todas las operaciones si las fases tienen efectos distintos.

### Paso 3.2: introducir `SimulationTick`

Crear un contexto explícito:

```cpp
struct SimulationTick
{
    uint64_t Index = 0;
    float DeltaSeconds = 1.0f / 60.0f;
    double ClockSeconds = 0.0;
};
```

Usarlo en `StepWorld` y `StepResources`.

Durante esta fase, mantener `DeltaSeconds == 1/60`. El objetivo inicial es hacer visible la dependencia, no cambiar la frecuencia.

### Paso 3.3: separar tiempo y visuales

Separar:

```text
UpdateSeasonSimulation(tick)
ApplySeasonVisuals(clock)
ApplyRain(tick)
```

La actualización de plantas, animales y recursos debe poder ejecutarse en una prueba headless sin ImGui.

### Criterio de terminado

- el runtime puede avanzar un tick sin crear una ventana;
- la UI no es necesaria para simular;
- los replays no cambian;
- la carga y sincronización de escenas conservan el estado actual.

## Fase 4: extraer `EditorTribeController`

Esta es la extracción de mayor valor porque `TribeCommand` concentra demasiados efectos.

### Paso 4.1: mover `ActorBody`

Mover `ActorBody` al nuevo controlador o a un archivo privado del controlador. Mantener sus garantías actuales:

- buscar el actor por nombre en cada operación;
- no conservar punteros a actores que puedan destruirse;
- tratar un actor eliminado como ocupado;
- devolver errores explícitos si el actor ya no existe.

### Paso 4.2: separar construcción de tribu

Dividir `tribe build` en pasos internos:

```text
ParseTribeBuildRequest
ValidateTribeBuildRequest
PrepareTribeBuild
CreateResourcePatch
CreateCampInteractives
CreateActors
BindTribeMembers
CommitTribeBuild
```

El método público sólo debe coordinar esos pasos.

### Paso 4.3: evitar estados parciales

Hay dos opciones aceptables:

#### Opción A: preparación y commit

Construir primero una descripción de lo que se va a crear:

```cpp
struct TribeBuildPlan
{
    std::string Model;
    glm::vec2 Camp;
    int Workers = 4;
    std::vector<std::string> Names;
};
```

Validar el plan antes de modificar la escena. Aplicar los cambios sólo después de que todas las validaciones hayan pasado.

#### Opción B: rollback

Guardar los elementos creados y eliminarlos si una operación posterior falla. Esta opción es más difícil porque hay que restaurar también:

- actores eliminados;
- entidades de escena;
- recursos;
- interactivos;
- bindings;
- estado de la tribu.

Preferir la opción A siempre que sea posible.

### Paso 4.4: mover persistencia y comandos de IA

Mover:

- `RestoreTribe`;
- `AiCommand`;
- parte de `TribeCommand`;
- preparación del estado de abilities al restaurar.

No mover todavía `DrawNpcInspector`, porque pertenece a la presentación.

### Paso 4.5: introducir consultas de tribu

La UI no debería recalcular todo directamente. Añadir consultas como:

```cpp
struct TribeMemberSummary
{
    std::string Name;
    std::string Role;
    std::string Job;
    std::string Detail;
    bool Dead = false;
    bool Abstract = false;
};

struct TribeSummary
{
    double Time = 0.0;
    int Resting = 0;
    int Dead = 0;
    int Abstract = 0;
    std::vector<TribeMemberSummary> Members;
};
```

La implementación puede seguir leyendo `Tribe` internamente, pero la UI sólo recibe el summary.

### Criterio de terminado

- construir una tribu fallida no deja actores ni entidades a medio crear;
- `EditorApp` no contiene la implementación de `ActorBody`;
- la restauración de tribus sigue funcionando;
- `ai status`, `ai memory` y `ai scream` conservan su comportamiento.

## Fase 5: separar handlers de comandos

Crear handlers por dominio:

```text
NavigationCommands
TribeCommands
EcologyCommands
ResourceCommands
InteractiveCommands
GardenCommands
AbilityCommands
```

### Paso 5.1: mantener `EditorApp::RegisterCommands`

El registro puede permanecer en `EditorApp`:

```cpp
m_Commands.Add("ability", [this](const auto& args)
{
    return m_AbilityCommands.Execute(args);
});
```

El handler no debe necesitar llamar a `EditorApp::Execute`.

### Paso 5.2: extraer operaciones internas tipadas

Por ejemplo, sustituir el flujo:

```cpp
Execute("actor spawn Ana ...");
```

por:

```cpp
ActorSpawnRequest request;
request.Name = name;
request.Model = model;
request.Position = position;
request.EnableAi = true;
m_Actors.Spawn(request);
```

Si `ActorWorld` aún no tiene una API apropiada, añadir una API pequeña y específica antes de extraer el handler.

### Paso 5.3: separar validación de ejecución

Cada comando debe seguir:

```text
parsear
validar
ejecutar
formatear resultado
```

Evitar mezclar las cuatro operaciones en un único bloque con muchos `if`.

## Fase 6: centralizar la selección

Crear un `SelectionContext` para mantener consistentes:

- actor seleccionado;
- entidad seleccionada en `SceneEditor`;
- foco de cámara;
- entidad mostrada en el panel Abilities;
- miembro mostrado en el panel Tribe.

Interfaz sugerida:

```cpp
class SelectionContext
{
public:
    void SelectActor(const std::string& name, bool focus);
    void Clear();
    const std::string& ActorName() const;
};
```

Este objeto debe depender de interfaces de escena/cámara, no de toda la aplicación.

Al seleccionar:

1. comprobar si el actor existe;
2. actualizar el actor activo;
3. actualizar la entidad de escena si existe;
4. mover la cámara sólo si se pidió foco;
5. limpiar o corregir la selección si el actor desaparece.

## Fase 7: extraer paneles y depuración visual

Extraer después de estabilizar las consultas.

### Paneles

Mover:

- `DrawWorldStatePanel`;
- `DrawTribePanel`;
- `DrawAbilitiesPanel`;
- `DrawBag`;
- `DrawNpcInspector`.

Cada panel debería recibir:

```cpp
const WorldStateSnapshot&
```

o una interfaz de consultas, y un pequeño dispatcher de acciones.

No debe acceder directamente a:

- `m_Gameplay.Names()`;
- tags de dominio;
- atributos por string;
- `m_Props`;
- `m_SceneEditor`;

salvo mediante una interfaz definida para ese propósito.

### Depuración visual

Mover:

- `DrawNavDebug`;
- `DrawAiDebug`.

Estas funciones pueden depender de cámara, terreno y datos de debug, pero no deberían conocer cómo se construyen las tribus o cómo se ejecutan los comandos.

## Fase 8: actualizar CMake y dependencias

Añadir los nuevos `.cpp` a [`Editor/CMakeLists.txt`](../Editor/CMakeLists.txt):

```cmake
add_library(GBEditor STATIC
    ...
    src/Gameplay/EditorGameplay.cpp
    src/Gameplay/EditorGameplayParsing.cpp
    src/Gameplay/EditorGameplayRuntime.cpp
    src/Gameplay/EditorTribeController.cpp
    src/Gameplay/EditorGameplayPanels.cpp
    src/Gameplay/EditorGameplayDebug.cpp)
```

Mantener los nuevos headers dentro de `Editor/src` y respetar los includes del proyecto.

No añadir dependencias externas para parseo, UI o utilidades básicas.

## Estrategia de pruebas

## Pruebas que deben pasar después de cada fase

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Para una iteración rápida, ejecutar primero el target de tests del editor y después el conjunto completo.

## Casos funcionales mínimos

### Comandos

- `nav status`
- `nav path` con números válidos
- `nav path` con texto inválido
- `resources build`
- `resources status`
- `interactives build`
- `garden build`
- `ability list`
- `ability activate`
- `ecology spawn`
- `ecology season`
- `tribe build`
- `tribe status`
- `tribe log`
- `tribe clear`
- `ai status`
- `ai memory`
- `ai scream`

### Persistencia

- guardar después de construir recursos;
- guardar con tribu activa;
- cargar una tribu;
- cargar un actor que ya no existe;
- guardar durante una ability;
- comprobar que no quedan tags de busy inválidos.

### Fallos

- modelo de actor inválido;
- actor duplicado;
- workers fuera de rango;
- coordenadas inválidas;
- entidad objetivo inexistente;
- construcción que falla a mitad de camino;
- escena sin terreno;
- escena sin navegación.

## Reglas para evitar regresiones

1. Cada commit debe compilar.
2. Cada extracción debe conservar el diff funcional mínimo.
3. No cambiar nombres públicos y refactorizar a la vez.
4. No modificar serialización durante la extracción.
5. No introducir `catch (...)`.
6. No convertir errores en valores por defecto.
7. No usar `as any` o equivalentes.
8. No mezclar optimizaciones de rendimiento con la primera extracción.
9. No incluir binarios, carpetas de build, saves locales ni escenas generadas.
10. Si un método nuevo necesita demasiadas dependencias, detenerse y rediseñar la frontera.

## Orden recomendado de commits

Una secuencia limpia sería:

1. `Extract gameplay argument parsing`
2. `Validate gameplay command arguments`
3. `Introduce gameplay service context`
4. `Extract gameplay runtime`
5. `Extract tribe controller`
6. `Remove internal command execution`
7. `Centralize editor gameplay selection`
8. `Extract gameplay panels`
9. `Extract gameplay debug rendering`
10. `Add focused gameplay tests`

Cada commit debería ser reversible y no contener cambios no relacionados.

## Checklist final

### Diseño

- [ ] `EditorGameplay.cpp` sólo coordina o expone una fachada pequeña.
- [ ] La simulación no depende de ImGui.
- [ ] La UI no conoce las reglas internas de cada sistema.
- [ ] Los comandos no se usan como API interna.
- [ ] Las clases nuevas reciben dependencias estrechas.
- [ ] La selección tiene una única fuente de verdad.

### Robustez

- [ ] No quedan conversiones `atoi`/`atof` en los comandos refactorizados.
- [ ] Los argumentos inválidos producen errores explícitos.
- [ ] `tribe build` no deja estado parcial.
- [ ] Las reconstrucciones de gameplay tienen fases explícitas.
- [ ] Los actores eliminados no dejan punteros colgantes.

### Validación

- [ ] Compila con `GB_WARNINGS_AS_ERRORS=ON`.
- [ ] Pasan los tests del editor.
- [ ] Pasan los replays existentes.
- [ ] Se han probado los comandos de éxito y error.
- [ ] Se han probado save/load y restauración de tribus.
- [ ] No se han añadido archivos generados al commit.

## Primer paso recomendado

No empezar moviendo los paneles. El primer cambio debería ser:

1. crear `EditorGameplayParsing.h/.cpp`;
2. añadir tests para `ParseFloat`, `ParseInteger` y límites;
3. migrar sólo `NavCommand`;
4. compilar con warnings como errores;
5. ejecutar tests;
6. migrar el resto de comandos uno por uno.

Es una mejora pequeña, observable y reversible. Después de esa fase, extraer `EditorTribeController`, porque es la parte con mayor riesgo de estado parcial y mayor concentración de responsabilidades.
