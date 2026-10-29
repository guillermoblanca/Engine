# Conflicto, combate y violencia (H14)

Peleas entre cualesquiera dos cosas con sistema de habilidades (miembros de un clan, de dos clanes, un zorro y un niño, un cazador y un conejo),
heridas que sangran, frenan y se curan, miedo y valor, caza, incursiones con agravios y la hostilidad entre clanes de la diplomacia (H13).
Lógica de CPU, determinista (cada «dado» es un hash de la semilla, dos nombres y un contador; sin azar global) y casi todo en datos.
Tests: `tests/combat_test.cpp` (test `combat`).

[← Volver al README](../README.md) · Plan: [PLAN_SOCIEDAD_VIVA.md](PLAN_SOCIEDAD_VIVA.md) (H14)

| Pieza | Dónde | Qué es |
|---|---|---|
| `CombatRules`, `LoadCombatRules`, `DefaultCombatRules` | `GB/Gameplay/Combat.h` | Las reglas como datos (sección `combat`) |
| `CombatSystem`, `Combatant`, `Wound`, `CombatEvent` | `GB/Gameplay/Combat.h` | El núcleo: luchadores, golpes, heridas, moral, depredadores; guardado |
| `IHostility`, `HostilityTable` | `GB/Gameplay/Combat.h` | Quién es hostil a quién, a quién se puede asaltar, agravios (tabla simple para tests y editor) |
| `ClanHostility` | `GB/Game/ClanHostility.h` | El adaptador sobre la diplomacia de los clanes (H13) |
| `FightOrFlight`, `WeighRaid`, `ChooseRaidParty`, `ConflictState` | `GB/Game/Conflict.h` | Las reglas puras del clan y su estado guardado |
| Ganchos en `Tribe` | `GB/Game/TribeConflict.cpp` (y una línea cada uno en `Tribe.cpp`, `TribeWork.cpp`) | Amenazas, luchar o huir, órdenes, caza, descanso y cuidados, incursiones |
| Editor | `Editor/src/EditorConflict.cpp` | `combat`, `hostile`, `raid`, ragdoll al morir, sección `combat` del guardado |

## El núcleo: GAS

Es el mismo sistema de habilidades de H1 ([habilidades.md](habilidades.md)):

- **Atributos**: `Health` (la necesidad de `GB/AI/Needs.h`, 0..100; un animal la recibe de su regla) y `Stamina` (0..`maxStamina`; la pagan los golpes y
  los bloqueos y vuelve con el tiempo; un animal que corre en una pelea la gasta y, agotado, corre a media velocidad).
- **Heridas**: tipo (`cut`, `bite`, `bruise`) y gravedad 0..1. Una herida nueva **sangra** `bleedSeconds` (vida por segundo = `bleed` × gravedad, la mitad si
  alguien la cura), **frena** (efectos `Combat.Wounded.Light/Serious/Grave` que escalan `Vigor`, en tres escalones de `slow`) y, cuando deja de sangrar,
  **se cura** `healPerDay` al día (× `restFactor` descansando o durmiendo, × `careFactor` si alguien la cuida). Una gravedad total de `lethal` **mata**
  (`State.Dead`, `Death.Wounds`; `CauseOfDeath` dice `Wounds`).
- **Armas**: datos (`weapons`): una etiqueta de objeto (`Item.Tool.Axe`: el hacha de piedra y la de sílex también son armas; `ItemDef::Power` suma `power`
  de daño), daño, alcance, segundos entre golpes, estamina, probabilidad de herir, tipo de herida y montajes. `fists` y `bite` son naturales.
  `BestWeapon` elige la mejor de la mochila.
- **Habilidades**: `Attack.<arma>` (el montaje del arma; su notificación `hit` lanza `Event.Combat.Hit` y **solo entonces** se resuelve el golpe: un golpe
  cancelado antes no hace nada), `Block` (`State.Blocking` mientras dura), `Flee` (`State.Fleeing` un rato) y `Stagger` (la reacción a un golpe que entra).
  Sin personaje corren con temporizadores a las mismas fracciones.
- **Golpe determinista**: probabilidad = `base` + `dexterity` × (destreza del atacante − la del defensor) − `stamina` × lo gastado, entre `min` y `max`;
  daño = (daño del arma + `power`) × (1 + `strength` × (fuerza − 0,5)) × (1 − `endurance` × (resistencia − 0,5)) × una variación de 0,85 a 1,15; un bloqueo deja
  pasar `blockLets`. El dado es `CombatRoll(semilla, atacante, defensor, n)`. El defensor que ve venir el golpe puede bloquear (`blockChance` × (0,5 + destreza)).
- **Moral**: empieza en `start` + `boldness` × audacia, baja con el daño recibido (`hurt`) y cuando cae alguien de su lado cerca (`allyDown`); por debajo de
  `fleeBelow` (o con menos de `fleeHealth` de vida, salvo los más audaces) huye.

**Quién mueve a quién.** Un luchador `Entity` (un animal, un duelista de test) lo mueve el combate: hacia su enemigo o lejos de él (un animal en una pelea
lleva `State.Held` y la ecología no lo toca). Un luchador externo (un miembro de un clan) lo mueve su dueño con órdenes, que caminan con navegación, y dice
dónde está con `Place`. El combate golpea por los dos cuando el enemigo está a su alcance.

## El clan: amenazas, luchar o huir

Cada miembro está en las peleas con su lado (el nombre del clan), los genes (fuerza, resistencia, destreza; un niño pega según lo que ha crecido) y la
audacia de su personalidad, y la mejor arma de su mochila.

- **Ve** amenazas en su cono de visión (`threats.sight`), y **oye** el grito de alarma de un compañero (`alarm` metros: el `scream` de la IA): pierde
  `safety` de seguridad (la necesidad `Safety`) y lo recuerda.
- **Debe responder** a quien le ataca, al que ataca a un compañero cerca, a un extraño de un clan hostil en el campamento (o muy hostil y cerca); un niño,
  también a un depredador cercano. Entonces decide por **utilidad** (`FightOrFlight`, con la explicación en el registro):
  - luchar = `bold` × audacia + `health` × vida + `odds` × min(fuerzas/2, 1) + `kin` si defiende a su familia o a un niño + `camp` si es en el campamento;
  - huir = `fear` × (1 − seguridad) + `timid` × (1 − audacia) + `odds` × (1 − fuerzas) × 1,5 si está en desventaja + 2 × `health` × (1 − vida) si está muy herido;
  - las **fuerzas** son la suma de `CombatSystem::Strength` de su lado cerca contra la del lado del enemigo; la **cultura** del clan multiplica (la norma
    `daring` tiene efectos `fight` y `flee`); un niño nunca lucha (`childrenFight`).
- **Luchando**, sus órdenes le llevan al alcance de su arma; **huyendo**, a casa si el campamento queda lejos del peligro, si no `fleeDistance` en dirección
  contraria (girando si por ahí no se puede andar), hasta `safeDistance`.
- **Heridos y cuidados**: con más de `restAbove` de gravedad, la opción `recover` (descansar en el campamento); un adulto sano puede ir a **cuidar** a un
  herido que nadie atiende (`care`: más si es pariente o amigo): al acabar, el evento de relación `care` y `careSeconds` × 3 de cura más rápida. Con una
  **choza del curandero** en pie (edificio con `healing`, [PROGRESION.md](PROGRESION.md)) el herido se repone delante de ella y, a su alcance, sus heridas se
  curan `1 + healing` veces más rápido (`Combatant::Heal`).

## Animales

- **Depredadores** (`predators`): un zorro con hambre (`Food` < `hungry`) ataca a un niño (o a un débil, o a cualquiera, según `prey`) **solo** a menos de
  `sight` y si nadie de su lado está a menos de `alone`. El niño huye (el grito avisa a los adultos, que defienden a su familia); el zorro se rinde a
  `giveUp` metros, huye si su moral se rompe y come `meal` si mata.
- **Caza** (`hunts`): con poca comida (menos de tres por miembro) y un animal de la regla a menos de `range` del campamento, el tablón publica `hunt <etiqueta>`
  (`hunters` plazas, prioridad `priority`). El cazador persigue al animal (que no se entera hasta tenerlo a `groupRadius`, y entonces huye y se cansa) y al
  matarlo recibe `units` de `item`: **carne** (`meat`, comida que se estropea en 900 s: la economía la come primero), y `byproductUnits` de `byproduct`: la
  **piel** (`hide`, la del curtido de la progresión). La ecología se lleva el cuerpo y lo cuenta como cazado.

## Más caza (vida social)

`social.json` suma cacerías a la sección `combat` (por id): **ciervo** (4 carne, 2 pieles, 2 huesos, 2 tendones, 1 grasa), **jabalí** (se
revuelve con colmillos: `bite`; 3 carne, 1 piel, 1 hueso, 2 grasa) y **urogallo** (1 carne, 3 plumas). Campos nuevos de `HuntRule`:

- `yields` (objeto → unidades): el **despiece**, además de `item` y `byproduct`. `CheckCombat` comprueba que son objetos.
- `group`: una **cacería en grupo**: nadie sale tras la presa hasta que haya tantos con el trabajo; el primero **ojea** («drives Deer (a group
  hunt)») y los demás **esperan con la lanza** («waits for Deer with the spear»); todos van tras el mismo animal.

Las **creencias** del clan pueden prohibir cazar cerca del lugar sagrado (tabú `sacred_hunt`: ni se publica la cacería ni se elige esa
presa, `Tribe::HuntAllowed`) y la primera pieza de un cazador puede ir entera al almacén (`share_kill`); cada muerte llama a `Tribe::Hunted`
(primera caza → rito; pieza grande → augurio). Ver [VIDA_SOCIAL.md](VIDA_SOCIAL.md).

## Incursiones y agravios

Cada `raids.every` segundos el clan pesa una incursión contra los campamentos que la hostilidad le deja asaltar: motivo = `bold` × audacia media +
`hunger` × lo que le falta de comida + `grievance` × agravio + `hostility` × hostilidad − `defense` × la defensa del objetivo (sus empalizadas: cada clan la
dice al asentarse, `RaidTarget::Defense`). Quien defiende su campamento suma `threats.walls` × esa defensa a luchar. Con `want` o más (y pasado el `cooldown`) sale una partida
(`ChooseRaidParty`: adultos sanos y libres, los más audaces y fuertes primero, de `party` a `maxParty`). Cada asaltante va al almacén, coge `carry` unidades
(comida primero: `steal`) y vuelve a casa; en casa deja lo robado en el almacén. La primera vez que roban, la víctima recibe un **agravio** (`wrong`).
Matar a alguien de otro clan deja el doble. La incursión se retira cuando cae o huye `retreat` de la partida, a los `maxSeconds`, o si un **pacto** la
prohíbe a mitad. El clan robado ve a los extraños en su almacén (`raided` en su registro y en su cultura).

**Hostilidad.** `IHostility` es la interfaz: `Hostility(a, b)`, `RaidTargets`, `Settled`, `Wronged`, `Grievance`.
- `HostilityTable`: pares explícitos (`hostile` en los datos, `hostile a b` en el editor), los depredadores hostiles a todos, y agravios que hacen hostil a la
  víctima (× `GrievanceHostility`) y se olvidan con los días.
- `ClanHostility` (H13): entre clanes manda la diplomacia: `ClanWorld::Hostility` (agresividad × mala voluntad y agravio; 0 con alianza o tregua),
  `MayRaid` (solo si son hostiles y sin pacto), los campamentos y almacenes de sus tribus, `Relate(culpable, víctima, "raid"/"theft", ..., víctima)` para los
  agravios (las reglas de la diplomacia) y `Contested` (cada región que ambos reclaman suma `Contest` al agravio para el motivo). Escucha a la diplomacia
  (`Listen`) y guarda los últimos eventos para el informe. Todo lo demás (depredadores, la tabla) va a la tabla de respaldo.

## Datos: la sección `combat`

En `assets/data/era1.json`, registrada en los paquetes (`CombatContentSection`, `AddCombatContentSection`): atómica; `weapons`, `predators` y `hunts` añaden o
sustituyen entradas enteras por id; `hits`, `injuries`, `morale`, `threats` y `raids` sustituyen esa parte entera (lo que no dice es el valor por defecto);
`hostile` sustituye la lista. `DefaultCombatRules()` dice lo mismo que el archivo (lo comprueba el test). `CheckCombat` comprueba que la carne de una caza es
un objeto. El paquete `era1` declara `meat` como dado (`given`: la caza).

## Guardado

- La tribu guarda su `ConflictState` en su sección (`conflict`: la incursión, lo que sabe cada miembro de las amenazas y los cuidados, contadores).
- El editor guarda la sección `combat`: `{ "fights": CombatSystem::ToJson(), "hostility": HostilityTable::ToJson() }` (luchadores, modos, heridas, moral,
  contadores; la tabla y los agravios). Los golpes en el aire no se guardan (sus habilidades se cancelan al cargar). Todo opcional: una partida de antes carga sin peleas.

## Editor

```
combat status [nombre] | log [n] | hurt <nombre> <vida> [gravedad] | fight <a> <b> | flee <a> <b> | stop <nombre>
hostile [a b [0-1]]           quién es hostil a quién (clanes, Animal.Fox), agravios; con un par, lo fija (0 hace la paz)
raid <clan> | status          manda al clan elegido a asaltar a un clan hostil, o cómo va su incursión
```

El panel **Tribe** tiene la sección **Conflict** (el mismo texto que `raid status`). Un actor que muere en una pelea cae como **ragdoll**
(`Actor::StartRagdoll`); en los tests, el estado (`FightMode::Dead`, `State.Dead`).

## QA de la simulación (H20)

La tabla `SimInvariants()` tiene el invariante `combat`: vida y estamina dentro de su rango y números; los muertos ni luchan ni son atacados; moral y heridas
en rango, nadie vive con heridas letales; y las heridas se curan (su total solo crece con un golpe nuevo). `deaths` acepta la muerte por `Wounds`.

## Animaciones

Montajes reales del pack de espada y escudo (`packs/full`, nombres de `assets/animation/mixamo.manifest.json`; el test comprueba que existen):
`sword_attack_1/3` (puños, interino), `sword_slash_1/3` (hacha), `sword_attack_4` (pico), `sword_attack_2` (lanza), `sword_block_1/2` (bloqueo),
`sword_impact_1/2` y `react_small_front` (reacción). Faltan (ver [ANIMACIONES_PENDIENTES.md](ANIMACIONES_PENDIENTES.md)): puñetazos para X Bot, hachazo,
lanza y maza de verdad, lanzar, esquivar, levantarse del suelo, morder (el zorro es de primitivas), el set `injured` al andar herido (hoy solo frena).

## Límites conocidos

- Sin arco ni armas a distancia (hay clips de arco), sin armaduras ni escudos que reduzcan el daño, sin rendición ni botín de los caídos.
- El set `injured` de locomoción no se activa todavía con las heridas (solo `Vigor`).
- Las batallas abstractas de cientos (resueltas sin cuerpos) y la tasa de guerra por agresividad en un barrido de semillas quedan para después.
- Las incursiones van a por el almacén principal; los almacenes secundarios (H18) y las defensas de los edificios (H8) no cuentan aún.
