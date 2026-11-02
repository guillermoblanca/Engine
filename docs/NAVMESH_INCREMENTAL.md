# NavMesh incremental (propuesta)

Propuesta de diseño para un navmesh de polígonos con invalidación incremental en runtime, al estilo del `dtTileCache` de Recast/Detour que usa
`RecastNavMesh` en Unreal, que conviva con la cuadrícula actual (`GB::NavGrid`, ver [NAVEGACION.md](NAVEGACION.md)) detrás de una interfaz común. **No
implementada todavía** (es la Fase 4 de [GRAFO_TERRENO.md](GRAFO_TERRENO.md)); este documento fija el diseño antes de escribir el código.

## 1. Por qué

`NavGrid` solo conoce obstáculos circulares (`CircleObstacle`), una limitación que el propio equipo documenta en NAVEGACION.md y en
`PLAN_SOCIEDAD_VIVA.md` como motivo para ir hacia un navmesh (un edificio rectangular se aproxima hoy con varios círculos). Además, investigando el
código se confirmó que **no existe ningún camino real que modifique la altura del terreno en runtime y lo notifique a la navegación**:

- `Terrain::GetEditableHeightmap()` + `Terrain::RefreshMesh(i0,j0,i1,j1)` existen pero ningún sistema del motor, Editor o Sandbox los llama hoy.
- `NavGrid::UpdateRegion` (pensada exactamente para "el suelo se editó, un río se movió") existe pero nadie la llama: es API muerta.
- `DestructionSystem` (física, fragmenta cuerpos rígidos) está totalmente aislado del `ActorWorld`/`NavGrid`.
- La única vía de invalidación real hoy es el *polling* de 1 segundo del Editor (`EditorApp::m_NavSyncTimer` → `PlayerMode::SyncNavigation`), que
  reconstruye toda la lista de obstáculos de la escena cada vez, no un evento.

Esto no es un bug a arreglar sino un camino a construir desde cero.

## 2. Interfaz común: `IPathProvider`

```cpp
// GB/Navigation/IPathProvider.h
class IPathProvider
{
public:
    virtual ~IPathProvider() = default;
    virtual bool Ready() const = 0;
    virtual bool Walkable(const glm::vec2& p, const NavProfile& profile = NavProfile()) const = 0;
    virtual bool FindPath(const NavProfile&, const glm::vec2& from, const glm::vec2& to, NavPath& out,
        const std::vector<CircleObstacle>* extra = nullptr) const = 0;
    virtual bool LineClear(const NavProfile&, const glm::vec2& a, const glm::vec2& b, float* cost = nullptr,
        const std::vector<CircleObstacle>* extra = nullptr) const = 0;
    virtual uint32_t Revision() const = 0;
    virtual bool ChangedSince(uint32_t revision, const glm::vec2& a, const glm::vec2& b, float pad = 2.0f) const = 0;
};
```

`GB::NavGrid` ya tiene exactamente estos métodos con estas firmas (`NavGrid.h:99-139`): añadir `: public IPathProvider` y `override` es un cambio de
riesgo cero (mismo código, cero comportamiento nuevo). **DIP/LSP**: `AIDriver`, `ITribeBody::Route`/`SetNavigation` dejan de incluir `NavGrid.h`
directamente y reciben un `const IPathProvider*`, así que el futuro `NavMeshTileCache` se puede sustituir sin tocar quién lo consulta — el mismo tipo de
inversión que `AbstractAgent` ya practica hoy con sus `Route`/`Gate` como `std::function` en vez de conocer `NavGrid` (`GB/AI/Abstract.h`).

## 3. Tiles con invalidación incremental

```cpp
// GB/Navigation/NavMeshTileCache.h
struct NavMeshTile
{
    int TileI, TileJ;
    std::vector<glm::vec2> Vertices;        // poligonal, no celdas
    std::vector<std::array<int, 3>> Tris;
    uint32_t Revision = 0;
    bool Dirty = true;
};

class NavMeshTileCache final : public IPathProvider
{
public:
    bool Build(const CharacterWorld& world, const NavConfig& config, std::string* error = nullptr);
    void InvalidateRegion(const glm::vec2& min, const glm::vec2& max);   // marca sucias las tiles tocadas; no reconstruye todavía
    int ProcessDirtyTiles(double budgetMs);                              // reconstruye tiles sucias hasta un presupuesto, una vez por fotograma
    // ... Walkable/FindPath/LineClear/Revision/ChangedSince de IPathProvider ...
};
```

**SRP**: la triangulación (contorno de las celdas caminables + *ear clipping* o similar) vive aparte en `NavMeshBuilder.h`, para no repetir en
`NavMeshTileCache` el anti-patrón de `Terrain` como *God Class*. `InvalidateRegion`/`ProcessDirtyTiles` separan "marcar sucio" de "reconstruir" para que
una edición de terreno no cueste un *stall*: se marca al instante y se reconstruye repartido en varios fotogramas, con el mismo espíritu que
`NavGrid::NewFrame`/`AllowPlan` ya reparte las búsquedas de camino.

## 4. Notificar el cambio: reutilizar el idioma existente, no un `EventBus` nuevo

El motor no tiene un *EventBus* genérico, pero sí un idioma repetido de listener-por-`std::function` (`Clans.h::Listen`/`m_Listeners`,
`AbilitySystemComponent.h::AddListener`). Se reutiliza igual en vez de introducir un mecanismo ajeno al estilo del motor:

```cpp
// GB/World/TerrainChangeEvents.h
struct TerrainChangedEvent { glm::vec2 Min, Max; };

class TerrainChangeBus
{
public:
    using Listener = std::function<void(const TerrainChangedEvent&)>;
    void Listen(Listener listener) { m_Listeners.push_back(std::move(listener)); }
    void Notify(const TerrainChangedEvent& e) { for (auto& l : m_Listeners) l(e); }
private:
    std::vector<Listener> m_Listeners;
};
```

Punto de conexión real a construir: todo código que llame a `Terrain::GetEditableHeightmap()` + `RefreshMesh()` (hoy nadie; después,
`DestructionSystem` y una futura herramienta de edición de terreno del Editor) termina llamando `bus.Notify({min, max})`.
`NavGrid::UpdateRegion` y `NavMeshTileCache::InvalidateRegion` se suscriben con `bus.Listen(...)`. Esto conecta por primera vez la destrucción con la
navegación.

## 5. Verificación

Nuevo `tests/navigation_navmesh_test.cpp`: edita un heightmap de prueba, dispara `TerrainChangeBus::Notify`, comprueba que solo las tiles tocadas quedan
`Dirty` y que tras `ProcessDirtyTiles` el camino afectado cambia (o pasa a `Partial`) sin reconstruir el resto; compara el resultado con
`NavGrid::UpdateRegion` sobre la misma edición para confirmar que ambas implementaciones de `IPathProvider` dan respuestas consistentes donde las dos
aplican.
