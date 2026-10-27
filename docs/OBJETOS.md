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

## Contenido como datos

Objetos, recetas y nodos de recurso viven en un documento JSON (`assets/data/era1.json`, que el editor lee al arrancar encima de lo que define el código): añadir un objeto, una
receta o un tipo de nodo no necesita compilar. Las entradas con el mismo `id` sustituyen a las anteriores; un documento con un error se rechaza entero y dice cuál.

```
content status                       cuántos hay
content export <fichero.json>        escribe lo que hay cargado (punto de partida de un fichero propio)
content load <fichero.json>          añade o sustituye por id (por ejemplo una era nueva)
```

Se comprueba que las recetas fabriquen y pidan objetos que existen, que los nodos den objetos que existen, que las etiquetas sean válidas y que apilado, peso y cantidades tengan sentido.
El test `items` comprueba que el fichero del juego define lo mismo que `RegisterEraOneItems`.

## Regeneración y estaciones

Los nodos regeneran en `ResourceSystem::Update(gameplay, dt, rate)`, con el temporizador en el atributo `Regrow` del nodo (se guarda con la partida). El editor pasa como `rate` el
factor de la estación ([ECOLOGIA.md](ECOLOGIA.md)): en invierno no crece ni una baya.

## Pendiente de H2

- Colocar nodos desde el panel del editor (los tipos ya son datos).
- Montajes propios de talar, picar y recoger; faltan también los de recoger del suelo, dejar y cavar en bucle (`ANIMACIONES_PENDIENTES.md`).
- Agarre visible del objeto en su socket y contenedores en el mundo (caja, almacén).
- Fabricar como habilidad con estación en la escena (hoy `actor bag <nombre> craft`).
