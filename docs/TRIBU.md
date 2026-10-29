# La primera tribu (H2 + H3)

`GB/Game/Tribe.h`: un puñado de trabajadores que, sin que nadie los dirija, recogen recursos, los llevan a un almacén común, fabrican sus herramientas y
encienden una hoguera. Es la sociedad más sencilla posible: reglas sobre las mismas habilidades, objetos y nodos de recurso que usa el jugador
([OBJETOS.md](OBJETOS.md)), así que lo que hace un trabajador es lo que podría hacer el jugador. Es determinista (los empates se rompen por nombre, sin azar).

## Roles

| Rol | Qué hace |
|---|---|
| `forager` (recolector) | Plantas (bayas, fibra), juncos y arcilla con las manos |
| `woodcutter` (leñador) | Talar árboles (de cualquier tamaño) y recoger ramas; necesita un hacha; enciende la hoguera |
| `miner` (minero) | Picar roca, sílex y cobre; necesita un pico |

**Los roles son datos**: la sección `roles` de `assets/data/era1.json` (`TribeRole`; `content load` también la lee). Cada rol da un peso a cada opción y dice su herramienta:

```json
"miner": { "weights": { "Resource.Stone": 1.0, "Resource.Loose": 0.5, "fire": 0.0 }, "default": 0.3, "tool": "stone_pickaxe", "toolTag": "Item.Tool.Pickaxe" }
```

El peso de recoger de un nodo se busca por su id (`fiber_grass`), luego por su tipo del más concreto al más general (`Resource.Stone.Flint`, `Resource.Stone`) y si no, `default`
(0,3: cualquiera ayuda). Así un nodo nuevo de un tipo conocido ya se trabaja, y un rol nuevo no necesita código. `tool` es el objeto que el rol fabrica y lleva; `toolTag`, la
etiqueta que cuenta como tenerlo (un hacha de sílex es un `Item.Tool.Axe`). Opcionales: `nav` (cómo camina el rol y cuánto le pesa la carga, ver [NAVEGACION.md](NAVEGACION.md)),
`agenda` y `nightShift` (su propia agenda del día, ver [IA.md](IA.md)).

Cada rol es además un **oficio** (H7): `skill` dice la familia de aptitud que representa (`foraging`, `woodcutting`, `mining`) y `fixed: true` lo deja fuera del reparto
automático (una guardia o un líder puestos a mano). Ver «Aptitudes, oficios y tablón» más abajo.

La sección `tribe` del mismo archivo dice la agenda de la tribu, qué es comida (`food`: `Item.Food`, salvo `drink`: `Item.Food.Water`) y cuánto quiere el almacén de cada objeto
(`stock`: bayas 6 o 3 por miembro, madera 10, piedra 8, el resto 12). Ningún id de objeto de comida o de existencias queda en el código de la tribu.

Los trabajadores ya no siguen un if/else por rol: cada uno es un **agente** ([IA.md](IA.md)) que ve y oye, recuerda lo que ha visto (y lo comparte con el clan en el campamento), tiene
necesidades (hambre, sed, descanso, abrigo, seguridad, compañía) y, cuando queda libre, puntúa lo que puede hacer (comer, beber, dormir, calentarse, fabricar su herramienta, recoger,
llevar al almacén, encender o avivar la hoguera, explorar, descansar) y elige. **El rol es un conjunto de pesos** sobre esas opciones: el recolector prefiere bayas y fibra, el leñador árboles y la
hoguera, el minero rocas; todos ayudan con lo que falta. El hambre sustituye al antiguo temporizador de 40 s: un trabajador come una baya (primero la que lleva, luego la del almacén)
cuando la necesidad aprieta más que su trabajo, y la hoguera quema el calor de un trozo de madera cada 30 s (si nadie la aviva se apaga y vuelve a arder cuando la avivan): así hay siempre algo que
reponer y nadie se queda sin trabajo para siempre. Cada uno espera y duerme en su propio sitio alrededor del campamento.

**La hoguera es el interactivo `Fire Pit`** ([INTERACTIVOS.md](INTERACTIVOS.md)). Al encenderla (el kit de 4 madera y 3 piedra) la tribu construye en el campamento
un `fire_pit` de los datos con el nombre `Campfire` (`TribeConfig::FireName`, `FireDef`), encendido con `FireHeat` de combustible y con un atributo `BurnSeconds` =
`FireSeconds / FireHeat` (15 s por unidad: el mismo ritmo de antes). Lo quema el `InteractiveSystem` del anfitrión, no la tribu. Cuando le quedan `StokeBelow` (2) o menos,
un miembro al que su rol le da peso en `fire` (el leñador) coge del almacén el trozo que más calienta (o usa el que lleva), va a la hoguera y la aviva con **`Stoke`**: la
misma habilidad y la misma regla de combustible (`ItemDef::Fuel`: madera 2, rama 1) que el jugador. El abrigo (`NearFire`) y «la hoguera arde» se leen del estado del
interactivo (`State.Lit`). Una partida de antes (con `fire`, `burning`, `fireTimer` y `fireHeat` en la sección `ai`) construye al cargar la hoguera con el calor que le quedaba.

Un trabajador sin su herramienta va al campamento a hacerla si el almacén tiene los materiales; si no, ayuda recogiendo lo que falta. Lleva lo que recoge al
almacén cuando su mochila llega a 4 unidades, y deja de recoger un recurso cuando el almacén tiene 40. El leñador enciende la hoguera cuando todos tienen su
herramienta y el almacén puede pagarla (la hoguera no se come la piedra que aún necesita el pico). Solo recogen lo que **recuerdan**: al principio miran a su alrededor (parados giran
la cabeza) y, si no conocen lo que su oficio pide, salen a explorar.

**Sumideros.** Lo que sobra se gasta: el campamento usa una unidad de fibra por miembro cada 60 s (solo la que pasa de 12) y la comida se estropea por su vida útil (H18, E6: un montón de bayas que nadie come se pierde entero al día y medio, ver [ECONOMIA.md](ECONOMIA.md)), además de la madera de la hoguera y las bayas que se comen. Así
el almacén no se llena para siempre. **Quien no tiene nada que hacer** descansa mirando alrededor, o explora si no conoce lo que su oficio pide, y el panel dice por qué (`every tree is empty: it waits for them to regrow or looks for more`...).

## Aptitudes, oficios y tablón de trabajos (H7: E3 y E8)

Código en `GB/Game/Skills.h` (aptitudes), `GB/Game/JobBoard.h` (tablón y reglas) y `GB/Game/TribeWork.cpp` (lo que la tribu hace con ellos); `Tribe.cpp` solo los llama.
Todo es dato: la sección **`work`** de `assets/data/era1.json` (`WorkRules`, `LoadWorkRules`, registrada en los paquetes como `work`; `DefaultWorkRules()` dice lo mismo).

**Aptitudes por práctica.** Cada miembro tiene un nivel de 0 a 100 por **familia** de trabajo (`TribeMember::Skills`). La familia de una actividad se busca como el peso de un
rol: por id del nodo, por su tipo del más concreto al más general y por nombre de opción (`families`: `Resource.Plant` y `Resource.Loose` → `foraging`, `Resource.Wood` y
`Resource.Plant.Branch` → `woodcutting`, `Resource.Stone` → `mining`, `craft` → `crafting`, `fire` y `stoke` → `firekeeping`).

- **Sube con la práctica**: cada unidad recogida (y cada herramienta hecha, cada vez que aviva o enciende la hoguera, con su peso en `practice`) da `gain` (0,6) × (1 − nivel/100):
  rendimientos decrecientes. El oficio del miembro (`TribeRole::Skill`) multiplica la ganancia por `profession` (1,5): aptitud por agente, potenciada por el oficio
  (decisión 4 de la propuesta). El registro dice cuándo alguien pasa una decena (`Biel (woodcutter) is better at woodcutting (10)`).
- **Baja despacio** sin práctica: `decayPerDay` puntos por día simulado (4); un nivel que llega a 0 desaparece.
- **Efecto modesto**: con el nivel al máximo, `yield` (+50 %) más golpes por viaje y `speed` (+40 %) menos espera entre golpes (el `wait` de las órdenes baja de 1 s); con
  nivel 0 las órdenes son las de siempre. Y **gusta más lo que se hace bien**: el peso de una opción es el del rol más `weight` (0,6) × nivel/100 de su familia
  (`Tribe::MemberWeight`). Así un recolector que ha talado mucho acaba prefiriendo talar, y un miembro con práctica en `firekeeping` aviva la hoguera aunque su rol no lo pida.

**Oficios derivados.** Los roles de datos siguen siendo los **perfiles** de oficio (pesos, herramienta, navegación, agenda). Quién tiene cada uno ya no es fijo:

- `tribe build` reparte los oficios por las **necesidades del clan** (`Tribe::PlanRoles`): cada objeto que dan los nodos se quiere en su existencia objetivo (sección `tribe`) y
  esa demanda va al rol con más peso para alguno de sus nodos; la hoguera suma `fireDemand` (6) al rol que más la quiere; los miembros se reparten de uno en uno al rol con más
  demanda por miembro. Con era 1: 4 miembros son recolector, minero, leñador y otro recolector; 20 son sobre todo recolectores (la comida se pide por boca) con al menos tres leñadores y tres mineros. Un rol `fixed` nunca se reparte.
- **Revisión de oficios** cada `review` segundos (900, unos tres cuartos de día): con las necesidades de ahora (lo que falta en el almacén pesa más) se calcula el reparto ideal; si un
  oficio tiene menos manos que las que pide, un miembro de un oficio con de más lo toma (el que mejor lo hace, con su aptitud en su propio oficio en contra: `inertia`); si no, un
  miembro que es `skillSwitch` (25) puntos mejor en otro oficio que en el suyo se cambia. Como mucho un cambio por revisión, nadie toma un oficio cuya herramienta no puede tener,
  un clan de menos de `minClan` (3) no se especializa y los roles `fixed` no se tocan. El registro lo dice (`takes up the trade of miner (the clan needs more hands there: 1 of 2)`).

**Tablón de trabajos del clan** (`JobBoard`). Cada `every` segundos (5) el clan publica lo que necesita:

| Trabajo | Cuándo | Plazas |
|---|---|---|
| `gather <objeto>` | el almacén más lo que llevan tiene menos de lo que quiere: la existencia objetivo, más el mantenimiento del campamento (`upkeep`: 1 fibra por miembro) y, con hoguera, `fireWood` (4) del mejor combustible | una por cada `slotUnits` (6) que faltan, hasta `maxSlots` (3) |
| `craft <herramienta>` | algún miembro no tiene la herramienta de su oficio | tantas como miembros sin ella |
| `fire Campfire` | no hay hoguera, las herramientas están hechas y el almacén la puede pagar | 1 |
| `stoke Campfire` | a la hoguera le queda `StokeBelow` o menos y hay leña | 1 |
| `expedition <objeto>` (H12) | un `gather` lleva `scarcity` segundos sin que nadie conozca un nodo de su objeto ([EXPLORACION.md](EXPLORACION.md)) | `maxParty` mientras se forma; luego, la partida |

La prioridad sale de la necesidad (`priority` por tipo: recoger 1 × lo que falta, fabricar 1,4, avivar 1,6, encender 1,2). **Cada miembro, al decidir, reclama** el trabajo que más
le vale: prioridad × su gusto por ese trabajo (rol y aptitud) × cercanía × `Vigor`. Solo se ofrece a quien **sabe dónde hacerlo** (recuerda un nodo de ese objeto y tiene la
herramienta que pide), una herramienta solo a quien le falta y puede fabricarla, la hoguera solo a quien le gusta. Con un trabajo reclamado, las opciones que lo hacen valen
`bonus` más (×1,6, factor `job` en el inspector): **no sustituye al cerebro de utilidad**, así que el hambre, la sed y el sueño siguen mandando y la histéresis del selector sigue igual.
Contra atascos: un miembro solo cambia de trabajo si otro le vale `switch` (0,3) más; una reclamación sin avances (entregas, golpes, la herramienta hecha) en `claimSeconds` (240) caduca;
un trabajo sin nadie sube de prioridad con la espera (`ageBoost` cada `ageSeconds`, hasta `maxAge`); y uno cubierto desaparece con sus reclamaciones.

**Guardado.** Las aptitudes, el tablón (trabajos, reclamaciones, edades) y los relojes del tablón y de la revisión van en la sección `ai` (`skills`, `board`, `boardTimer`,
`reviewTimer`). Una partida de antes carga con las aptitudes a 0 y el tablón vacío.

## Exploración, expediciones y migración (H12)

Con un mundo generado la tribu recibe sus **regiones** (`Tribe::SetRegions`; el editor lo hace solo) y entonces: los miembros cuentan en el campamento qué parte de cada región
han visto y qué nodos hay (el **atlas** del clan); cuando el tablón pide un objeto que nadie sabe dónde encontrar, el clan publica una **expedición** a la región más prometedora
(probabilidad por los biomas de los datos, viaje y peligro) que reclaman dos o tres miembros que saben recogerlo, y que sale con algo de comida, recorre la región y vuelve a
contarlo; y si el hogar se queda sin comida, sin agua o muy por debajo de lo que fue durante mucho tiempo, el clan **se muda** al mejor sitio que conoce (el almacén y la hoguera van
con el campamento, cada miembro camina hasta allí). El elector de sitios es intercambiable (`SetSiteChooser`) para que H8 ponga el suyo. Código en `GB/Game/TribeExplore.cpp`
(los enganches en `Tribe.cpp` son una línea cada uno), reglas en la sección `exploration` y estado en la sección `ai` (`explore`). Todo en [EXPLORACION.md](EXPLORACION.md).

## Sociedad: vida, herencia, relaciones y cultura (H9 + H10)

Diseño completo en [SOCIEDAD.md](SOCIEDAD.md); código en `GB/Game/Society.h` (estado y reglas, sección **`society`** de `era1.json`) y `GB/Game/TribeSociety.cpp` (los ganchos).
Cada miembro tiene **edad** (un año son `daysPerYear` días), **etapa** (niño, adulto, mayor), sexo, padres y pareja, **genes** (fuerza, resistencia, percepción, destreza,
longevidad) y **personalidad** (sociabilidad, diligencia, curiosidad, audacia). Cada día la sociedad envejece a todos, los mayores pueden morir de vejez, se forman
parejas por afinidad y, con comida de sobra y hoguera, nacen hijos: **un cuerpo nuevo** que la tribu pide a su `spawner` (el editor genera un actor del modelo de la tribu, a
escala de niño). Los genes y la personalidad, la etapa (un niño no hace trabajo pesado y aprende más rápido; un mayor enseña mejor y trabaja más despacio) y la cultura del clan
(normas que derivan con la personalidad de los adultos y tradiciones nacidas de eventos repetidos) son factores sobre las opciones del cerebro (`nature`, `life`, `culture`).
Los **vínculos** entre pares suben y bajan con el trabajo compartido, la comida que otro trajo, el campamento, las lecciones y las disputas por un nodo, y deciden la pareja,
el compañero que se busca y quién enseña a quién. Las aptitudes no se heredan, pero sí lo rápido que se aprenden.

## El jugador (H15)

Un miembro puede ser **el jugador** (`TribeMember::Player`, `Tribe::AddPlayer`): el clan lo cuenta como a cualquiera, pero el cerebro no decide por
él; lo que hace llega por `Tribe::Player*` (`TribePlayer.cpp`) y suma **posición** en el clan, que hace que el clan acepte sus propuestas. Todo en
[JUGAR.md](JUGAR.md).

## Varios clanes (H13)

Cada clan del mundo es una tribu; la de `tribe build` es el clan 0 y `clan new` hace las demás, con su almacén, su hoguera (`Campfire <clan>`), su pozo y
sus miembros. Los enganches con el mundo de clanes son una línea cada uno (`TribeOutside`: la opción `envoy` de los enviados y el aviso de cada
recogida) y el código está en `GB/Game/Clans.cpp` y `TribeClans.cpp`. Todo en [CLANES.md](CLANES.md).

## En el editor

```
tribe build [modelo.fbx|fox] [x z] [trabajadores]       crea el campamento y los trabajadores (nombres de los datos, oficios por las necesidades, sin tope de 6); sin x z, en un mundo
                                                        generado, en el mejor sitio (`site best`); el parche de recursos solo si no hay 8 nodos `wild` a 40 m
settlement status | start <edificio> [x z]              obras y edificios del clan (H8, ver ASENTAMIENTOS.md)
tribe status                                            almacén y qué hace cada uno (con sus aptitudes y su trabajo del tablón)
tribe jobs                                              el tablón: cada trabajo con su prioridad, plazas y quién lo tiene; y qué tiene cada miembro
tribe log [n]                                           las últimas n líneas de lo que ha pasado (el registro guarda las últimas 800)
tribe remove <nombre>                                   saca a un miembro de la tribu (el actor sigue)
tribe family|relations <nombre>                         su familia, genes y personalidad / sus vínculos (H9, H10; ver SOCIEDAD.md)
tribe culture | relate <a> <b> <evento> | day           normas y tradiciones del clan / un evento entre dos / un día de la sociedad ahora
ai debug|status|memory|scream                           el cerebro de los miembros (ver IA.md)
timescale <1-20>                                        acelera la simulación (el día de la tribu en un minuto)
```

Los trabajadores son actores normales (`actor list`, `actor bag <nombre>`): se guardan con la partida, y el estado de la tribu (almacén, hoguera, memoria y decisión de cada uno, sus
necesidades) va en la sección `ai` del guardado: al cargar siguen donde estaban. `tribe build` pone también un pozo cerca del campamento. Una partida guardada con un trabajador a medias de una habilidad vuelve con la habilidad cancelada y sin `State.Busy` (antes la tribu entera quedaba bloqueada tras cargar).

## Panel Tribe y selección

- Panel **Tribe** (se pone al frente al hacer `tribe build`; View > Tribe): tiempo, estado de la hoguera, almacén con barras, el **tablón** (trabajos por prioridad, plazas, quién los tiene
  y por qué se publicaron), tabla de miembros (qué hacen, qué llevan y su trabajo bajo el oficio), las **aptitudes** del seleccionado con barras (la de su oficio marcada), el
  miembro seleccionado con su inventario, sus órdenes, sus necesidades, **qué decidió y por qué** (las opciones con su puntuación) y lo que sabe, y el registro de lo ocurrido. Clic en un nombre lo selecciona; doble clic va hasta él.
- **Clic en un personaje en el viewport** lo selecciona: el panel *Abilities* muestra el nombre del personaje, su rol y su inventario. Clic en una parcela, un árbol o un
  arbusto muestra sus habilidades y atributos.
- Por qué se paran: cuando el almacén llega a 40 de un recurso nadie recoge más de él (`StockCap`), y si no queda ningún nodo de lo que toca (las rocas no se regeneran)
  descansan. El panel y `tribe status` dicen cuál es el motivo.

## Test

`tests/sim_sweep_test.cpp` (test `sim_sweep`, H20): la tribu entera vive días sobre mundos generados de varias semillas y biomas, con la navegación real, comprobada en cada paso contra la tabla de invariantes de `GB/Game/SimQA.h` (ver [QA_SIMULACION.md](QA_SIMULACION.md)); en el editor, `qa sim` y el panel QA.

`tests/work_test.cpp` (test `work`, H7): la sección `work` de `era1.json` dice lo mismo que el código, va y vuelve, y un error la deja intacta (también dentro de un paquete); las aptitudes
suben con rendimientos decrecientes, bajan despacio y el oficio las potencia; el tablón publica, ordena, llena plazas, envejece lo que nadie coge, deja caducar reclamaciones sin avances y
va y vuelve en JSON; el reparto de oficios es determinista y cubre los tres oficios con 4 y con 20; un clan de cuatro sube sus aptitudes en 10 minutos (alguno es el mejor en su propio
oficio), publica y reclama trabajos sin pasarse de plazas, trabaja más rápido con práctica y sigue encendiendo la hoguera; se guarda y se carga igual (y una partida sin aptitudes ni
tablón carga); dos ejecuciones acaban idénticas; la revisión mueve una mano al oficio que falta (la que ya sabe), un rol fijo no se mueve y un clan de dos no se especializa; y un clan de
20 vive 15 minutos con los tres oficios y muchos miembros cogiendo trabajos.

`tests/tribe_test.cpp` ejecuta cuatro trabajadores con cuerpos de prueba que interpretan el mismo texto de tareas que un actor real durante 10 minutos de
simulación: se hacen las dos herramientas y la hoguera (que es el interactivo y arde al ritmo de la tribu), alguien la aviva con `Stoke`, ningún contador baja de cero
y dos ejecuciones acaban idénticas; una partida antigua con la hoguera ardiendo (o apagada) carga como el interactivo con el calor que le quedaba. El cerebro tiene los suyos (`perception`, `ai`, ver [IA.md](IA.md)).

## Límites conocidos

- Con los recursos agotados (las rocas no se regeneran) el minero espera en el campamento y, si no conoce ninguna roca, sale a explorar; con regiones (H12) el clan manda una
  expedición a buscarlas y, si el hogar entero se agota, se muda ([EXPLORACION.md](EXPLORACION.md)).
- Los oficios cambian (revisión, H7) pero las decisiones colectivas son simples: no hay líder ni votación, y la herramienta de cada oficio sigue ligada al rol.
- El tablón publica las obras (`deliver` y `build`, H8: [ASENTAMIENTOS.md](ASENTAMIENTOS.md)) pero no las propuestas del jugador (H15); la fatiga no frena la práctica. Las aptitudes no se heredan (H9), solo lo rápido que se aprenden.
- La aceptación completa de H7 (un clan de 20 que se organiza solo durante días y cambia de reparto cuando cambia la necesidad) solo se prueba 15 minutos en CI; un barrido de
  semillas de la simulación (H20) diría si las curvas de práctica y la revisión están bien equilibradas.
- Hay almacén principal y almacenes secundarios con acarreo (H18, [ECONOMIA.md](ECONOMIA.md)), pero no caminos (la navegación es otra parte de H5).

## Economía (H18)

Cadenas de producción (`make`), caducidad y conservación, campos (`farm`), almacenes secundarios y acarreo (`haul`), cestas que amplían la carga: código en `GB/Game/TribeEconomy.cpp`, reglas en la sección `economy`. Todo en [ECONOMIA.md](ECONOMIA.md).
- Más límites de percepción, necesidades y decisión en [IA.md](IA.md).

## Conflicto (H14)

Amenazas vistas y oídas, luchar o huir, heridos que descansan y se cuidan, caza (`hunt` en el tablón), incursiones y agravios: código en `GB/Game/TribeConflict.cpp` (los ganchos en `Tribe.cpp` son una línea cada uno), núcleo en `GB/Gameplay/Combat.h`, reglas en la sección `combat`. Todo en [CONFLICTO.md](CONFLICTO.md).
