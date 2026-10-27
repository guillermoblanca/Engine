# Objetos, inventario y recursos (H2)

Lógica pura en `GB/Gameplay/Items.h`, sin escena ni renderer y determinista (contenedores ordenados, sin azar).

| Pieza | Qué es |
|---|---|
| `ItemDef` / `ItemLibrary` | Objeto en datos: id, nombre, categoría, peso, apilado máximo, etiquetas (`Item.Tool.Axe`), habilidad que lo usa y socket donde se muestra |
| `Inventory` | Contenedor con número de casillas y límite de peso opcional: `Add`, `Remove`, `Count`, `CountTagged`, `Transfer`, `ToJson`/`FromJson` |
| `ResourceNodeDef` / `ResourceNode` | Nodo de recurso (árbol, roca, baya, hierba): cantidad, recogida por golpe, regeneración (`RegenSeconds`) o agotamiento, herramienta requerida por etiqueta |
| `Recipe` / `RecipeBook` | Materia prima → producto, con estación y herramienta (necesaria, no se gasta). `WhyNot` explica por qué no se puede fabricar |
| `RegisterEraOneItems` | Contenido de la era 1: madera, piedra, fibra, baya, hacha y pico de piedra, hoguera |

Cada `Actor` lleva una mochila de 12 casillas (`Actor::Bag()`), que se guarda y se carga con la partida (`ActorState::Inventory`).

## Editor

```
actor bag <actor>                       lista lo que lleva
actor bag <actor> add <objeto> [n]
actor bag <actor> remove <objeto> [n]
actor bag <actor> craft <receta> [estación]
```

## Pendiente de H2

- Nodos de recurso como entidades de la escena (con su estado en la partida) y su colocación desde el editor.
- Habilidades de recoger, talar y picar ligadas a los nodos, con sus montajes; faltan los de recoger del suelo, dejar y cavar en bucle (ver `ANIMACIONES_PENDIENTES.md`).
- Agarre visible del objeto en su socket y contenedores en el mundo (caja, almacén).
