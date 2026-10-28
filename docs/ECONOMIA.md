# Economía del clan (H18: cadenas de producción, E6, E7 y E11)

Qué fabrica el clan y a partir de qué, qué se estropea y cómo se conserva, qué cultiva y dónde guarda las cosas. Lógica pura de CPU y determinista
(mapas por id, empates al primero, sin azar: la lluvia sobre los campos sale de la estación). Las reglas son datos (sección `economy`), el estado va
con la tribu en el guardado.

| Pieza | Dónde | Qué es |
|---|---|---|
| `EconomyRules`, `LoadEconomyRules`, `DefaultEconomyRules`, `CheckEconomy` | `GB/Game/Economy.h` | Las reglas como datos (sección `economy`) |
| `EconomyState`, `Stockpile` | `GB/Game/Economy.h` | Campos, almacenes secundarios y contadores; se guardan con la tribu |
| `PlanProduction`, `ProductionPlan` | `GB/Game/Production.h` | Planificador de cadenas (puro) |
| `AgeInventory`, `LifeLeft` | `GB/Gameplay/Spoilage.h` | Caducidad por montón (pura) |
| `Field`, `FieldPlot`, `CropDef` | `GB/Gameplay/Farming.h` | Campos y cultivos (puro) |
| Ganchos en `Tribe` | `GB/Game/TribeEconomy.cpp` | Plan, trabajos `make`, `farm` y `haul`, reparto de herramientas y cestas, depósito en el almacén más cercano, reloj de caducidad y campos |

`Tribe.cpp` y `TribeWork.cpp` solo llevan llamadas de una línea: `Update` → `StepEconomy`; `PostJobs` → `PlanEconomy` (antes de los trabajos de recoger,
que piden también lo que falta para las cadenas) y `PostEconomyJobs`; `JobScore` → `EconomyJobScore`; `Decide` → `EconomyOptions`; `Execute`/`Finish` →
`ExecuteEconomy`/`FinishEconomy`; la entrega pasa por `DepositPlace`/`DepositAt`.

## Cadenas de producción

Cada vez que se refresca el tablón (`work.jobs.every`, 5 s) el clan calcula lo que **quiere** y el planificador lo convierte en recetas:

1. La herramienta de un oficio que le falta a un miembro (la hace él mismo con el trabajo `craft` de siempre; aquí solo se planifica de qué está hecha).
2. Una herramienta **mejor** de su oficio (más `power`) cuando el clan ya sabe hacerla (`production.upgrade`): un hacha de sílex para el leñador que lleva
   una de piedra. Al volver al campamento la coge del almacén y deja la vieja (`HandOut`).
3. Un contenedor (`Item.Container`: la cesta) por cada miembro que no lleva ninguno (`containersPerMember`).
4. La herramienta del campo (`farming.tool`, la azada) cuando hay campo o se podría marcar uno.
5. Lo que piden otros: `Tribe::SetDemand(origen, objeto, unidades)` (los materiales de una obra de H8; `economy demand` en el editor).
6. Los intermedios que el almacén guarda (`production.stock`: 2 de cordel).
7. Conservar: la comida perecedera que pasa de `preserveAbove` × su existencia objetivo se transforma en algo que dura más si hay receta conocida y estación
   (bayas → bayas secas en el secadero).

`PlanProduction` aparta primero lo que ya hay en el almacén (cada unidad cuenta una vez), fabrica el resto con la primera receta (por id) que el clan sabe
hacer y cuya estación tiene, y cada entrada de esa receta pasa a ser un deseo un nivel más abajo, hasta `production.depth` (3). Lo que ninguna receta hace es
**escasez bruta**: los trabajos `gather` la suman a lo que piden. Si la receta todavía no se sabe (H11), con `production.experiment` se quieren igualmente
sus entradas para poder **probarla**: así el clan hace cordel por su cuenta y el tallado de sílex se descubre jugando (antes solo se probaba si alguien
había dejado cordel en el almacén). Cada paso cuyas entradas ya están en el almacén se publica como trabajo **`make <receta>`** (prioridad
`production.priority`, ×1,1 si es un intermedio). Lo reclama quien sabe hacerlo; va a la estación, trabaja `production.seconds` y lo que sale queda en el
almacén.

**Estaciones.** El campamento tiene las de `production.campStations` (el banco de trabajo). Las demás (secadero, horno, forja) las da quien las tenga:
`Tribe::SetStations(función estación → sitio)`, el enganche para los edificios de H8. Sin ella, una receta de secadero sale como bloqueada («no
drying_rack to make it at»).

## Caducidad y conservación (E6)

- `ItemDef::ShelfLife` (segundos; 0 = no caduca) y `ItemDef::SpoilsTo` (en qué se convierte; vacío = desaparece). JSON `shelfLife`, `spoilsTo`.
  Baya 1800 s (día y medio), baya seca 14 400 s, grano 7200 s.
- `ItemSlot::Age`: la edad media de las unidades del montón. Al juntar montones se hace la media ponderada; `Transfer` lleva la edad; `RemoveOldest` come
  primero lo más viejo y la tribu come primero lo que antes caduca (`FoodToEat`).
- Cada `spoilage.step` (10 s) envejecen el almacén (con el `spoilRate` de su tipo), los almacenes secundarios y las mochilas de los miembros
  (`spoilage.bag`), todo por el factor de la estación (`seasons`: el invierno conserva, ×0,5). Un montón que llega a su vida se estropea entero; el
  registro lo dice («the clan loses 20 berry in the store: they went off»).
- Sustituye al sumidero antiguo (una baya de más de 20 cada 100 s): con 1800 s un montón de 20 que nadie come se pierde a un ritmo parecido, y lo que
  se come a tiempo no se pierde.
- Conservar es una receta: `dried_berry` (3 bayas → 2 secas, estación `drying_rack`) en `era1_ampliado.json`.

## Agricultura (E7)

- Cultivos en datos (`economy.crops`, se suman por id entre paquetes): `era1_ampliado` trae `grain` (semilla `grain_seed`, da 3 de grano y 2 semillas,
  90 s por fase, se siembra en primavera y verano). Las semillas salen de recolectar (`wild_grain`) y de la cosecha.
- El clan **marca un campo** (`farming.plots` parcelas en `columns` columnas, a `offset` del campamento) cuando un miembro sabe `farming.tech`
  (`tillage`) y hay semillas; al lado pone un almacén del tipo `logistics.fieldStore`.
- Parcelas con el ciclo del huerto (`grass` → `dug` → `sown` → `sprout` → `grown` → cosechada, vuelve a `dug`). Crecen solo húmedas (riego del
  trabajador o lluvia de la estación) y al ritmo de `SeasonClock::GrowthRate`. **La helada del invierno arruina** lo sembrado, brotado o maduro que
  quede en el campo.
- Trabajos `farm till|sow|tend|harvest` (hasta `plotsPerTrip` parcelas por viaje, `workSeconds` cada una). Labrar pide la azada (se coge del almacén
  y se queda con el agricultor); sembrar coge semillas del almacén y solo en su estación; la cosecha va a la mochila y se deja en el almacén del campo.
- El huerto del jugador sigue siendo de entidades con habilidades (`GardenSystem`); el campo del clan son datos (no cuesta nada por fotograma) con
  las mismas reglas de estación y humedad.

## Logística (E11)

- **Capacidad de carga**: un miembro lleva `DepositAt` (4) cosas más `logistics.perBagSlot` por casilla que añade su contenedor (una cesta, +6)
  antes de ir a dejarlas. Las cestas se piden en el plan y se reparten en el campamento.
- **Varios almacenes**: el principal (el del campamento) y los secundarios (`Tribe::AddStockpile`, `economy stockpile`, el del campo). Cada tipo
  (`logistics.stores`) tiene su tope por objeto (`cap`, el del principal sustituye a `TribeConfig::StockCap`), sus casillas, su `spoilRate`, qué
  acepta (`accepts`: etiquetas) y si se acarrea (`haul`). Un miembro deja lo que lleva en el almacén más cercano que lo acepte (uno secundario debe estar
  claramente más cerca que el campamento).
- **Acarreo**: un almacén secundario con `haulAt` cosas o más publica `haul <nombre>`; quien lo reclama va con la mochila vacía, carga hasta su
  capacidad y lo lleva al principal.
- **Capacidad del clan**: `economy status` dice cuánta comida entró hoy y ayer, para cuántos miembros, y cuánto cargan en total por viaje.

## Datos

Sección `economy` de `assets/data/era1.json` (`DefaultEconomyRules()` dice lo mismo; el test `economy` lo comprueba):

```json
"economy": {
  "production": { "depth": 3, "maxBatches": 3, "seconds": 4.0, "priority": 1.1, "stock": { "cordage": 2 }, "campStations": [ "workbench" ],
                  "upgrade": true, "experiment": true, "containersPerMember": 1.0, "preserveAbove": 1.0 },
  "spoilage": { "step": 10.0, "bag": 1.0, "seasons": { "winter": 0.5 } },
  "farming": { "tech": "tillage", "tool": "Item.Tool.Hoe", "plots": 6, "columns": 3, "spacing": 2.0, "offset": [ 9.0, 4.0 ],
               "moistureSeconds": 120.0, "workSeconds": 2.0, "plotsPerTrip": 3, "priority": 1.2, "fields": 1 },
  "crops": [],
  "logistics": { "stores": { "main": { "cap": 40, "slots": 40, "spoilRate": 1.0, "accepts": [], "haul": false },
                             "stockpile": { "cap": 20, "slots": 12, "spoilRate": 1.0, "accepts": [], "haul": true } },
                 "perBagSlot": 1.0, "priority": 0.9, "haulAt": 1, "fieldStore": "stockpile" }
}
```

Cada parte sustituye a la suya entera (lo que no dice es el valor por defecto; un mapa o una lista que da sustituye al de por defecto); `crops` suma por
id. Un error rechaza el documento entero (`AddEconomyContentSection` la registra en los paquetes). Los objetos que nombra no se comprueban en la carga
(los del mismo paquete aún no están confirmados): `CheckEconomy` lo hace después (lo usa `economy status` y el test).

## Guardado

- Los montones guardan su edad: en el inventario del almacén como tercer número (`["berry", 5, 321.5]`) y en la mochila de los actores en el hueco
  `inventory[].age` que reservó H4 (`ActorWorld` ya lo rellena). Solo se escribe si no es 0: lo que no caduca guarda los mismos bytes que antes.
- La sección `ai` de la tribu lleva `economy`: campos con sus parcelas, almacenes secundarios con su inventario, temporizador de caducidad y contadores.
- Una partida de antes carga sin campos ni almacenes secundarios y con todo fresco. No cambia `kFormatVersion`.

## Editor

```
economy status                               almacenes (con lo que le queda a lo perecedero), comida del día, hecho, estropeado, cosechado, acarreado y el plan
economy plan                                 el plan de producción de ahora
economy give <objeto> [n]                    pone algo en el almacén principal
economy stockpile <nombre> [x z] [tipo]      pone un almacén secundario
economy demand <origen> <objeto> <n>         un deseo de fuera (0 lo quita)
field status                                 los campos, sus parcelas y qué permite la estación
field mark [x z]                             marca un campo a mano
```

El panel Tribe tiene la sección **Economy and fields** con el mismo texto.

## Test

`tests/economy_test.cpp` (test `economy`): la sección de `era1.json` dice lo mismo que el código, va y vuelve, se rechaza entera con un error y
`era1_ampliado` añade el grano; los paquetes cargan en modo estricto con tecnologías y la sección; el planificador hace intermedios, los pide para
probar recetas que no se saben, da la escasez bruta y dice qué falta; la caducidad (vida, media ponderada, transferencia, más viejo primero, guardado
de la edad, partida antigua fresca, `spoilsTo`); en juego, el clan hace cordel, descubre el tallado de sílex y el leñador hace su hacha (dos
ejecuciones idénticas), un clan que sabe tallar sílex mejora el hacha del leñador, las bayas viejas se estropean, el excedente se seca solo con
secadero, un campo completa su ciclo en una estación (marcar, labrar, sembrar, brotar, cosechar, acarrear) y la helada lo arruina, el acarreo entre
dos almacenes, la cesta amplía la carga, y el guardado (con edades; una partida anterior carga).

## Límites y pendiente

- El validador de alcanzabilidad no lee los cultivos: el grano sigue como nota diferida del paquete.
- Mercados, precios, moneda e impacto ambiental (el resto de H18) no están.
- Los campos no se dibujan en la escena ni eligen sitio (lo hará H8 con su elección de lugar); el almacén secundario tampoco tiene modelo.
- Sal, ahumado y almacenes fríos: basta con datos (otra receta, otro tipo de almacén con `spoilRate` menor), pero no están en el contenido.
- Los nacimientos (H9) cuentan la comida del almacén principal, no la de los secundarios.
