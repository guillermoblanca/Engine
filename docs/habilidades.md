# Habilidades de juego (Gameplay Ability System)

Módulo `Engine/src/GB/Gameplay/`, espacio de nombres `GB`. Es un GAS pequeño al estilo de Unreal: etiquetas, atributos, efectos y habilidades
que mueven y animan al personaje reutilizando lo que ya había (`InteractionRunner`, `MotionWarp`, montajes y sockets). Es CPU puro (sin GL),
determinista y se ejecuta sin ventana; sus tests están en `tests/gameplay_test.cpp`.

## Piezas

| Pieza | Fichero | Qué es |
|---|---|---|
| `GameplayTag`, `GameplayTagContainer` | `GameplayTag.h` | Etiquetas jerárquicas con puntos (`Ability.Plant`, `State.Carrying.Box`). Preguntar por `State.Carrying` es cierto si se tiene `State.Carrying.Box` (coincidencia por prefijo, cortada en el punto). El contenedor cuenta cuántos efectos conceden cada etiqueta |
| `AttributeSet` | `AttributeSet.h` | Números con base, mínimo y máximo (energía, agua, semillas, fruta...). Los efectos instantáneos cambian la base; los que duran añaden modificadores (`Add`, `Multiply`, `Override`) que se quitan al acabar |
| `GameplayEffectDef` | `GameplayEffect.h` | `Instant`, `Duration` o `Infinite`; con `Period` aplica sus modificadores cada periodo (fruta que rebrota, energía que se recupera). Modifica atributos y concede o quita etiquetas. El **coste** de una habilidad es un efecto instantáneo y el **enfriamiento** uno con duración que concede `Cooldown.Ability.<nombre>` |
| `AbilityDef` | `GameplayAbility.h` | Datos de la habilidad: etiquetas requeridas y bloqueantes (del que actúa y del objetivo), atributos mínimos del objetivo, coste, enfriamiento, montajes, eventos ligados a las notificaciones del montaje, props que empuña |
| `GameplayLibrary` | `GameplayAbility.h` | Efectos y habilidades por nombre; las entidades se refieren a ellos por nombre y la escena queda pequeña |
| `AbilitySystemComponent` | `AbilitySystemComponent.h` | Etiquetas, atributos, efectos y habilidades de **una** entidad (jugador, NPC, parcela, árbol...): `Grant`, `CanActivate`, `Activate`, `Cancel`, `ApplyEffect`, `Update`, eventos y `ToComponent`/`FromComponent` |
| Tareas | `AbilityTasks.h` | `MoveToTargetTask` (usa `InteractionRunner` y `MotionWarp`), `PlayMontageAndWaitTask`, `WaitGameplayEventTask` |
| `GameplayWorld` | `GameplayWorld.h` | Un sistema por entidad con componente `Gameplay`; `Load`/`Store` con el `EntityWorld` |
| Huerto | `Garden.h` | `RegisterGardenContent`, `BuildGarden` y `GardenSystem` (las reglas) |

## Cómo corre una habilidad

1. `CanActivate`: concedida, no en marcha, etiquetas requeridas y bloqueantes, no en enfriamiento, coste asumible y objetivo válido (etiquetas y
   atributos). Si falla, `why` dice qué falta ("needs State.Soil.Dug", "not enough Energy"...).
2. `Activate`: concede las etiquetas de la habilidad (`Ability.X` y las `ActiveTags`, p. ej. `State.Busy`), empuña el prop (`EquipProp`) y arma las tareas.
   Con personaje, la habilidad **confirma** (paga el coste, arranca el enfriamiento, aplica los efectos de activación) al llegar al objetivo; sin
   personaje, al activarla.
3. Tareas, en orden: `MoveToTarget` (anda al punto de interacción del objetivo y se coloca con motion warp) → `PlayMontageAndWait` (reproduce el
   montaje; sus **notificaciones** disparan los eventos de la habilidad) → `WaitGameplayEvent` si la habilidad espera una respuesta.
4. Un evento (`AbilityEventDef`) aplica efectos a quien actúa y al objetivo y se envía a ambos; los *listeners* del objetivo reaccionan (la
   parcela se cava, el árbol suelta un fruto). Si el montaje se interrumpe o se cancela antes de la notificación, el evento **no** ocurre.
5. Al acabar se devuelven las etiquetas, se suelta el prop y, si fue bien, se aplican los `EndEffects`.

Sin montaje en el personaje (o sin personaje) la misma habilidad corre con temporizadores a las mismas fracciones: así los tests de CPU son
deterministas y el resultado es igual. Las notificaciones de los montajes del Mixamo salen de las habilidades: `RegisterAbilityMontages`
(`MixamoMontages()` más los tiempos de `AbilityDef::Events`).

Quien maneja al personaje (teclado, IA o lista de tareas) no importa: `AbilitySystemComponent::Controlling()` dice que una habilidad lo mueve y
`Input()` es el `CharacterInput` de ese fotograma. El orden por fotograma es `PrepareAbilities` → `GameplayWorld::Update` → controlador → animación.

## Escena y JSON

Cada entidad puede llevar un `GameplayComponent` (etiquetas, atributos con rango, habilidades concedidas y efectos infinitos) que `SceneIO`
guarda en la sección `"gameplay"`. `GameplayWorld::Load(escena)` crea los sistemas y `Store(escena)` escribe el estado vivo. No se guardan los
temporizadores de los efectos con duración.

## El huerto

`garden build [x z]` (o `BuildGarden`) coloca bajo la entidad `Garden`: 6 parcelas (`Plot 1..6`), un `Tree Spot` vacío, un `Fruit Tree`, una `Cow`,
un `Compost`, 3 cajas, una carretilla, semillero, regadera y pala, todo con primitivas del motor, y las entidades `Gardener` (el jugador) y
`Farmhand` (un NPC) con sus habilidades. Parcelas y árboles tienen su propio sistema de habilidades:

| Estado de la parcela | Etiqueta |
|---|---|
| hierba | `State.Soil.Grass` |
| cavada | `State.Soil.Dug` |
| sembrada / brote / crecida | `State.Soil.Planted.Sown` / `.Sprout` / `.Grown` (`State.Soil.Planted` las cubre a las tres) |

`GardenSystem::Update` hace crecer lo que está húmedo (`Moisture` > 0 → `Growth` sube; 4 s por fase, la humedad dura 10 s; un `Tree Spot` con sapling tarda
6 s). Un árbol adulto rebrota fruta con un efecto periódico (`FruitRegrow`, cada 8 s, máximo 3) y la vaca leche (`MilkRegrow`).

| Habilidad | Montaje | Objetivo (etiquetas) | Efecto |
|---|---|---|---|
| `Dig` | `dig_plant` | `State.Soil.Grass` | cava (coste 5 de energía) |
| `Sow` | `plant` | `State.Soil.Dug` | siembra (energía 2, semilla 1) |
| `Water` | `water` | `State.Soil.Planted` o `State.Tree.Sapling` | humedad +1 (agua 1, energía 1) |
| `PlantTree` | `plant_tree` | `State.Tree.Empty` | planta el sapling, espera `Event.Tree.Rooted` (sapling 1, energía 8) |
| `PickFruit` | `pick_fruit`, `_2`, `_3` en turno | `State.Tree.Grown` con `Fruit >= 1` | fruta +1 |
| `Pull` | `pull_plant`, `_2` en turno | `State.Soil.Planted.Grown` | arranca la planta, cosecha +1 |
| `Milk` | `milk_cow` | `State.Cow` con `Milk >= 1` | leche +1 |
| `CarryBox` / `DropBox` | set `box` | — | efecto infinito que concede `State.Carrying.Box` |
| `PushWheelbarrow` / `ReleaseWheelbarrow` | set `wheelbarrow` | — | `State.Carrying.Wheelbarrow` |
| `DumpWheelbarrow` | `wheelbarrow_dump` | `Garden.Compost`, con la carretilla | cosecha → compost |

Las habilidades de trabajo bloquean con `State.Busy` y `State.Carrying` (no se cava con una caja en brazos). Las acciones que faltan en los packs y qué
se hace en su lugar están en [ANIMACIONES_PENDIENTES.md](ANIMACIONES_PENDIENTES.md).

## Editor

* Panel **Abilities**: habilidades de la entidad elegida (botón Usar/Cancelar, estado y motivo si no está lista), atributos, etiquetas, efectos y enfriamientos, y el estado del huerto.
* `garden build [x z]`, `garden status`.
* `ability list|status [entidad]`, `ability grant <habilidad> [entidad]`, `ability activate <habilidad> [objetivo] [actor]`, `ability cancel [habilidad] [actor]`,
  `ability apply <efecto> [entidad]`, `ability sync` (sistemas desde la escena) y `ability store` (estado a la escena). Los nombres con espacios van entre comillas.
* Listas de tareas (`player_tasks`): `ability <habilidad> [entidad]` (con `_` por espacios). El jugador y un NPC con tareas usan **las mismas** habilidades.

Ejecutable sin ventana, con el pack de `packs/all` (locomoción + granja + heridos en una carpeta):

```bash
Editor --hidden --quality=low --script=huerto.txt
```

```text
generate temperate 5
garden build 0 0
player "D:/GameEngine/packs/all/X Bot.fbx"
ability activate Dig "Plot 1"
wait 900
player_tasks ability Sow Plot_1 | ability Water Plot_1 | wait 1
wait 900
garden status
```
