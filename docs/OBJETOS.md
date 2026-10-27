# Objetos, inventario y recursos (H2)

Lógica pura en `GB/Gameplay/Items.h`, sin escena ni renderer y determinista (contenedores ordenados, sin azar).

| Pieza | Qué es |
|---|---|
| `ItemDef` / `ItemLibrary` | Objeto en datos: id, nombre, categoría, peso, apilado máximo, etiquetas (`Item.Tool.Axe`), habilidad que lo usa y socket donde se muestra |
| `Inventory` | Contenedor con número de casillas y límite de peso opcional: `Add`, `Remove`, `Count`, `CountTagged`, `Transfer`, `ToJson`/`FromJson` |
| `ResourceNodeDef` / `ResourceNode` | Nodo de recurso (árbol, roca, baya, hierba): cantidad, recogida por golpe, regeneración (`RegenSeconds`) o agotamiento, herramienta requerida por etiqueta |
| `Recipe` / `RecipeBook` | Materia prima → producto, con estación y herramienta (necesaria, no se gasta). `WhyNot` explica por qué no se puede fabricar |
| `RegisterEraOneItems` | Contenido de la era 1: madera, piedra, fibra, baya, hacha y pico de piedra y de pedernal, cesta de mimbre, hoguera |

Cada `Actor` lleva una mochila de 12 casillas (`Actor::Bag()`), que se guarda y se carga con la partida (`ActorState::Inventory`).

## Herramientas con nivel y contenedores (E2)

Una variante de herramienta lleva una etiqueta hija (`Item.Tool.Axe.Flint`) y por la jerarquía de etiquetas sirve donde se pida `Item.Tool.Axe`: las habilidades `Chop`/`Mine`, los nodos (`requires`) y las recetas (`tool`) la aceptan sin cambios.

Campos opcionales de `ItemDef` (en el JSON: `power`, `speed`, `bagSlots`, `bagWeight`; si faltan valen 0, 1, 0 y 0, así que los ficheros antiguos siguen cargando; `ContentToJson` solo los escribe si no son los valores por defecto):

| Campo | Qué hace |
|---|---|
| `power` | Unidades extra por golpe sobre el `perGather` del nodo (el último golpe da lo que quede). Hacha de pedernal: +1, tala un árbol de 5 en 3 golpes en vez de 5 |
| `speed` | Multiplicador de la velocidad del montaje y del reloj del golpe (1,15 = un 15 % más rápido) |
| `bagSlots`, `bagWeight` | Casillas y límite de peso extra mientras el objeto va en la mochila |

- `ResourceSystem::SyncTools` guarda, por familia de herramienta (`ToolFamily`: tres segmentos, `Item.Tool.Axe`), los atributos `Power:<familia>` y `Speed:<familia>` de la **mejor** herramienta que lleva el actor (más potencia, luego más velocidad, luego el id menor); sin herramienta vuelven a 0 y 1. El nodo lee `Power:<su requires>` al sacar unidades y `PlayMontageAndWaitTask` lee `Speed:<familia que pide la habilidad>` (`MontagePlayer::Play` admite ahora un `rateScale`). Los NPC de la tribu lo reciben igual, porque usan las mismas habilidades.
- Contenedores: objetos con la etiqueta `Item.Container` (cesta de mimbre: +6 casillas, +8 kg). Regla: cuenta **solo el mejor** que lleve el actor (más casillas, luego más peso); no se suman. `Inventory::SyncCapacity` aplica el bono; `Slots()` y `MaxWeight()` devuelven capacidad propia más bono. El bono se deriva del contenido, así que **no se guarda**: `ActorState::Inventory` sigue guardando solo las pilas y el editor llama a `SyncCapacity` en cada paso (`StepResources`, también tras cargar). El formato del guardado no cambia. `Inventory::ToJson` añade `bonusSlots`/`bonusWeight` solo cuando hay bono, y un texto antiguo sin ellos carga igual.
- Si se pierde la cesta, el contenido se queda aunque exceda la capacidad; no entra nada más hasta que haya sitio. El bono de peso solo afecta a mochilas con límite (la del actor es ilimitada hoy).
- Contenido en `era1.json`: `branch`, `flint`, `reed`, `cordage`, `flint_axe` y `flint_pickaxe` (potencia 1, velocidad 1,15), `basket_wicker` y las recetas `cordage`, `flint_axe`, `flint_pickaxe`, `basket_wicker`. Aún no hay nodos de rama, pedernal ni junco en el mundo (`era1_ampliado.json` los define, pero `resources build` no los coloca): hasta entonces, `actor bag <nombre> add flint 3`.
- Editor: `actor bag <nombre>` y el panel de inventario muestran casillas usadas/totales (con el origen del bono), peso y, por familia, la herramienta activa con su potencia y velocidad.
- Pendiente: durabilidad (la propuesta no la pide), niveles duros (`Item.Tool.Pickaxe.Hard`, parte de E1) y que el nodo compruebe la herramienta en la habilidad.

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

- Habilidades `Chop` (necesita `Item.Tool.Axe`), `Mine` (`Item.Tool.Pickaxe`), `Gather` (bayas, a mano) y `PullFiber` (matas de fibra). Todo actor las tiene. Cada tipo de nodo dice la suya (`ability` en `assets/data/era1.json`). Los montajes son provisionales (tajos de espada con la herramienta en la mano para talar y picar, brazo a media altura para las bayas y agacharse para la fibra), ver `ANIMACIONES_PENDIENTES.md`.
- `ResourceSystem` saca las unidades del nodo y las entrega a la mochila de quien lo recoge; las herramientas de la mochila pasan a ser etiquetas del actor (`SyncTools`); un nodo agotado pierde lo que se recoge de él (copa, bayas, mata, roca) y lo recupera al regenerarse.
- La regeneración es un efecto periódico del sistema de habilidades (`Regrow.<id>`), así que se guarda con la partida; las rocas no regeneran.
- Editor: `resources build [x z]` coloca un parche de nodos, `resources status` los lista. Los nombres con espacio se pasan con guion bajo en las listas de tareas: `actor tasks worker ability Chop tree_2`.

## Contenido como datos

Objetos, recetas y nodos de recurso viven en un documento JSON (`assets/data/era1.json`, que el editor lee al arrancar encima de lo que define el código): añadir un objeto, una
receta o la definición de un tipo de nodo no necesita compilar. **Colocar** nodos sí está limitado: `resources build` (`BuildResources`) solo sabe poner los cuatro tipos de la era 1 (`tree`, `rock`, `berry_bush`, `fiber_grass`), así que un tipo nuevo definido en JSON no aparece en el mundo hasta que haya un colocador por datos (mallas y posición por tipo). Las entradas con el mismo `id` sustituyen a las anteriores; un documento con un error se rechaza entero y dice cuál.

```
content status                       cuántos hay
content export <fichero.json>        escribe lo que hay cargado (punto de partida de un fichero propio)
content load <fichero.json>          añade o sustituye por id (por ejemplo una era nueva)
```

Se comprueba que las recetas fabriquen y pidan objetos que existen, que los nodos den objetos que existen, que las etiquetas de los objetos sean válidas (y vengan en una lista) y que apilado, peso y cantidades tengan sentido; un valor del tipo equivocado (un texto donde va un número) se rechaza con un mensaje, nunca con una excepción. No se comprueba que la etiqueta `requires` de un nodo ni la habilidad que nombra un objeto existan.
`Inventory::FromJson` rechaza enteros dañados: cuentas menores que 1, casillas o peso negativos, valores que no son números.
El test `items` comprueba que el fichero del juego define lo mismo que `RegisterEraOneItems`.

## Regeneración y estaciones

Los nodos regeneran en `ResourceSystem::Update(gameplay, dt, rate)`, con el temporizador en el atributo `Regrow` del nodo (se guarda con la partida). El editor pasa como `rate` el
factor de la estación ([ECOLOGIA.md](ECOLOGIA.md)): en invierno no crece ni una baya.

## Pendiente de H2

- Colocar nodos desde el panel del editor (los tipos ya son datos).
- Montajes propios de talar, picar y recoger; faltan también los de recoger del suelo, dejar y cavar en bucle (`ANIMACIONES_PENDIENTES.md`).
- Agarre visible del objeto en su socket y contenedores en el mundo (caja, almacén).
- Fabricar como habilidad con estación en la escena (hoy `actor bag <nombre> craft`).
