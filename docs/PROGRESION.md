# Progresión de la Edad de Piedra: tecnologías y edificios

Diseño y árbol final de lo que un clan de la era 1 puede descubrir y levantar, y de lo que cada cosa le da. Todo es contenido de datos
(`assets/data/era1.json`, `era1_ampliado.json`, `visuals.json`); el código solo sabe de provisiones genéricas (camas, estación, almacén,
abrigo, defensa, curación, cultura) que cualquier edificio puede declarar.

[← Volver al README](../README.md) · relacionado: [CONOCIMIENTO.md](CONOCIMIENTO.md) (cómo se descubre), [ASENTAMIENTOS.md](ASENTAMIENTOS.md) (cómo se construye),
[ECONOMIA.md](ECONOMIA.md) (cadenas, caducidad, almacenes), [CONFLICTO.md](CONFLICTO.md) (incursiones y heridas), [SOCIEDAD.md](SOCIEDAD.md) (compañía y cultura),
[PAQUETES.md](PAQUETES.md) (validador), [VISUALES.md](VISUALES.md) (modelos por fase)

## 1. Qué se quería y de dónde se partía

Antes de este trabajo la era 1 tenía seis tecnologías (piedra, fuego y cordel conocidas; cestería y tallado de sílex por descubrir; fundición de la era 2,
diferida) más dos del catálogo ampliado (hueso y labranza), y cuatro edificios (refugio, almacén, banco y secadero) que ninguna tecnología bloqueaba. El
carril visual ya había dibujado un horno y una empalizada que no existían como edificios.

Objetivos del diseño:

- Un árbol de 15–25 tecnologías en ramas con sentido (herramientas, fuego y cocina, fibra y contenedores, caza y pieles, construcción, comida y almacén,
  hierbas, rito), con requisitos razonables y números de descubrimiento que hagan que un clan de 4–8 encuentre las primeras en los primeros días de
  juego y las últimas a lo largo de semanas (un día de juego = 1200 s).
- Una vista previa de la era 2 (alfarería y horno, cobre con forja) **completa**: la cadena se puede recorrer entera, así que ya no hay nada diferido.
- 10–14 edificios nuevos o existentes con fases, costes, lo que dan y la tecnología que los abre.
- Que todo eso **juegue**: el planificador los pide por necesidades genéricas, la economía los usa, el combate y la sociedad notan sus efectos.

## 2. Reglas de diseño

1. **Lo que se descubre se prueba con materiales comunes.** En el juego un intento de experimentar solo necesita los ingredientes de una receta de la
   tecnología en el almacén o la mochila (no la estación ni la herramienta). Por eso casi todas las tecnologías nuevas desbloquean una receta barata
   (`daub`, `reed_mat`, `stake`, `poultice`...) que el clan prueba con lo que recoge. Las que no tienen nada que sostener en la mano son **ideas**
   (`timber_framing`, `food_storage`, `ritual`): se prueban sin materiales, pero solo cuando no hay ninguna con materiales a mano (ver 6.1).
2. **La estación propia no bloquea el descubrimiento.** Una tecnología cuyo edificio da la estación de su receta (la cocina y el hogar, el ahumado y su
   secadero, el curtido y su bastidor, la alfarería y su horno, la fundición y su forja) se puede descubrir: el validador lo sabe (ver 6.2).
3. **Edificios pagables con lo que hay.** Las fases piden materiales que la tribu recoge (piedra, madera, ramas, juncos, fibra, arcilla) o intermedios
   de una receta que ya sabe (barro, esteras, estacas, cuero, pomada, ladrillo cocido). El planificador cuenta como pagable lo que da un nodo, lo que hay
   en el almacén o lo que hace una receta conocida con su estación.
4. **Provisiones genéricas, no casos especiales.** Ningún edificio tiene código propio: declara números (`shelter`, `rest`, `warmth`, `station`, `store`,
   `storeCap`, `keep`, `defense`, `healing`, `culture`, `aura`, `supersedes`) y los sistemas los leen.
5. **El paquete base juega solo.** `era1` es la progresión completa (es lo que carga el editor y el modo de juego). `era1_ampliado` sigue siendo opcional:
   añade hueso, labranza, grano, tablones, resina, sal, ladrillo de adobe y hacha de cobre encima.

## 3. Árbol de tecnologías (final)

Las conocidas desde el principio están en **negrita**. «Idea» = no desbloquea recetas (se prueba sin materiales). `exp` es la probabilidad de un
intento (crece un 50 % de lo base por cada fallo, `knowledge.growth`), `s` lo que dura un intento.

| Rama | Tecnología | Era | Requiere | Desbloquea | Descubrimiento (exp / observar / enseñar / s) |
|---|---|---:|---|---|---|
| Herramientas | **`stone_tools`** Stone tools | 1 | — | `stone_axe`, `stone_pickaxe` | conocida |
| Herramientas | `flint_knapping` Flint knapping | 1 | cordage, stone_tools | `flint_axe`, `flint_pickaxe` | 0,15 / 0,34 / 0,25 / 20 |
| Caza | `spear_making` Hafted spears | 1 | flint_knapping | `flint_spear` | 0,12 / 0,3 / 0,25 / 20 |
| Caza y pieles | `hide_working` Hide working | 1 | flint_knapping | `leather`, `leather_bag`; edificio `hide_frame` | 0,1 / 0,3 / 0,25 / 25 |
| Fuego | **`fire_making`** Fire making | 1 | — | `campfire` | conocida |
| Fuego y cocina | `cooking` Cooking | 1 | fire_making | `cooked_meat`; edificio `hearth` | 0,2 / 0,34 / 0,3 / 15 |
| Fuego y cocina | `smoking` Smoking | 1 | cooking | `smoked_meat`; edificio `smoking_rack` | 0,1 / 0,3 / 0,25 / 25 |
| Fibra | **`cordage`** Cordage | 1 | — | `cordage` | conocida |
| Fibra y contenedores | `basketry` Basketry | 1 | cordage | `basket_wicker` | 0,2 / 0,34 / 0,25 / 15 |
| Fibra y contenedores | `weaving` Weaving | 1 | basketry | `reed_mat` | 0,15 / 0,3 / 0,25 / 20 |
| Construcción | `wattle_and_daub` Wattle and daub | 1 | basketry | `daub`; edificio `hut` | 0,15 / 0,3 / 0,25 / 20 |
| Construcción | `timber_framing` Timber framing (idea) | 1 | flint_knapping, wattle_and_daub, weaving | edificios `longhouse`, `granary` | 0,04 / 0 / 0,2 / 60 |
| Construcción | `palisade_building` Palisades | 1 | flint_knapping | `stakes`; edificio `palisade` | 0,08 / 0,3 / 0,2 / 30 |
| Comida y almacén | `food_storage` Food storage (idea) | 1 | basketry | edificio `cache_pit` | 0,1 / 0 / 0,25 / 20 |
| Comida y campo | `tillage` Tillage (ampliado) | 1 | flint_knapping | `stone_hoe`, `flint_sickle` | 0,1 / 0,25 / 0,2 / 30 |
| Herramientas | `bone_working` Bone working (ampliado) | 1 | cordage, stone_tools | `bone_pickaxe` | 0,2 / 0,34 / 0,25 / 20 |
| Hierbas | `herb_lore` Herb lore | 1 | — | `poultice`; edificio `healer_hut` | 0,15 / 0,3 / 0,25 / 20 |
| Rito | `ritual` Rites (idea) | 1 | fire_making | edificio `totem` | 0,06 / 0 / 0,3 / 30 |
| Era 2 | `pottery` Pottery | 2 | fire_making, wattle_and_daub | `fired_brick`; edificio `kiln` | 0,05 / 0,2 / 0,15 / 40 |
| Era 2 | `smelting` Smelting | 2 | pottery, flint_knapping | `copper_ingot`; edificio `forge` | 0,05 / 0,2 / 0,1 / 40 |

Veinte tecnologías: dieciséis en `era1` (tres conocidas), dos en `era1_ampliado` y dos de la era 2 en `era1`.

```
stone_tools ─┬─ flint_knapping ─┬─ spear_making
cordage ─────┤                  ├─ hide_working ──────────────── (hide_frame)
             │                  ├─ palisade_building ─────────── (palisade)
             │                  ├─ tillage*
             │                  └─────────────┐
             └─ bone_working*                 │
cordage ── basketry ─┬─ weaving ──────────────┤
                     ├─ wattle_and_daub ──────┴─ timber_framing ─ (longhouse, granary)
                     │        └──────────────── pottery² ── smelting² ── (kiln, forge)
                     └─ food_storage ─────────── (cache_pit)
fire_making ─┬─ cooking ── smoking ─────────── (hearth, smoking_rack)
             └─ ritual ─────────────────────── (totem)
herb_lore ───────────────────────────────────── (healer_hut)
                                     * era1_ampliado   ² era 2
```

**Ritmo esperado.** Con `growth` 0,5 una tecnología de 0,15–0,2 sale en 3–4 intentos y una de 0,04–0,06 en 8–12. Los intentos solo los hacen miembros
desocupados (la opción vale 0,2 por curiosidad, 1,1 si da la herramienta de su oficio) y cada uno espera `s` entre dos, así que en la práctica un clan de
4–8 descubre cestería, sílex, hierbas, barro y esteras en los primeros 1–3 días, cocina en cuanto entra la primera carne, y las ideas lentas
(entramado, rito) y la era 2 a lo largo de semanas. Observar y enseñar las extienden por el clan en horas.

## 4. Edificios (final)

`aura`: metros más allá de la huella a los que llegan sus efectos a cualquiera (abrigo de un hogar, compañía de un tótem, curación de la choza
del curandero). Sin `aura` el abrigo y el descanso solo son para quien duerme dentro (las camas). Los costes son la suma de las fases.

| Edificio | Tecnología | Fases (materiales, trabajo) | Da |
|---|---|---|---|
| `shelter` Shelter (choza de ramas) | — | cimiento (3 piedra, 6 s) · armazón (6 ramas, 12 s) · cubierta (4 juncos, 3 fibra, 10 s) | 3 camas, +0,2 descanso y +0,3 abrigo al dormir |
| `hut` Wattle hut (cabaña de barro) | wattle_and_daub | cimiento (4 piedra, 8 s) · armazón (6 ramas, 2 madera, 12 s) · paredes (6 ramas, 6 barro, 14 s) · cubierta (8 juncos, 2 fibra, 10 s) | 4 camas, +0,3 descanso, +0,45 abrigo; sustituye al refugio |
| `longhouse` Longhouse (casa larga) | timber_framing | zapatas (8 piedra, 10 s) · armazón (10 madera, 3 cordel, 18 s) · paredes (10 ramas, 8 barro, 18 s) · cubierta (10 juncos, 3 esteras, 14 s) | 8 camas, +0,35 descanso, +0,55 abrigo; sustituye a refugio y cabaña |
| `storage` Storage | — | suelo (4 piedra, 2 madera, 8 s) · paredes (6 ramas, 10 s) · techo (4 juncos, 8 s) | almacén secundario (`stockpile`) |
| `cache_pit` Cache pit (silo enterrado) | food_storage | hoyo (2 piedra, 12 s) · forro (4 juncos, 2 fibra, 6 s) | +10 al tope del almacén principal; lo que hay en él se estropea a ×0,75 |
| `granary` Raised granary (granero elevado) | timber_framing | pilotes (4 madera, 4 piedra, 10 s) · suelo (3 madera, 4 ramas, 8 s) · paredes (6 ramas, 4 barro, 10 s) · techo (6 juncos, 8 s) | +20 al tope; se estropea a ×0,5 |
| `workbench` Workbench | — | banco (3 madera, 2 piedra, 8 s) | estación `workbench` |
| `drying_rack` Drying rack | — | armazón (4 ramas, 6 s) · rejilla (3 fibra, 6 s) | estación `drying_rack` |
| `hearth` Communal hearth (hogar) | cooking | anillo (6 piedra, 8 s) · fuego (3 madera, 6 s) | estación `hearth`; +0,4 abrigo y +0,15 compañía por segundo a 6 m |
| `smoking_rack` Smoking rack (ahumadero) | smoking | armazón (6 ramas, 8 s) · cubierta (4 juncos, 2 fibra, 8 s) | estación `smoking_rack` |
| `hide_frame` Hide frame (curtiduría) | hide_working | foso (2 piedra, 6 s) · bastidor (4 ramas, 2 cordel, 8 s) | estación `tannery` |
| `healer_hut` Healer's hut (choza del curandero) | herb_lore | armazón (6 ramas, 8 s) · paredes (6 juncos, 2 fibra, 8 s) · remedios (2 pomada, 4 s) | heridas ×2 más rápidas a 5 m |
| `totem` Totem (lugar de reunión) | ritual | círculo (6 piedra, 8 s) · poste (2 madera, 12 s) | +0,25 compañía por segundo a 9 m |
| `palisade` Palisade (empalizada) | palisade_building | estacas (12 estacas, 14 s) · travesaños (6 ramas, 3 cordel, 10 s) | defensa 1 |
| `kiln` Kiln (horno) | pottery (era 2) | base (6 piedra, 8 s) · cúpula (8 arcilla, 12 s) · chimenea (2 arcilla, 4 s) | estación `kiln` |
| `forge` Forge (forja) | smelting (era 2) | hogar (6 piedra, 6 ladrillo cocido, 12 s) · fuelle (2 piel, 2 madera, 8 s) | estación `forge` |

Dieciséis edificios: los cuatro de antes (sin tecnología: saber común), diez nuevos de la era 1 y dos de la era 2. Corral: no, porque no hay animales
de cría (queda para cuando los haya); mejorar el pozo: no, porque el pozo es un interactivo y moverlo es de H12.

## 5. Objetos y recetas nuevos

| Objeto | Qué es | De dónde |
|---|---|---|
| `herbs` Healing herbs | `Item.Resource.Herb` | nodo `herb_patch` (a mano, `Gather`; prado y bosque) |
| `hide` Hide | `Item.Resource.Hide` | la caza: subproducto de los conejos (`combat.hunts.rabbit.byproduct`) |
| `poultice` Poultice | `Item.Medicine.Poultice` | `poultice`: 2 hierbas + 1 fibra (herb_lore) |
| `leather` Leather | `Item.Resource.Leather` | `leather`: 2 pieles en la curtiduría (hide_working) |
| `leather_bag` Leather bag | contenedor `Item.Container.Bag`, +9 casillas | `leather_bag`: 2 cuero + 1 cordel en el banco (hide_working) |
| `cooked_meat` Cooked meat | `Item.Food.Meat.Cooked`, dura 2400 s | `cooked_meat`: 1 carne en el hogar (cooking) |
| `smoked_meat` Smoked meat | `Item.Food.Meat.Smoked`, dura 9600 s | `smoked_meat`: 2 carne + 1 madera en el ahumadero → 2 (smoking) |
| `daub` Daub | `Item.Resource.Daub` | `daub`: 2 arcilla + 1 fibra → 2 (wattle_and_daub) |
| `reed_mat` Reed mat | `Item.Resource.Mat` | `reed_mat`: 4 juncos + 1 cordel (weaving) |
| `stake` Stake | `Item.Resource.Wood.Stake`, combustible 1 | `stakes`: 1 madera → 2, con hacha (palisade_building) |
| `flint_spear` Flint spear | arma `Item.Weapon.Spear` | `flint_spear`: 1 madera + 1 sílex + 1 cordel en el banco (spear_making) |
| `fired_brick` Fired brick | `Item.Resource.Brick.Fired` | `fired_brick`: 2 arcilla + 1 madera → 2 en el horno (pottery) |

La conservación elige la receta conocida cuyo producto **dura más** (antes, la primera por id): con hogar y ahumadero la carne sobrante se ahuma.

## 6. Código que lo hace funcionar

### 6.1 Conocimiento

- `ClanKnowledge::NextExperiment`: primero las tecnologías con materiales a mano (por id, como antes); una **idea** solo cuando no hay ninguna, y
  entre ideas la que menos intentos fallidos lleva (empate por id). Así una idea siempre disponible no tapa a las demás.

### 6.2 Validador

- Una tecnología se puede probar con una receta cuya estación da un edificio que **ella misma** desbloquea (y se puede pagar): en el juego el intento
  no necesita la estación. Es lo que deja descubrir cocina, ahumado, curtido, alfarería y fundición.
- `era1` ya no difiere nada (el cobre se funde en la forja de la era 2); `era1` da `hide` (la caza) igual que da `meat`.

### 6.3 Edificios (`BuildingDef`, sección `buildings`)

Campos nuevos (todos opcionales; un fichero antiguo carga igual):

| Campo | Qué hace | Quién lo lee |
|---|---|---|
| `storeCap` | unidades más por objeto en el almacén principal (`Tribe::StoreCap`) | H18 |
| `keep` | multiplicador del ritmo de caducidad del almacén principal (el menor de los que están en pie) | H18 |
| `defense` | valor de defensa: lo resta el atacante a su motivo de incursión y suma valor a quien defiende el campamento | H14 |
| `healing` | multiplica la curación de las heridas a quien está a su alcance; los heridos van a reponerse allí | H14 |
| `culture` | compañía por segundo a quien está a su alcance | H10 |
| `aura` | metros más allá de la huella a los que llegan abrigo (sin camas), compañía y curación | — |
| `supersedes` | ids de edificios que deja de pedir para las camas cuando este se puede planear | H8 |

### 6.4 Planificador (H8)

Necesidades nuevas, además de camas, almacén lleno y estación:

| Necesidad | Urgencia |
|---|---|
| La comida se estropea en el almacén y hay un edificio con `keep` < 1 | 1,2 |
| El almacén está lleno de algo y hay un edificio con `storeCap` | 2 (como el almacén) |
| Alguien está herido y hay un edificio con `healing` | 1,2 (0,3 sin heridos) |
| El clan ha sufrido incursiones y hay un edificio con `defense` | 2 (0,35 sin incursiones) |
| Un lugar de reunión (`culture`) | 0,3 + 0,05 por miembro (máx. 0,8) |
| Abrigo común (`warmth` con `aura`, sin camas) | 0,4 |

Cada una solo para el primer edificio de su tipo. Lo pagable incluye intermedios de recetas conocidas. **Varias obras a la vez:** `economy.construction`
(`maxSites` 3, `membersPerSite` 4): una obra abierta por cada cuatro miembros vivos, como mucho tres (un clan de 4 sigue con una).

### 6.5 Efectos

- **Defensa (H14):** `RaidTarget::Defense` (lo dice cada clan al asentarse); `WeighRaid` resta `raids.defense` × defensa del objetivo; al defender el
  campamento, `threats.walls` × defensa se suma a luchar.
- **Curación (H14):** `Combatant::Heal` (multiplicador) = 1 + suma de `healing` de los edificios a su alcance; la opción `recover` lleva al herido
  delante de la choza del curandero si hay una.
- **Compañía (H10):** `culture` × dt a la necesidad `Company` de quien está al alcance (hogar, tótem).
- **Almacén y caducidad (H18):** tope y ritmo de caducidad del almacén principal.
- **Caza (H14):** `HuntRule::Byproduct` / `ByproductUnits` (la piel).

### 6.6 Visuales

Cada edificio nuevo tiene sus fases en `visuals.json` (sistema procedural de edificios); el horno y la empalizada ya dibujados se usan tal cual.
Props nuevos: lanza de sílex, bolsa de cuero; montones de pieles y de carne curada.

## 7. Pendiente

- Los edificios no son obstáculos de navegación (las camas están dentro de la huella; la empalizada con puerta queda para cuando la navegación sepa de
  puertas de edificios).
- La lanza no se reparte (no hay oficio de cazador); la usa quien la lleve (el jugador, o un miembro que la coja del almacén a mano).
- Corral y animales de cría; pozo de piedra; alfarería de vasijas como contenedor del almacén.
- Barrido de semillas del ritmo (cuántos días tarda cada rama con 4, 8 y 20 miembros).
