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

## Nodos de recurso en el mundo

`GB/Gameplay/Resources.h`: árboles, rocas, arbustos de bayas y matas de fibra son entidades con sistema de habilidades (como las parcelas del huerto).
Llevan el atributo `Amount` y las etiquetas `Resource.Node`, `Resource.Node.<id>` y `Resource.Wood|Stone|Plant`; vacíos, `State.Depleted`.

- Habilidades `Chop` (necesita `Item.Tool.Axe`), `Mine` (`Item.Tool.Pickaxe`) y `Gather` (a mano). Todo actor las tiene. El montaje es provisional (`dig_plant`, `pick_fruit`), ver `ANIMACIONES_PENDIENTES.md`.
- `ResourceSystem` saca las unidades del nodo y las entrega a la mochila de quien lo recoge; las herramientas de la mochila pasan a ser etiquetas del actor (`SyncTools`); un nodo agotado pierde lo que se recoge de él (copa, bayas, mata, roca) y lo recupera al regenerarse.
- La regeneración es un efecto periódico del sistema de habilidades (`Regrow.<id>`), así que se guarda con la partida; las rocas no regeneran.
- Editor: `resources build [x z]` coloca un parche de nodos, `resources status` los lista. Los nombres con espacio se pasan con guion bajo en las listas de tareas: `actor tasks worker ability Chop tree_2`.

## Pendiente de H2

- Colocar nodos desde el panel del editor y definirlos en un fichero de datos (hoy están en `RegisterEraOneItems`).
- Montajes propios de talar, picar y recoger; faltan también los de recoger del suelo, dejar y cavar en bucle (`ANIMACIONES_PENDIENTES.md`).
- Agarre visible del objeto en su socket y contenedores en el mundo (caja, almacén).
- Fabricar como habilidad con estación en la escena (hoy `actor bag <nombre> craft`).
