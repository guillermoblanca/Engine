# Objetos, inventario y recursos (H2)

Lógica pura en `GB/Gameplay/Items.h`, sin escena ni renderer y determinista (contenedores ordenados, sin azar).

| Pieza | Qué es |
|---|---|
| `ItemDef` / `ItemLibrary` | Objeto en datos: id, nombre, categoría, peso, apilado máximo, etiquetas (`Item.Tool.Axe`), habilidad que lo usa, socket donde se muestra y combustible (`fuel`) |
| `Inventory` | Contenedor con número de casillas y límite de peso opcional: `Add`, `Remove`, `Count`, `CountTagged`, `Transfer`, `ToJson`/`FromJson` |
| `ResourceNodeDef` / `ResourceNode` | Nodo de recurso (árbol, roca, baya, hierba): cantidad, recogida por golpe, regeneración (`RegenSeconds`) o agotamiento, herramienta requerida por etiqueta |
| `Recipe` / `RecipeBook` | Materia prima → producto, con estación y herramienta (necesaria, no se gasta). `WhyNot` explica por qué no se puede fabricar. Con un conjunto de recetas conocidas (`WhyNot`/`Craft(..., known)`, H11) solo se fabrica lo que se sabe hacer: «does not know how to make 'x'»; sin él (`nullptr`), todo |
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
- Contenido en `era1.json`: `branch`, `flint`, `reed`, `cordage`, `flint_axe` y `flint_pickaxe` (potencia 1, velocidad 1,15), `basket_wicker` y las recetas `cordage`, `flint_axe`, `flint_pickaxe`, `basket_wicker`. Con E1, ramas, pedernal y junco salen de nodos que `resources build` coloca (sección «Nodos más finos»).
- Editor: `actor bag <nombre>` y el panel de inventario muestran casillas usadas/totales (con el origen del bono), peso y, por familia, la herramienta activa con su potencia y velocidad.
- Pendiente: durabilidad (la propuesta no la pide), niveles duros (`Item.Tool.Pickaxe.Hard`, parte de E1) y que el nodo compruebe la herramienta en la habilidad.

## Nodos más finos, combustible y cobre (E1)

**Tipo de nodo desde datos.** `ResourceNodeDef::Kind` (JSON `kind`) dice qué es el nodo; el nodo lleva la etiqueta `Resource.<Kind>` y las habilidades casan por ella (`Chop` en `Resource.Wood`, `Mine` en `Resource.Stone` y por debajo, `Gather` en `Resource.Plant`, `PullFiber` en las matas de fibra y la nueva `Collect` en `Resource.Loose`). Ya no se deduce del id del objeto; si `kind` falta (un fichero antiguo) se usa la regla de antes (madera, piedra, y lo demás planta). Como las etiquetas son jerárquicas, `Stone.Flint` y `Stone.Copper` se pican con `Mine` y siguen siendo piedra.

| Nodo | Da | Capacidad / golpe | Regenera | Herramienta | Habilidad | Tipo |
|---|---|---|---|---|---|---|
| `tree` | `wood` | 5 / 1 | 1 cada 30 s | hacha | `Chop` | `Wood` |
| `old_tree` | `wood` | 8 / 1 | 1 cada 120 s | hacha | `Chop` | `Wood` (grande) |
| `young_tree` | `branch` | 3 / 1 | 1 cada 40 s | manos | `Gather` | `Plant.Branch` |
| `clay_bank` | `clay` | 8 / 2 | 2 cada 60 s | manos | `Collect` | `Loose.Clay` |
| `reed_bed` | `reed` | 8 / 2 | 2 cada 20 s | manos | `Collect` | `Loose.Reed` |
| `flint_outcrop` | `flint` | 5 / 1 | no | pico | `Mine` | `Stone.Flint` |
| `copper_vein` | `copper_ore` | 4 / 1 | no | pico | `Mine` | `Stone.Copper` |

Árbol grande = **nodos hermanos** (decisión 1 de la propuesta): el `old_tree` da madera y los `young_tree` que crecen a su lado dan ramas; no hay `Yields` todavía. `Collect` es una habilidad nueva (a mano, montaje de agacharse) en vez de `Dig`/`Cut`: `Dig` ya es la azada del huerto y la propuesta pide `Gather` a mano para arcilla y junco. `young_tree` es `Plant.Branch` para que `Gather` lo case sin tocar sus requisitos; ojo, los conejos de `Ecology` comen todo lo que sea `Resource.Plant`, también estos brotes.

**Colocación por datos.** Cada nodo puede llevar un `patch`: `count`, `perWorker`, `spacing`, `shape` (`row` en filas de ocho, `ring`, `cluster`: dispersión de ángulo áureo, siempre igual) y `near` (otro tipo junto al que se coloca, para los hermanos). `BuildResourcePatch(world, origen, nodos, trabajadores)` pone `count + perWorker × trabajadores` de cada tipo que tenga parche, cada tipo en su zona del parche, sin solaparse; los cuatro tipos antiguos llevan los mismos números que `ScaledResources` (2+2w, 3+2w, 2+w, 2+w). `model` (opcional, por defecto el id) elige las primitivas con que se dibuja: árbol joven pequeño y claro, viejo grande y oscuro, arcilla marrón, junco verde alto y fino, pedernal gris oscuro, cobre verdoso; un modelo desconocido es una masa gris. Un nodo vacío oculta las partes que se recogen (`Crown`, `Berries`, `Tuft`, `Rock`, `Mound`, `Reeds`, `Flint`, `Ore`). `BuildResources` con recuentos sigue existiendo (los cuatro tipos de siempre) para los tests y escenas que lo usan.

**Combustible por objeto.** `ItemDef::Fuel` (JSON `fuel`): calor que da el objeto en `Stoke`. Madera 2, rama 1 (hace falta el doble de ramas para el mismo fuego), tronco `log` 4 en el contenido ampliado. Todo objeto con etiqueta `Item.Resource.Wood` (o hija) puede ir al fuego; se echa primero el de más calor y luego el de id menor. `InteractiveSystem::Items` apunta a la biblioteca (el editor la pone); sin ella `Stoke` echa `wood` por `Config::FuelPerWood`, como antes, y ese valor es también el calor de un objeto de madera que no declare `fuel`. El fuego de la tribu (`Tribe::Burn`) sigue la misma regla con lo que hay en el almacén: cada 30 s necesita `FireHeat` (2) de calor, una madera o dos ramas.

**Cobre.** Objetos `copper_ore` (mena), `copper_ingot` (lingote) y `clay`, nodo `copper_vein` y la receta `copper_ingot` (2 de mena, estación `forge`). La forja llega con E10, así que no se puede fabricar aún: `WhyNot` dice «needs the station 'forge'». En el contenido ampliado el hacha de cobre pide lingotes.

Editor: `resources build [x z [trabajadores]]` coloca el parche por datos y dice cuántos de cada tipo; `resources status` agrupa por tipo (nodos, unidades, agotados y cada uno); el panel World state ya agrupa por tipo y muestra los nuevos. Probar: `resources build 0 0 3`, `actor bag <nombre> add stone_pickaxe`, `ability activate Collect "clay bank 1" <nombre>`.

Decisión pendiente: `old_tree` da `wood` (el mismo objeto que `tree`, para que la tribu y las recetas actuales lo usen); la propuesta lo llamaba «tronco» (`log`, 6 kg, de 4 en 4), que queda en el contenido ampliado para tablones.

## Editor

```
actor bag <actor>                       lista lo que lleva
actor bag <actor> add <objeto> [n]
actor bag <actor> remove <objeto> [n]
actor bag <actor> craft <receta> [estación]
```

## Nodos de recurso en el mundo

`GB/Gameplay/Resources.h`: árboles, rocas, arbustos de bayas y matas de fibra son entidades con sistema de habilidades (como las parcelas del huerto).
Llevan el atributo `Amount` y las etiquetas `Resource.Node`, `Resource.Node.<id>` y `Resource.<kind>` (`Wood`, `Stone`, `Plant`, `Loose.Clay`...; sale del campo `kind` del nodo); vacíos, `State.Depleted`.

- Habilidades `Chop` (necesita `Item.Tool.Axe`), `Mine` (`Item.Tool.Pickaxe`), `Gather` (bayas, a mano) y `PullFiber` (matas de fibra). Todo actor las tiene. Cada tipo de nodo dice la suya (`ability` en `assets/data/era1.json`). Los montajes son provisionales (tajos de espada con la herramienta en la mano para talar y picar, brazo a media altura para las bayas y agacharse para la fibra), ver `ANIMACIONES_PENDIENTES.md`.
- `ResourceSystem` saca las unidades del nodo y las entrega a la mochila de quien lo recoge; las herramientas de la mochila pasan a ser etiquetas del actor (`SyncTools`); un nodo agotado pierde lo que se recoge de él (copa, bayas, mata, roca) y lo recupera al regenerarse.
- La regeneración es un efecto periódico del sistema de habilidades (`Regrow.<id>`), así que se guarda con la partida; las rocas no regeneran.
- Editor: `resources build [x z [trabajadores]]` coloca el parche por datos (cada nodo asentado sobre el terreno, `SnapToGround`), `resources world [densidad]` reparte los nodos por todo el mundo generado según el bioma ([RECURSOS_MUNDO.md](RECURSOS_MUNDO.md)), `resources status` los lista agrupados por tipo. Los nombres con espacio se pasan con guion bajo en las listas de tareas: `actor tasks worker ability Chop tree_2`.

## Contenido como datos

Objetos, recetas y nodos de recurso viven en un documento JSON (`assets/data/era1.json`, que el editor lee al arrancar encima de lo que define el código): añadir un objeto, una
receta o la definición de un tipo de nodo no necesita compilar. **Colocar** nodos también sale de los datos (E1): el campo `patch` del nodo dice cuántos y cómo (ver arriba); la malla sí está en el código, una por `model` conocido. Las entradas con el mismo `id` sustituyen a las anteriores; un documento con un error se rechaza entero y dice cuál.

```
content status                       cuántos hay
content export <fichero.json>        escribe lo que hay cargado (punto de partida de un fichero propio)
content load <fichero.json>          añade o sustituye por id (por ejemplo una era nueva)
```

Se comprueba que las recetas fabriquen y pidan objetos que existen, que los nodos den objetos que existen, que las etiquetas de los objetos sean válidas (y vengan en una lista) y que apilado, peso y cantidades tengan sentido; un valor del tipo equivocado (un texto donde va un número) se rechaza con un mensaje, nunca con una excepción. No se comprueba que la etiqueta `requires` de un nodo ni la habilidad que nombra un objeto existan.
`Inventory::FromJson` rechaza enteros dañados: cuentas menores que 1, casillas o peso negativos, valores que no son números.
El test `items` comprueba que el fichero del juego define lo mismo que `RegisterEraOneItems`.

**Paquetes (H17).** `era1.json` es el fichero del paquete `era1` (`assets/data/era1.pack.json`), que el editor carga al arrancar; `era1_ampliado.json` es el paquete opcional
`era1_ampliado`, que depende de él (`pack load era1_ampliado`). Al cargar se comprueba además que **todo se pueda conseguir**: que cada objeto salga de un nodo, de una
receta alcanzable o de lo que el paquete declara dado (`given`), que las herramientas que piden nodos y recetas se puedan fabricar, que las estaciones existan y que no haya
ciclos de recetas sin entrada. Ver [PAQUETES.md](PAQUETES.md).

## Regeneración y estaciones

Los nodos regeneran en `ResourceSystem::Update(gameplay, dt, rate)`, con el temporizador en el atributo `Regrow` del nodo (se guarda con la partida). El editor pasa como `rate` el
factor de la estación ([ECOLOGIA.md](ECOLOGIA.md)): en invierno no crece ni una baya.

## Recetas que hay que saber (H11)

Las tecnologías (sección `techs`, [CONOCIMIENTO.md](CONOCIMIENTO.md)) bloquean recetas y nodos: las herramientas de sílex detrás del tallado de sílex, la cesta detrás
de la cestería y el lingote detrás de la fundición. Lo que ninguna tecnología nombra sigue siendo de todos. La tribu solo fabrica (herramienta del oficio, hoguera) y solo
recoge lo que sabe cada miembro; las tecnologías del principio (piedra, fuego, cordel) hacen que todo lo de antes siga igual. `actor bag <actor> craft` no tiene puerta
(el jugador lo sabe todo por ahora).

## Pendiente de H2

- Colocar nodos desde el panel del editor (los tipos y el parche ya son datos).
- `Yields` (un nodo, varias salidas) en lugar de nodos hermanos, y que el fuego de la tribu queme ramas.
- Montajes propios de talar, picar y recoger; faltan también los de recoger del suelo, dejar y cavar en bucle (`ANIMACIONES_PENDIENTES.md`).
- Agarre visible del objeto en su socket y contenedores en el mundo (caja, almacén).
- Fabricar como habilidad con estación en la escena (hoy `actor bag <nombre> craft`).
