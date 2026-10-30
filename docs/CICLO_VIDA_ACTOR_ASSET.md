# Ciclo de vida de actores y assets

Este documento describe cómo se crea, inicializa, actualiza, guarda, restaura y destruye un actor en este proyecto. También separa el ciclo de vida del asset que utiliza el actor y compara ambos flujos con el ciclo de vida de `AActor` de Unreal Engine.

La comparación con Unreal es conceptual. Este motor no tiene `UObject`, reflexión, `World`, `BeginPlay` ni garbage collector. Sus objetos se destruyen mediante ownership explícito de C++ (`std::unique_ptr` y `std::shared_ptr`).

## Hallazgo: el actor actual está acoplado a la geometría

El diseño actual mezcla dos conceptos que deberían poder existir por separado:

1. **Actor lógico:** identidad, estado, reglas, órdenes, abilities, inventario, IA, tareas, persistencia y simulación.
2. **Representación:** `Model`, `ModelInstance`, skeleton, animación, IK, props y envío a `Scene`.

Hoy `Actor` incluye directamente ambos grupos. `Actor::Init` recibe un `std::shared_ptr<Model>`, calcula la escala a partir de sus bounds y crea `ModelInstance`. Además, `ActorWorld::Spawn` carga o exige un modelo antes de insertar el actor. En consecuencia, un actor que sólo tenga lógica no puede seguir el mismo flujo de spawn.

Esto impide casos válidos como:

- una tribu abstracta que sólo existe para simular población;
- un trigger, marcador o volumen de gameplay sin representación;
- un actor de servidor o replay sin render;
- un agente lejano que se abstrae sin FBX;
- una entidad que todavía está esperando que se cargue su asset visual;
- una regla o componente de inventario que no necesita geometría.

El repositorio sí tiene subsistemas reutilizables (`AbilitySystemComponent`, `TaskList`, `CharacterController`, `AIDriver`, `Inventory`, `CharacterAnimator`), pero no existe todavía una abstracción común de `Component` que los registre, ordene y notifique de las fases de vida. Por tanto, la composición actual es por campos fijos dentro de `Actor`, no por componentes intercambiables.

### Objetivo arquitectónico: `GBObject` como instancia mínima

La regla propuesta es:

> Un objeto debe poder existir en el mundo con identidad y transform aunque no tenga geometría. Un actor es una especialización de ese objeto que añade comportamiento de gameplay; la geometría es un componente opcional.

La raíz del modelo debe ser `GBObject`. No debe representar necesariamente un personaje ni una entidad renderizable:

```text
GBObject
├── Name / stable handle
├── Transform
│   ├── Position
│   ├── Rotation
│   └── Scale
├── Lifecycle state
└── reusable logical components
    ├── Tags / metadata
    ├── Persistence
    ├── Trigger / interaction
    └── custom gameplay logic

Actor : GBObject
├── movement
├── input / AI / tasks / replay
├── abilities
├── inventory
├── optional physics
└── optional presentation
    ├── Geometry
    ├── Animation
    ├── Audio
    └── Effects
```

`GBObject` es la instancia mínima que se registra en el mundo: nombre, transform y ciclo de vida. Puede representar un punto fijo, un marcador, un nodo interactivo, un trigger o un objeto abstracto. No debe incluir `Model`, `ModelInstance`, `CharacterController`, cámara, teclado ni ImGui.

`Actor` hereda de `GBObject` y añade la lógica necesaria para una entidad que puede actuar: movimiento, input de jugador, IA, tasks, abilities, inventario o replay. Un `Actor` puede seguir siendo completamente lógico si no tiene `GeometryComponent`.

La dependencia deseada es:

```text
GBObject
├── Transform
├── lifecycle
└── GBObjectComponent[]

Actor : GBObject
├── componentes de gameplay (GBObjectComponent)
│   ├── AbilityComponent
│   ├── InventoryComponent
│   ├── TaskComponent
│   ├── AIComponent
│   ├── MovementComponent
│   └── PhysicsComponent
└── Presentation components opcionales
    ├── GeometryComponent
    ├── AnimationComponent
    ├── AudioComponent
    └── EffectsComponent
```

La capa `GBObject` no debe incluir `Model.h`, `ModelInstance.h`, `Scene.h` ni tipos de renderer. La capa de presentación puede consultar el transform y el estado lógico, pero el objeto o actor no debe dejar de existir porque el asset visual falle o todavía no esté disponible.

### Diferencia entre `GBObject`, `Actor` y asset

| Elemento | Puede existir sin geometría | Responsabilidad |
|---|---:|---|
| `GBObject` | Sí, siempre | identidad, transform, componentes y ciclo de vida |
| `Actor` | Sí | comportamiento de gameplay, movimiento, órdenes e interacción |
| `GeometryComponent` | No | cargar y representar un asset visual |
| `Model` | Sí, como asset cargado | datos compartidos de malla, materiales, skeleton y clips |
| `ModelInstance` | No | estado visual de una instancia concreta |

## Fuentes del proyecto

- Actor individual: `Engine/src/GB/Game/Actor.h` y `Actor.cpp`.
- Contenedor y propietario de actores: `Engine/src/GB/Game/ActorWorld.h` y `ActorWorld.cpp`.
- Carga de modelos: `Engine/src/GB/Assets/Model.cpp`.
- Integración con escena y gameplay: `Editor/src/Gameplay/EditorGameplay.cpp`.
- Persistencia: `Engine/src/GB/Save/SaveWorld.h`, `ActorWorld::CaptureActors` y `ActorWorld::RestoreActors`.
- Animación: `Engine/src/GB/Animation/CharacterAnimator.cpp`, `AnimInstance.cpp` y `AnimBindings.cpp`.

## Resumen del ownership

```text
EditorApp / Game / Test
└── ActorWorld
    ├── m_Objects: map<string, unique_ptr<GBObject>>
    │   ├── GBObject
    │   │   └── transform + GBObjectComponent[]
    │   └── Actor : GBObject
    │       ├── Gameplay: movement, abilities, tasks, AI, replay, inventory
    │       ├── Physics opcional
    │       └── Presentation opcional
    │           ├── shared_ptr<Model>       # asset compartido
    │           ├── unique_ptr<ModelInstance> # instancia visual propia
    │           └── CharacterAnimator / AnimInstance
    ├── m_Models: cache de shared_ptr<Model>
    ├── NavGrid
    └── referencias no propietarias:
        ├── EntityWorld / Scene
        ├── GameplayWorld
        └── ActorHost
```

`ActorWorld` debe evolucionar a propietario de `GBObject`, o delegar esa responsabilidad en un `ObjectWorld`. Los actores serían un tipo registrado dentro de ese contenedor común. El objeto no es copiable ni movible porque otros sistemas pueden conservar su dirección mientras vive. La identidad es un handle estable asignado al publicar; el nombre es único dentro del mundo y es la clave de serialización y de enlace con la escena y `GameplayWorld`.

El `Model` es un asset compartido: varios actores pueden utilizar el mismo modelo. Cada actor crea su propio `ModelInstance`, porque la pose, los overlays, los montajes, los props y el estado visual no deben compartirse entre personajes.

En el estado actual, el árbol anterior todavía está materializado como una única clase `Actor`, no como componentes independientes. El árbol representa la frontera objetivo del refactor.

## Ciclo completo de un objeto y un actor

```text
Descripción de spawn de GBObject
        │
        ▼
ObjectWorld::Spawn
        │
        ├── valida nombre y duplicados
        ├── crea GBObject con transform
        ├── ejecuta OnSpawnStart
        ├── crea componentes declarados
        ├── si es Actor, añade gameplay y componentes opcionales
        ├── si existe GeometryDefinition, solicita el asset visual
        ├── ejecuta OnSpawnEnd
        ├── publica el objeto (lo hace el mundo, no el objeto)
        └── ejecuta BeginPlay
        │
        ▼
Objeto/Actor vivo en ObjectWorld
        │
        ├── componentes lógicos reciben Update
        ├── Actor recibe input / AI / tasks / script / replay
        ├── componentes de presentación actualizan representación
        ├── SyncTransform / ScenePresentation
        └── Capture / Restore
        │
        ▼
ObjectWorld::Destroy o Clear
        │
        ├── EndPlay
        ├── despublica el objeto
        ├── OnDestroy en componentes
        └── unique_ptr destruye GBObject/Actor
```

## Ciclo de vida propuesto

Las siguientes fases deben ser explícitas y aplicarse tanto al actor como a sus componentes. No todas las fases pertenecen al asset geométrico: `OnLoad` pertenece al recurso, mientras que `OnSpawnStart` y el resto pertenecen a la instancia runtime.

```text
AssetDefinition (opcional)
    │
    └── OnLoad
          │
          ▼
GBObject creado con Name + Transform
    │
    ├── OnSpawnStart
    │     ├── crea GBObject y componentes declarados
    │     ├── inyecta contexto y dependencias
    │     └── prepara estado inicial
    │
    ├── OnSpawnEnd
    │     └── registra referencias entre componentes
    │
    ├── (el mundo publica el objeto: handle, índices, evento ObjectSpawned)
    │
    ├── BeginPlay
    │     └── activa el objeto y, si es Actor, su gameplay
    │
    ├── Update / Tick
    │
    ├── OnPause
    │     └── congela o degrada el trabajo del actor
    │
    ├── EndPlay
    │     └── deja de participar en el mundo activo
    │
    └── OnDestroy
          ├── desregistra handles y listeners
          ├── destruye componentes de instancia
          └── libera el asset cuando no quedan referencias
```

El ciclo pertenece primero a `GBObject`; `Actor` lo hereda y puede especializar sus callbacks. Un `GBObject` sin componentes de gameplay atraviesa exactamente las mismas fases que un actor, pero no necesita cargar un modelo ni ejecutar movimiento.

### Estados y transiciones

| Desde | Fase | Hasta | Notas |
|---|---|---|---|
| `Constructing` | `OnSpawnStart`, `OnSpawnEnd`, `BeginPlay` | `Active` | `BeginPlay` sólo tras `OnSpawnEnd`; cada fase de spawn se ejecuta una vez |
| `Active` | `OnPause` | `Paused` | una sola vez; sin `Update` |
| `Paused` | `OnResume` | `Active` | no repite `OnSpawnStart` |
| `Active`, `Paused` | `EndPlay` | `Ending` | componentes en orden inverso |
| `Ending` (o vivo) | `OnDestroy` | `Destroyed` | idempotente; si sigue vivo ejecuta `EndPlay` antes |

Una fase pedida fuera de su estado se ignora. `Paused` sustituye a `ActorWorld::m_Suspended`. El nivel de detalle (Full/Reduced/Far) **no** es un estado: es una política de presentación que sólo existe si hay `GeometryComponent`.

### `OnLoad`: preparar un asset, no crear un actor

`OnLoad` pertenece al asset o a una definición de actor. Debe ejecutarse una sola vez por recurso cargado y ser independiente de la presencia de un actor en el mundo.

Responsabilidades:

- resolver la ruta o el identificador del recurso;
- cargar datos CPU/GPU;
- validar el formato;
- construir metadatos: bounds, skeleton, clips, sockets y materiales;
- registrar dependencias de otros assets;
- dejar el asset listo para crear instancias.

Para el estado actual, `Model::Load`, `Actor::LoadCharacter` y `ActorWorld::LoadModel` son el equivalente parcial de esta fase. Sin embargo, la carga de FBX y la búsqueda de animaciones están iniciadas desde `Actor`, lo que hace que la representación de un actor conozca detalles del formato del asset.

`OnLoad` no debe:

- insertar un actor en `ActorWorld`;
- crear `ModelInstance`;
- registrar abilities;
- iniciar IA o tasks;
- depender de posición, jugador o `Scene`.

Un asset lógico puede completar `OnLoad` sin tener `Model`. Por ejemplo, una definición de `Tribe`, `Trigger` o `AbstractAgent` puede cargar configuración, reglas y datos de simulación, y dejar vacío su componente `Geometry`.

### `OnSpawnStart`: construir una instancia incompleta

`OnSpawnStart` comienza cuando `ObjectWorld` acepta una descripción de spawn, pero antes de hacer visible o activa la instancia.

Responsabilidades:

1. reservar identidad y handle estable;
2. crear el `GBObject` y su transform;
3. crear sólo los componentes solicitados por la definición;
4. inyectar `ActorHost`, reloj, `GameplayWorld`, servicios de navegación y persistencia;
5. copiar el estado inicial;
6. conectar dependencias internas sin publicar todavía el actor;
7. solicitar la representación opcional si existe un asset visual;
8. permitir que `Actor` añada sus componentes de movimiento, input, IA o gameplay.

Una geometría ausente no debe ser un error de spawn. El resultado esperado puede ser:

```text
ActorSpawnResult
├── Actor lógico: válido
├── Gameplay: activo o pendiente según dependencias
└── Geometry: Ready | Loading | Missing | Failed
```

El actor no debe recibir `BeginPlay` hasta que el contrato del mundo indique que sus componentes obligatorios están listos. La geometría no debería ser obligatoria salvo para un tipo de actor que explícitamente la requiera.

### `OnSpawnEnd`: cerrar la composición

`OnSpawnEnd` se ejecuta cuando todos los componentes han sido creados y las referencias cruzadas pueden resolverse. En este punto el `GBObject` sigue siendo una instancia en construcción; `OnSpawnEnd` nunca publica.

Responsabilidades:

- crear la entidad de escena sólo si la presentación está disponible y se solicitó;
- enlazar abilities, inventory, tasks e IA;
- terminar la inicialización de componentes que necesitan verse entre sí;
- reportar errores de componentes opcionales sin invalidar el actor lógico.

Tras `OnSpawnEnd`, `ObjectWorld` publica el objeto (asigna handle, lo registra en sus índices y emite `ObjectSpawned`). La publicación debe suceder una sola vez. Un componente no debe observar un actor parcialmente insertado, y `OnSpawnEnd` debe ser el límite entre construcción y vida pública.

### `BeginPlay`: activar gameplay

`BeginPlay` significa que el `GBObject` ya pertenece al mundo activo y puede recibir tiempo y eventos. En un `Actor`, además habilita órdenes, abilities, IA, tasks o input.

Orden recomendado:

1. `ObjectWorld` marca el objeto como `Active`;
2. componentes lógicos reciben `BeginPlay`;
3. se preparan abilities y bindings;
4. se activa IA/tasks/replay según la definición;
5. el componente de geometría crea o conecta `ModelInstance` si el asset está listo;
6. se emite `BeginPlay` a listeners externos.

La implementación actual reparte este trabajo entre `Actor::Init`, `ActorWorld::Spawn`, `SetAbilities` y `PrepareAbilities`. La extracción de `GBObject::BeginPlay` y de callbacks de componentes evitaría que cada caller tenga que conocer ese orden.

### `Update` / `Tick`

Sólo los actores en estado `Active` deben recibir actualización normal. El world debe proporcionar un orden determinista y un contexto común:

```text
ActorWorld::Update(dt)
  -> componentes de simulación
  -> abilities / tasks / AI
  -> movimiento y física
  -> sincronización de estado
  -> componentes de presentación
```

La geometría puede actualizar pose, IK y props a una frecuencia menor mediante LOD, pero eso no debe cambiar la frecuencia de la lógica salvo que el actor se encuentre explícitamente suspendido.

### `OnPause`

`OnPause` no es destrucción ni `EndPlay`. El actor sigue existiendo y conserva su estado serializable, pero deja de consumir el trabajo que el mundo haya decidido pausar.

Casos:

- pausa global del juego;
- actor fuera del área simulada;
- replay detenido;
- actor abstraído por una simulación de tribu;
- suspensión por presupuesto.

El contrato recomendado es:

- emitir `OnPause` una sola vez al pasar de `Active` a `Paused`;
- detener `Tick` de componentes que no deban avanzar;
- conservar identidad, inventario, órdenes y estado;
- permitir que un componente de streaming termine una carga en segundo plano si el sistema lo admite;
- usar un evento complementario `OnResume` para reactivar sin repetir `OnSpawnStart`.

En el código actual, `ActorWorld::SetSuspended` y `m_Suspended` son una forma parcial de pausa para actores. No notifican a componentes, no tienen transición explícita y mezclan suspensión de gameplay con LOD de animación. Deben separarse:

```text
Paused: no simulación lógica
Reduced: simulación lógica, menos presentación
Far: simulación lógica, pose espaciada
```

### `EndPlay`

`EndPlay` marca el final de la participación del `GBObject` en un mundo activo, pero no necesariamente la liberación inmediata del objeto.

Responsabilidades:

- detener tasks, replay y AI;
- cancelar abilities activas;
- dejar de aceptar input;
- quitar el actor de navegación y colisiones dinámicas;
- desregistrar listeners temporales;
- retirar la entidad de escena o marcarla no visible;
- guardar o publicar el motivo de salida;
- notificar a componentes en orden inverso a su dependencia.

`EndPlay` debe ejecutarse antes de `OnDestroy` y también en un unload de nivel o cambio de mundo. En la implementación actual, parte de este trabajo ocurre implícitamente en `Remove`, `RestoreActors` y `Actor::~Actor`, pero no hay una fase común de `GBObject` que todos los componentes puedan observar.

### `OnDestroy`

`OnDestroy` es la fase final de la instancia. Debe ser idempotente y ejecutarse después de `EndPlay`.

Responsabilidades:

- invalidar el handle del actor;
- desconectar referencias no propietarias;
- quitar listeners y timers;
- destruir componentes en orden inverso de creación;
- liberar `ModelInstance`, animator, física y runtime de abilities;
- permitir que el asset compartido se libere sólo si no quedan referencias.

`Actor::~Actor` ya cancela abilities y libera animator/física, pero el destructor no debe ser el único mecanismo de ciclo de vida: un destructor no puede emitir eventos seguros a sistemas que quizá ya estén destruidos. La transición explícita debe ocurrir en `ObjectWorld::Destroy` antes de borrar el `unique_ptr`.

## Estado de la implementación

Ya existe en `Engine/src/GB/Game/`. Los componentes se organizan por tipo en `Components/`:

```text
Components/
├── ActorStateSaver.h        # IStateSaver: lo comparten todos los tipos
├── Gameplay/                # lógica; no necesita modelo
│   ├── MovementComponent
│   ├── OrdersComponent
│   ├── AbilityComponent
│   └── InventoryComponent
├── Presentation/            # cómo se ve; sólo con modelo
│   ├── GeometryComponent
│   ├── AnimationComponent
│   └── PropsComponent
└── Physics/                 # simulación del cuerpo caído
    └── RagdollComponent
```

Un componente nuevo va en la carpeta de su tipo; `GBObject` y `Actor` quedan en `Game/` porque no son componentes.

| Pieza | Fichero | Qué hace |
|---|---|---|
| `GBObject`, `GBObjectComponent` | `GBObject.{h,cpp}` | nombre, `Kind`, transform, estados y fases; reenvía las fases a los componentes (orden de alta, inverso al terminar). Test: `gbobject_test.cpp` |
| `MovementComponent` | `Components/Gameplay/MovementComponent.{h,cpp}` | `CharacterController`, `CharacterWorld`, obstáculos, tamaño y `MotionState`; no necesita modelo |
| `OrdersComponent` | `Components/Gameplay/OrdersComponent.{h,cpp}` | fuente de órdenes (Direct, AI, Tasks, Script, Replay) y grabación; elige el input del frame |
| `AbilityComponent` | `Components/Gameplay/AbilityComponent.{h,cpp}` | enlace con el sistema de abilities de `GameplayWorld` y el runtime que se le presta |
| `InventoryComponent` | `Components/Gameplay/InventoryComponent.h` | la bolsa; se guarda con el actor |
| `GeometryComponent` | `Components/Presentation/GeometryComponent.{h,cpp}` | `Model` compartido, `ModelInstance` propia, escala y matriz de raíz |
| `RagdollComponent` | `Components/Physics/RagdollComponent.{h,cpp}` | mundo de física del ragdoll y sus pasos |
| `AnimationComponent` | `Components/Presentation/AnimationComponent.{h,cpp}` | animador de locomoción o conmutador simple, manifest, bindings, montajes, IK, retarget |
| `PropsComponent` | `Components/Presentation/PropsComponent.{h,cpp}` | props en manos y espalda, looks y su dibujo |
| `IStateSaver` | `Components/ActorStateSaver.h` | cada componente guarda y restaura su parte de `Save::ActorState` (hace de `PersistenceComponent`) |

`Actor : GBObject` crea los cuatro primeros siempre y, si recibe un modelo, los cuatro siguientes. El orden de alta importa al destruir: `Geometry`, `Ragdoll`, `Animation`, `Props`; al terminar van al revés, así el animador (que contiene el ragdoll) desaparece antes que su mundo de física y el modelo es lo último. Los componentes del modelo se construyen aparte y se unen al objeto con `Adopt` sólo si el animador se construye bien.

`Actor` conserva la API pública anterior como delegación y ordena el frame (órdenes → abilities → movimiento → animación → props). Los componentes no se llaman entre sí salvo la dependencia declarada en su constructor (`Animation` y `Ragdoll` leen el cuerpo; `Props` lee la animación).

**Todavía no está hecho:**

- `Update` del frame lo dirige `Actor`; los `Update` de los componentes no se usan.
- Un modelo que no carga hace fallar el spawn; el objetivo es `Geometry: Failed` con el actor vivo.
- `ActorWorld` sigue siendo el contenedor (no `ObjectWorld`) y las claves siguen siendo nombres, sin handle estable.
- `ScenePresentationComponent`, `AudioComponent`, `EffectsComponent`, `TaskComponent` y `AIComponent` separados no existen: tareas e IA viven dentro de `OrdersComponent`.
- `Actor.h` sigue incluyendo los componentes del modelo; el aislamiento de includes para un actor puramente lógico es un paso posterior.

## Composición por componentes

La separación propuesta no requiere convertir cada método actual en una clase inmediatamente. El primer paso es definir un contrato común para componentes de `GBObject` y un objeto base con sólo identidad, transform y ciclo de vida:

```cpp
// Un único contrato de fases: lo implementan los componentes. Todas son opcionales.
class GBObjectComponent
{
public:
    virtual ~GBObjectComponent() = default;
    virtual void OnSpawnStart(GBObject&, GBObjectContext&) {}
    virtual void OnSpawnEnd(GBObject&) {}
    virtual void BeginPlay(GBObject&) {}
    virtual void Update(GBObject&, float dt) {}
    virtual void OnPause(GBObject&) {}
    virtual void OnResume(GBObject&) {}
    virtual void EndPlay(GBObject&) {}
    virtual void OnDestroy(GBObject&) {}
};

// No es abstracto: sus fases validan la transición de estado y reenvían a los
// componentes (en orden de creación, o inverso al terminar). Un GBObject sin
// componentes es un objeto válido (marcador, punto fijo). Implementado en
// Engine/src/GB/Game/GBObject.{h,cpp}.
class GBObject
{
public:
    virtual void OnSpawnStart(GBObjectContext&);
    virtual void OnSpawnEnd();
    virtual void BeginPlay();
    virtual void Update(float dt);
    virtual void OnPause();
    virtual void OnResume();
    virtual void EndPlay();
    virtual void OnDestroy();

    ObjectTransform Transform;
    const std::string& Name() const;
    ObjectState State() const;
    // componentes propios, no copiable ni movible
};

class Actor : public GBObject
{
    // Sólo añade componentes de gameplay (y especializa fases si hace falta);
    // no repite el ciclo ni hace obligatoria la geometría.
};
```

Componentes iniciales recomendados:

| Componente | Responsabilidad | Requiere geometría |
|---|---|---:|
| `TagsComponent` | tags y metadatos; nombre, handle, `Kind` y transform son campos de `GBObject`, no componentes | No |
| `MovementComponent` | `CharacterController`, navegación y movimiento | No |
| `AbilityComponent` | abilities, montajes lógicos y runtime | No |
| `TaskComponent` | tareas e interacciones | No |
| `AIComponent` | decisiones y órdenes AI | No |
| `InventoryComponent` | bolsa y objetos | No |
| `PersistenceComponent` | captura/restauración de estado | No |
| `GeometryComponent` | `Model`, `ModelInstance`, bounds y carga visual | Sí |
| `AnimationComponent` | animator, clips, IK, props y ragdoll visual | Sí o parcialmente |
| `ScenePresentationComponent` | convertir estado a `Scene` | Sí |

`PhysicsComponent` requiere una decisión posterior: la física de gameplay puede existir sin malla, mientras que el ragdoll sí requiere skeleton y representación. No debe mezclarse automáticamente toda física con `GeometryComponent`.

Un actor lógico mínimo podría componerse así:

```text
AbstractTribeMember
├── TagsComponent
├── MovementComponent
├── AbilityComponent
├── InventoryComponent
└── PersistenceComponent
```

Un personaje visible añadiría:

```text
Character
├── componentes lógicos anteriores
├── GeometryComponent
├── AnimationComponent
└── ScenePresentationComponent
```

El `ObjectWorld` debería crear `GBObject` o `Actor` y sus componentes desde una `ObjectDefinition`/`ActorDefinition`, no desde la extensión del archivo. La presencia de `.fbx` sólo debe seleccionar o alimentar `GeometryComponent`.

## Orden de callbacks entre componentes

Para evitar referencias a medio construir, el world debe imponer dos órdenes:

1. **Creación:** identidad, transform, gameplay, persistencia, presentación.
2. **Destrucción:** presentación, persistencia, gameplay, transform, identidad.

El flujo sería:

```text
Create:
  construct components
  OnSpawnStart (orden de dependencias)
  OnSpawnEnd (orden de dependencias)
  publish actor
  BeginPlay

Destroy:
  request destroy
  EndPlay (orden inverso)
  unpublish actor
  OnDestroy (orden inverso)
  release components
```

Las callbacks no deben invocarse desde un constructor C++ ni desde un destructor. El constructor no tiene todavía contexto suficiente y el destructor no garantiza que los servicios externos sigan vivos.

## Fases detalladas

> **Estado actual frente a objetivo.** Esta sección describe el código de hoy, donde `ActorWorld` posee `Actor`. En el objetivo, `ObjectWorld` posee `GBObject` y `ActorWorld` queda, como mucho, como fachada tipada (`FindActor`). Donde aparece `ActorWorld` se lee `ObjectWorld`; los mapas por nombre `m_Kinds`, `m_Suspended` y `m_Levels` pasan a campos del objeto (`Kind`, estado `Paused`) y de `GeometryComponent` (nivel de detalle).

### 1. Preparación del asset

El caller proporciona un `ActorSpawn`:

- `Path`: ruta del FBX o glTF;
- `Shared`: modelo ya cargado, principalmente útil en tests;
- `Kind`: identificador guardado para poder reconstruir el actor;
- `Position`, `Yaw` y `Height`;
- `Rigged`;
- `CreateEntity`.

`ActorWorld::LoadModel` primero resuelve la ruta directa y después la relativa a `m_AssetRoot`. El resultado se guarda en `m_Models`. Si el mismo asset se solicita otra vez, se reutiliza el `shared_ptr<Model>`.

Para un FBX, `Actor::LoadCharacter`:

1. resuelve la ruta;
2. carga el FBX principal mediante `Model::Load`;
3. busca otros `.fbx` en la misma carpeta;
4. ordena los archivos;
5. añade sus animaciones al modelo.

Para glTF, `Model::Load` parsea el archivo, carga buffers, valida el documento y convierte materiales, nodos, skins, cámaras, animaciones y mallas.

**Importante:** cargar un asset no crea todavía un actor. El modelo es un recurso reutilizable; el actor aparece después, en `ActorWorld::Spawn`.

### 2. Validación y creación del actor

`ActorWorld::Spawn` rechaza:

- nombre vacío;
- nombre duplicado;
- fallo de la parte lógica de `Actor::Init`.

**Estado actual** (a eliminar): también rechaza la ausencia de modelo, un error de carga y el fallo del animator. **Objetivo:** esos tres casos dejan el objeto vivo con `Geometry: Missing | Failed` y sólo `AnimationComponent` informa del error.

Después crea un `ActorSpec` con manifest, bindings, skeleton, posición, orientación y obstáculos de otros actores.

`Actor::Init`:

1. valida que el modelo exista;
2. asigna nombre y altura;
3. calcula la escala del modelo;
4. copia `ActorHost`;
5. copia manifest y bindings;
6. configura radio y profundidad de vadeo;
7. busca una posición libre y seca;
8. crea `ModelInstance`;
9. aplica el manifest efectivo;
10. crea `CharacterAnimator` si el actor es rigged;
11. comprueba que existan clips de locomoción;
12. registra montajes de abilities;
13. aplica pies, pasos y demás configuración.

Si la construcción del animator falla, se liberan `ModelInstance` y `Model`, y `Spawn` devuelve el error sin insertar el actor en `ActorWorld`.

### 3. Enlace con escena y gameplay

Después de `Init`, `ActorWorld::Spawn` puede crear una entidad de escena si `CreateEntity` está activo y no existe una entidad con ese nombre. También crea el `AbilitySystemComponent` correspondiente en `GameplayWorld`.

El actor mantiene una referencia lógica al sistema de abilities identificado por nombre. La escena y `GameplayWorld` siguen siendo sistemas separados:

```text
Actor.name
   ├── Scene entity con el mismo nombre
   └── AbilitySystemComponent con el mismo nombre
```

En el editor, `SyncGameplay` reconstruye sistemas derivados desde la escena y después llama a `m_Actors.PrepareAbilities()`. Esto permite que un actor recién creado o cargado pueda activar una ability en el mismo ciclo.

### 4. Preparación antes del frame

`ActorWorld::PrepareAbilities` se ejecuta antes de actualizar actores:

1. detecta si la librería de abilities cambió;
2. vuelve a sincronizar los montajes propios de cada actor;
3. prepara abilities de actores no suspendidos.

Un actor suspendido no recibe runtime de abilities en ese frame. La suspensión es parte del nivel de detalle de simulación para miembros abstractos o actores lejanos.

### 5. Actualización por frame

`ObjectWorld::Update(dt)`:

1. inicia un nuevo frame de navegación;
2. toma una instantánea de posiciones y radios al inicio;
3. calcula el nivel de detalle respecto al viewer;
4. asigna obstáculos de otros actores;
5. decide si la pose se actualiza en este frame;
6. llama a `Actor::Update(dt)` en orden de nombre.

El orden por nombre hace que la simulación sea reproducible y que la colisión entre actores no dependa del orden accidental de inserción.

`Actor::Update(dt)` sigue este flujo:

```text
si no hay ModelInstance: salir
si es ragdoll:
    física
    pelvis -> CharacterController
    motion tracker
    animator
si es actor normal:
    elegir input
    si una ability controla al actor, usar su input
    limitar velocidad según overlay activo
    guardar input si se está grabando
    avanzar CharacterController
    actualizar MotionState
    activar ragdoll si el aterrizaje lo requiere
    actualizar animator o animación simple
    adjuntar props pendientes cuando termina la transición
```

Las fuentes de órdenes son `Direct`, `AI`, `Tasks`, `Script` y `Replay`. El actor no conoce teclado, ImGui ni `EditorApp`.

### 6. Nivel de detalle

`ActorWorld` decide el coste visual sin cambiar las reglas de movimiento o abilities:

- **Full:** pose completa, IK, props y look-at;
- **Reduced:** sin IK y pose cada cierto número de frames;
- **Far:** pose espaciada, sin IK ni props;
- **Suspended:** el actor no se anima ni es obstáculo; un miembro abstracto puede moverlo mediante `Teleport`.

La calidad configura radios y frecuencia mediante `AnimLodForQuality`. El nivel de detalle es una decisión de presentación/performance, pero el actor conserva su estado de gameplay.

### 7. Sincronización con la escena

`ActorWorld::SyncEntities` escribe posición y rotación del actor en la entidad de escena que tiene su mismo nombre.

`ActorWorld::AddToScene` añade los actores no suspendidos a la escena de render. Esto no es ownership: la escena recibe datos de representación, pero `ActorWorld` sigue siendo el propietario de `Actor`.

### 8. Guardado y restauración

`CaptureActors` guarda:

- nombre;
- `Kind`;
- posición;
- yaw;
- overlay de animación;
- props;
- ragdoll;
- inventario y edad de los objetos.

No guarda el modelo completo ni todos los datos derivados del animator. El modelo se reconstruye usando `Kind` y los assets disponibles.

`RestoreActors`:

1. considera el save como la verdad sobre qué actores existen;
2. elimina actores no presentes en el save;
3. omite actores guardados que no existen actualmente;
4. detiene tasks;
5. quita suspensión;
6. teletransporta a posición y yaw;
7. restaura inventario;
8. restaura overlay si existe.

La restauración no reanuda automáticamente las tasks anteriores. Esto es deliberado para que una carga o replay empiece desde un estado controlado.

### 9. Destrucción

`ActorWorld::Remove` borra el actor del mapa. Al destruir el `unique_ptr`:

1. el destructor cancela todas las abilities;
2. desconecta el runtime de abilities;
3. libera el animator;
4. libera la física/ragdoll;
5. libera el `ModelInstance`;
6. libera su referencia al `Model`.

El `Model` compartido sólo se libera cuando desaparece el último `shared_ptr`, normalmente al limpiar la caché o destruir `ActorWorld`. Por eso la vida del asset es más larga que la vida de un actor individual.

`ActorWorld::Clear` elimina actores, estados de suspensión, niveles, tipos y la caché de modelos.

## Ciclo de vida de un asset

```text
Ruta o asset compartido
        │
        ▼
Resolver ruta / caché ActorWorld
        │
        ▼
Model::Load
        │
        ├── FBX: geometría, skeleton, clips y FBX vecinos
        └── glTF: materiales, nodos, skins, mallas y animaciones
        │
        ▼
Model compartido
        │
        ├── Actor A -> ModelInstance A -> pose A
        ├── Actor B -> ModelInstance B -> pose B
        └── preview/editor/tests
        │
        ▼
último shared_ptr liberado
        │
        ▼
asset fuera de memoria
```

### Diferencias entre `Model` y `ModelInstance`

| Objeto | Vida | Compartido | Contiene |
|---|---|---|---|
| `Model` | Cacheado en `ActorWorld` | Sí | mallas, materiales, skeleton y clips |
| `ModelInstance` | Una por `Actor` | No | transform, estado de pose y representación de esa instancia |
| `CharacterAnimator` | Una por actor rigged | No | locomoción, montajes, IK, bindings efectivos y transiciones |
| `AnimManifest` | Copia/base por actor | Habitualmente derivado de un manifest común | sets, turnos, montajes y abilities |

Cambiar bindings puede reconstruir el animator de los actores sin volver a cargar necesariamente el `Model`.

## Comparación con Unreal Engine

Fuente oficial: [Unreal Engine Actor Lifecycle](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-actor-lifecycle?lang=es-ES).

| Concepto | Este proyecto | Unreal Engine |
|---|---|---|
| Instancia mínima | `GBObject`: nombre, transform y componentes | `UObject` como base de objetos; `AActor` como instancia en `UWorld` |
| Actor | `Actor : GBObject`, con gameplay opcional | `AActor`, normalmente compuesto por `UActorComponent` |
| Asset de modelo | `Model`, cargado/cacheado con `shared_ptr` | `UObject`/asset gestionado por el sistema de objetos y referencias |
| Instanciación | `ObjectWorld::Spawn` + `GBObject::OnSpawnStart` | creación/spawn del `AActor` y ejecución de inicialización |
| Construcción visual | `GeometryComponent` + `ModelInstance` + `AnimationComponent` | componentes del actor, construcción de componentes y render state |
| Enlace al mundo | `ObjectWorld`, `GBObjectContext`, escena y `GameplayWorld` | `UWorld` y nivel |
| Inicio de juego | `OnSpawnEnd` -> publicación -> `BeginPlay` | construcción -> registro en mundo -> `BeginPlay` |
| Actualización | `ObjectWorld::Update` -> componentes -> `Actor` | `Tick` y ticks de componentes |
| Input/IA | `OrderSource`, abilities, tasks, AI, script o replay | controlador, componentes y gameplay framework |
| Guardado | `CaptureActors`/`RestoreActors` explícitos | serialización y sistemas de save propios del juego |
| Pausa | `OnPause`/`OnResume`, separada de LOD visual | pausa/tick habilitado según actor y componentes |
| Fin de juego | `EndPlay` antes de liberar la instancia | `EndPlay` durante destrucción o salida del mundo |
| Destrucción | `OnDestroy` + `unique_ptr`, destructor determinista | `Destroy`, estado Pending Kill y posterior garbage collection |
| Asset sin referencias | último `shared_ptr` libera el modelo | garbage collection cuando no quedan referencias alcanzables |

### Equivalencias aproximadas

```text
ObjectWorld::Spawn + GBObject::OnSpawnStart
        ≈ SpawnActor + construcción/inicialización de componentes

OnSpawnEnd + publicación + BeginPlay
        ≈ registro en el mundo + BeginPlay

ObjectWorld::Update
        ≈ Tick del actor y sus componentes

EndPlay + OnDestroy
        ≈ Destroy/EndPlay, pero sin fase PendingKill

destrucción de unique_ptr tras OnDestroy
        ≈ liberación posterior por GC, aunque aquí es determinista e inmediata
```

### Diferencias importantes

1. **No existe `PendingKill`:** después de `Remove`, el actor se destruye inmediatamente cuando desaparece su `unique_ptr`. Los punteros no propietarios dejan de ser válidos.
2. **No existe garbage collection global:** la vida depende del ownership C++ y del recuento de referencias de `shared_ptr`.
3. **La propuesta sí tiene `BeginPlay` explícito:** todavía no está implementado en código, pero debe pertenecer a `GBObject` y heredarse en `Actor`; `PrepareAbilities` será una operación interna de activación, no el sustituto del ciclo de vida.
4. **No hay construcción de Blueprint:** el equivalente de la definición de un objeto estará en `ObjectDefinition`/`ActorDefinition`, componentes y configuración de assets.
5. **El mundo y el gameplay están enlazados por handles:** el nombre es la clave legible y de serialización (única por mundo), pero las referencias en memoria usan el handle estable de `GBObject`.
6. **La instancia visual está separada del asset:** `GeometryComponent` puede crearse, cargarse tarde, fallar o estar ausente sin invalidar `GBObject` o `Actor`.
7. **La carga puede ser síncrona o asíncrona:** `OnLoad` prepara el asset y `GeometryComponent` resuelve cuándo está listo para instanciarlo.
8. **La simulación y el render tienen una frontera clara:** `Actor` actualiza lógica aunque no tenga geometría; LOD afecta principalmente a componentes de presentación.

## Mapa de mejoras recomendadas

La propuesta adopta la claridad del ciclo de Unreal sin copiar su garbage collector ni exigir que todo objeto sea un actor renderizable:

1. Crear `GBObject` como clase base abstracta con `Name`, `Transform`, estado y componentes.
2. Hacer que `Actor` herede de `GBObject` y sólo añada gameplay; no debe exigir `Model`.
3. Separar `ObjectWorld::Spawn` de la carga visual y permitir `GBObject` sin `GeometryComponent`.
4. Separar `OnLoad`, `BuildInstance`, `BindGameplay` y `BeginPlay`.
5. Definir estados `Constructing`, `Active`, `Paused`, `Ending` y `Destroyed`.
6. Definir un handle estable; conservar el nombre como display name o clave serializable compatible.
7. Definir eventos `ObjectSpawned`, `BeginPlay`, `EndPlay` y `ObjectDestroyed`.
8. Añadir una política explícita de carga síncrona, precargada o asíncrona.
9. Crear snapshots de `GBObject` y componentes para UI y diagnóstico.
10. Añadir tests de ciclo:
   - spawn de `GBObject` sólo con nombre y transform;
   - `Actor` sin geometría;
   - geometría ausente, pendiente y fallida;
   - modelo inválido sin invalidar el actor lógico;
   - falta de clips como error sólo de `AnimationComponent`;
   - `EndPlay` antes de `OnDestroy`;
   - clear con objetos que comparten modelo;
   - objeto pausado y reanudado.

## Conclusión

La arquitectura objetivo toma de Unreal la separación entre instancia de mundo y componentes, y la formalidad de `BeginPlay`/`EndPlay`, pero mantiene las ventajas de este motor: ownership C++ explícito, destrucción determinista y assets compartidos con `shared_ptr`.

La jerarquía base será:

```text
GBObject
  -> OnSpawnStart
  -> OnSpawnEnd
  -> BeginPlay
  -> Update / Tick
  -> OnPause / OnResume
  -> EndPlay
  -> OnDestroy

Actor : GBObject
  -> componentes de movimiento, input, IA, abilities y persistencia
  -> GeometryComponent opcional
  -> AnimationComponent opcional
```

Esto permite crear desde un punto fijo con transform hasta un actor con mesh, animaciones, IA o input de jugador sin hacer que `EditorApp`, ImGui o los paneles conozcan los detalles internos del runtime.
