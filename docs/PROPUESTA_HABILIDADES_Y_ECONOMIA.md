# Propuesta: herramientas, materiales, oficios, agricultura, conservación y edificios

> Documento de diseño. No cambia el motor. Reparte la visión del diseñador en hitos pequeños (E0 a E12) y clasifica cada cosa en **[D] solo datos**, **[C] necesita código** o **[A] necesita animaciones o modelos del artista**.
> Se apoya en [PLAN_SOCIEDAD_VIVA.md](PLAN_SOCIEDAD_VIVA.md) (H7 roles, H8 construcción, H11 conocimiento, H18 economía), [OBJETOS.md](OBJETOS.md), [TRIBU.md](TRIBU.md), [IA.md](IA.md), [ECOLOGIA.md](ECOLOGIA.md) e [INTERACTIVOS.md](INTERACTIVOS.md).
> Contenido de ejemplo ya incluido: `assets/data/era1_ampliado.json` (no se carga por defecto; se prueba con `content load assets/data/era1_ampliado.json` y lo comprueba `tests/items_test.cpp`).

Tamaños: **S** una sesión, **M** unas pocas, **L** varias.

## 1. Qué se puede expresar ya como datos (y qué no)

Se ha revisado `Items.h`, `Resources.h` y `Garden.h`. El cargador (`LoadContent`) acepta objetos, recetas y nodos; **las etiquetas son jerárquicas** (`Item.Tool.Axe.Flint` cuenta como `Item.Tool.Axe` en `CountTagged`, y el test lo comprueba).

| Se puede en datos hoy | No se puede hoy (y por qué) |
|---|---|
| Objetos nuevos, con peso, apilado, etiquetas, habilidad y socket | Caducidad: `ItemDef` no tiene vida útil y `ItemSlot` no guarda edad |
| Variaciones de herramienta por material (`Item.Tool.Axe.Flint`) | Que una herramienta mejor dé más rendimiento o vaya más rápida: no hay campo de potencia |
| Recetas con estación y herramienta necesaria | Que una receta exija conocimiento: `Recipe` no tiene requisito |
| Tipos de nodo nuevos (capacidad, golpe, regeneración, herramienta, objeto) | Un nodo con **dos** salidas (un árbol grande da troncos y ramas): un nodo = un objeto. Se resuelve con nodos hermanos (ver 3.2) o con código |
| Nodos por tamaño (`young_tree` / `old_tree`) | Colocarlos en el mundo: `BuildResources` solo conoce `tree`, `rock`, `berry_bush` y `fiber_grass` con su forma fija |
| Contenedores como objetos (`basket_wicker`) | Que el contenedor cambie las casillas de la mochila |
| | Cobertura de `RequiresTag` del nodo: la exigen las habilidades `Chop`/`Mine` (fijas en `Resources.cpp`), no el nodo; y la etiqueta de tipo sale de `KindTag`, que solo distingue `wood`, `stone` y "planta" para el resto (una rama saldría como `Resource.Plant`) |
| | Edificios, fases de obra, conocimiento, oficios, tablón |

Conclusión: el **catálogo** (E0) es solo datos; casi todo lo que lo hace *útil* necesita código pequeño (E1 a E3).

## 2. Hitos propuestos

| Hito | Nombre | Tamaño | Clase | Depende de |
|---|---|---|---|---|
| E0 | Catálogo ampliado de materiales, herramientas y recetas | S | [D] (hecho aquí) | nada |
| E1 | Nodos más finos: tamaños, etiqueta de tipo desde datos, colocación guiada por datos | M | [C] | E0 |
| E2 | Herramientas con nivel: potencia y velocidad, y contenedores que amplían la mochila | S–M | [C] | E0 |
| E3 | Aptitudes por práctica y oficios derivados | M | [C] | E2, H7 |
| E4 | Conocimiento por agente y por clan; recetas desbloqueables | M–L | [C] | E0, H11 |
| E5 | Descubrimiento de herramientas nuevas (observación y experimentación) | M | [C] | E4 |
| E6 | Caducidad y conservación (secado, almacén, sal, ahumado) | M–L | [C] | E0 |
| E7 | Agricultura: semillas, campo, ciclo y estaciones | M–L | [C] + [A] | E1, E2, E6 |
| E8 | Tablón de trabajos del clan | M | [C] | E3, H7 |
| E9 | Edificios por datos: refugio, almacén, secadero | L | [C] + [A] | E1, E6, H8 |
| E10 | Edificios avanzados: corral, taller, horno | M–L | [C] + [A] | E9 |
| E11 | Escala de clanes: capacidad de carga, logística, varios almacenes | M | [C] | E8, E9 |
| E12 | Futuro: terreno suave y bases con malla procedural | L | [C] | E9 |

Cada hito se describe en las secciones 3 a 11 con sus habilidades, datos, animaciones, riesgos.

## 3. Herramientas y variaciones (E0, E2)

### 3.1 Variaciones por material

Una herramienta es un objeto con una etiqueta que **hereda** de la del tipo, así que cualquier receta o nodo que pida `Item.Tool.Axe` acepta todas las variantes:

| Objeto | Etiqueta | Materiales | Estación | Nota |
|---|---|---|---|---|
| `stone_axe` (ya existe) | `Item.Tool.Axe` | madera, piedra, fibra | mesa de trabajo | base |
| `flint_axe` | `Item.Tool.Axe.Flint` | pedernal 2, rama 2, cordel 1 | mesa | filo mejor que la piedra |
| `copper_axe` | `Item.Tool.Axe.Copper` | cobre nativo 3, rama 2, cordel 1 | forja (no existe aún) | llega con E10 |
| `bone_pickaxe` | `Item.Tool.Pickaxe.Bone` | hueso 3, rama 2, cordel 1 | mesa | pico ligero |
| `stone_hoe` | `Item.Tool.Hoe` | piedra 2, rama 2, cordel 1 | mesa | habilita `Dig` de la azada |
| `flint_sickle` | `Item.Tool.Sickle` | pedernal 1, rama 1, cordel 1 | mesa | cosecha de grano |
| `basket_wicker` | `Item.Container.Basket` | junco 5, rama 2 | a mano | amplía la mochila (E2) |

Los materiales intermedios (`cordage` = 3 fibras, `plank` = 1 tronco por 2 tablones con hacha) también son datos.

### 3.2 Código que lo hace útil (E2, M)

- `ItemDef::Power` (rendimiento extra por golpe) y `ItemDef::Speed` (multiplicador de la habilidad). Un hacha de cobre da +1 madera cada 3 golpes o golpea 25 % más rápido. Se lee en `ResourceSystem` al sacar unidades.
- `ItemDef::BagSlots` (casillas extra que da el objeto mientras se lleva) y `BagWeight` (peso máximo extra). `Inventory` ya tiene `Slots()` y `MaxWeight()`: basta con un `SetCapacity` y un `SyncCapacity(actor)` hermano de `SyncTools`. Una cesta de mimbre: +6 casillas y +8 kg. Sólo una cesta cuenta (la mejor), para evitar acumularlas.
- Habilidades: **no hacen falta nuevas** para usar las variantes; `Chop`, `Mine`, `Dig`, `Gather` ya las cubren. Se les añade una **segunda opción de herramienta** (`Item.Tool.Axe` o cualquier hijo) que ya está resuelta por la jerarquía de etiquetas.
- Riesgo: la exigencia de herramienta del nodo (`RequiresTag`) hoy no la comprueba el nodo; para tener *niveles* (cobre sólo con pico duro, por ejemplo `Item.Tool.Pickaxe.Hard`) hay que mover la comprobación del nodo a la habilidad (parte de E1).

### 3.3 Que los NPC aprendan a crear herramientas nuevas (E4 y E5)

Ver sección 6 (conocimiento) y 7 (descubrimiento). En resumen: la receta `flint_axe` existe en datos pero está **bloqueada** hasta que el agente o el clan la conoce.

## 4. Materiales recolectables más finos (E1)

### 4.1 Tabla de nodos de `era1_ampliado.json`

| Nodo | Da | Capacidad / golpe | Regeneración | Herramienta | Habilidad | Nota |
|---|---|---|---|---|---|---|
| `young_tree` | rama | 3 / 1 | 1 cada 40 s | manos | `Gather` | árbol pequeño, sólo ramas |
| `old_tree` | tronco | 6 / 1 | 1 cada 120 s | hacha | `Chop` | árbol grande; ver abajo para las ramas |
| `oak_acorns` | bellota | 4 / 2 | 2 cada 90 s | manos | `Gather` | semilla de árbol |
| `flint_outcrop` | pedernal | 5 / 1 | no | pico | `Mine` | piedra afilada |
| `granite_rock` | bloque de granito | 8 / 1 | no | pico | `Mine` | piedra de construcción |
| `copper_outcrop` | cobre nativo | 4 / 1 | no | pico | `Mine` | era del cobre |
| `clay_bank` | arcilla | 8 / 2 | 2 cada 60 s | manos (azada mejora) | `Gather` | ladrillos y cerámica |
| `reed_bed` | junco | 8 / 2 | 2 cada 20 s | manos | `Gather` | cestería |
| `pine_resin` | resina | 3 / 1 | 1 cada 90 s | manos | `Gather` | pegamento y antorchas |
| `wild_grain` | semilla de grano | 6 / 2 | 2 cada 60 s | manos | `Gather` | origen de la agricultura |
| `salt_flat` | sal | 10 / 2 | no | manos | `Gather` | conservación |
| `old_bones` | hueso | 3 / 1 | no | manos | `Gather` | herramientas de hueso |

### 4.2 Árbol grande con ramas **y** troncos

Un nodo da un solo objeto. Tres opciones, de menos a más trabajo:

1. **Nodos hermanos** (datos, hoy): un árbol grande es la entidad `old_tree` (troncos) más una entidad hija `young_tree` (ramas) colocadas juntas. Sin código, pero dos nodos.
2. **`ResourceNodeDef::Yields`** (lista de objeto, proporción) con el reparto por golpe: p. ej. cada 3 golpes un tronco y cada golpe una rama (S–M, código en `ResourceNode::Gather`).
3. **Tamaño continuo**: un atributo `Size` del nodo (0 a 1) que escala capacidad y lo que da; un árbol *crece* (brote, joven, viejo) con las estaciones. Se enlaza con la plantación (sección 8).

Recomendado: 1 para probar ya, 2 como destino, 3 sólo si se quiere crecimiento visible.

### 4.3 Código de E1 (M)

- `KindTag` pasa a leer las etiquetas del objeto (`Item.Resource.Wood*` da `Resource.Wood`, `Item.Resource.Stone*` da `Resource.Stone`, etc.), no el id.
- `BuildResources` recorre los **nodos del documento** en lugar de cuatro filas fijas; la forma (cubo, esfera, tamaño) sale de campos de datos opcionales (`shape`, `scale`, `color`) hasta que haya modelos.
- Nuevos campos de nodo: `size` (`small|medium|large`) y `tier` (nivel de herramienta requerido).
- Riesgo: la tribu elige por tipo de nodo conocido; los tipos nuevos exigen añadir pesos de rol (ver E3) o entrarán como "otro recurso".

### 4.4 Plantar

Un árbol *se planta* con `PlantTree` (ya existe, usa `plant_tree`) y una bellota o plantón; el huerto ya tiene `Tree Spot` (`State.Tree.Empty/Sapling/Grown`). Propuesta: un `Tree Spot` en `Grown` se convierte en nodo `old_tree` o `young_tree` según el ciclo, y `Saplings`/`Seeds` pasan de atributos del jugador a **objetos** (`acorn`, `grain_seed`) consumidos de la mochila (E7).

## 5. Oficios y aptitudes (E3, E8)

Hoy el rol es un conjunto de pesos fijos (TRIBU.md). Se propone que **el oficio sea una consecuencia de la práctica**, no una etiqueta:

- **Aptitud** = atributo por agente (`Skill.Woodcutting`, `Skill.Mining`, `Skill.Foraging`, `Skill.Farming`, `Skill.Weaving`, `Skill.Building`, `Skill.Crafting`), 0 a 100. Sube al usar la habilidad correspondiente (con rendimientos decrecientes) y baja despacio sin usarla. Se guardan como atributos del agente, así que ya entran en el guardado.
- **Efecto**: multiplica el rendimiento y la velocidad de la habilidad (un efecto `Skill.Bonus.*` con magnitud por nivel) y reduce la fatiga (`Vigor`).
- **Oficio** = la aptitud más alta por encima de un umbral. Mapa orientativo:

| Oficio | Aptitud | Herramienta típica | Nodos y tareas |
|---|---|---|---|
| Recolector | Foraging | manos, cesta | bayas, fibra, junco, resina, grano silvestre |
| Leñador | Woodcutting | hacha | `old_tree`, `young_tree`, tablones |
| Cantero | Mining | pico | piedra, pedernal, granito, cobre |
| Agricultor | Farming | azada, hoz | campo, riego, cosecha |
| Cestero | Weaving | manos | cuerda, cestas, esteras |
| Constructor | Building | hacha, martillo | obras del tablón |
| Artesano | Crafting | mesa | herramientas, ladrillos |

- **Cambio de oficio**: cada cierto tiempo el agente puntúa "me conviene cambiar" con la necesidad del clan (tablón) y su aptitud. Los pesos de rol de la IA pasan a **calcularse** desde las aptitudes (cada aptitud alimenta el peso de las opciones `gather <nodo>` de su familia), con una pequeña inercia para no oscilar.
- **Tablón de trabajos del clan** (E8): lista ordenada de `Job { tipo, objetivo, cantidad, prioridad, reclamado por }`. Lo rellenan el almacén (falta X), las necesidades (invierno cerca, sin comida), las obras (faltan materiales) y el jugador. El agente añade a su utilidad la opción `job <id>` con puntuación = prioridad × aptitud × cercanía. Reglas para evitar atascos: un trabajo reclamado caduca si su dueño no avanza; los no deseados suben de prioridad con la espera.
- **Memoria y conocimiento**: un trabajo sólo se ofrece al agente si **conoce** el sitio (memoria espacial, IA.md) y **conoce** la receta o el uso (sección 6).
- Habilidades nuevas: ninguna para los oficios en sí. Se registra el efecto de práctica `Skill.Practice` (aplicado por el sistema cuando una habilidad termina bien) y los efectos `Skill.Bonus.<Familia>`.

Tamaño: aptitudes y oficios derivados **M**; tablón **M**. Riesgo: balance de curvas (que la práctica no sea ni trivial ni interminable) y que el oficio no cambie cada minuto: barridos de semillas (H20).

## 6. Conocimiento por agente y por clan (E4)

H11 diseña el árbol tecnológico completo; aquí se pide un **primer corte pequeño** que valga para herramientas.

Modelo:

- `KnowledgeId` = identificador de un saber: `recipe:flint_axe`, `use:salt_preserve`, `craft:basket`, `skill:farming`, `site:clay_bank` (esto último ya lo cubre la memoria espacial).
- `KnowledgeBase` por agente: conjunto de saberes con nivel de dominio (0 a 1). Una segunda por **clan** (lo que cualquiera de sus miembros conoce y se ha compartido). Se guardan con el estado de IA (`ai` en el guardado) y se mezclan igual que `SpatialMemory::Merge`.
- **Puertas en datos**: `Recipe` (y edificio, y nodo) gana `knowledge` (lista de saberes necesarios). Sin él la receta no aparece como opción ni en el editor ni para el NPC. Las recetas de base (`cordage`, `stone_axe`) se conocen desde el principio de la era.
- **Difusión**: en el campamento, igual que la memoria de clan (IA.md): el que sabe enseña al que no a un ritmo lento y sólo si el aprendiz está cerca y desocupado; muere el último que lo sabe y el saber sale del clan (si no está en un libro, un edificio o un aprendiz).
- **Habilidades**: `Teach` (maestro y aprendiz, requiere compañía) y `Study` (observar una pieza). Efectos `Knowledge.Learn.<id>`.
- Editor: `knowledge status <actor|clan>` y `knowledge grant <id>` para pruebas.
- Tamaño M–L. Riesgo: explosión de saberes; empezar con 10 y que sean datos.

## 7. Descubrimiento de variaciones (E5)

Dos vías, las dos deterministas (hash de nombre y reloj, como el paseo de los conejos):

1. **Observación**: un agente que ve a otro (o al jugador) fabricar algo, o que ve una herramienta en el suelo, gana "progreso de comprensión" del saber correspondiente. Al llegar a 1 lo aprende. Usa la percepción de H5 (cono y distancia).
2. **Experimentación**: con tiempo libre, comodidad (necesidades altas) y materiales en la mochila, un agente prueba una **combinación plausible**: elige una receta *desconocida* cuyos ingredientes lleva y cuya "etiqueta de familia" encaja con otra que sabe hacer (sabe el hacha de piedra, prueba el pedernal). Cada intento cuesta tiempo y materiales (una fracción se pierde) y tiene probabilidad creciente. Habilidad `Experiment` con montaje de trabajo en el banco.
3. **Accidente** (opcional): un golpe contra pedernal da lascas, y descubre "el pedernal corta".

Datos nuevos: `Recipe.discovery` = `{ "mode": "observe|experiment|accident", "family": "Item.Tool.Axe", "difficulty": 0.3 }`, y `Item.Family` para saber qué variantes se parecen. Con ellos "los NPC crean herramientas a partir de los materiales que conocen" sale solo: la IA añade la opción `experiment` cuando conoce materiales de una receta bloqueada.

Tamaño M. Riesgos: que el clan descubra todo enseguida o nunca (curva de dificultad y barrido de semillas); que los NPC derrochen materiales (límite de intentos y reserva del almacén).

## 8. Agricultura (E7)

Ya existe: parcelas con `Dig`, `Sow`, `Water`, `Pull`; ciclo `Sown`, `Sprout`, `Grown` y `GardenSystem::GrowthRate` por estación; `PlantTree` y `Tree Spot`. Faltan **cultivos con identidad**, **semillas como objetos** y **campos generados**.

### 8.1 Ciclo

| Fase | Etiqueta del suelo | Habilidad | Herramienta | Tiempo | Notas |
|---|---|---|---|---|---|
| Preparar | `State.Soil.Dug` | `Dig` | azada (`Item.Tool.Hoe`) | 1 acción | hoy `dig_plant` |
| Sembrar | `Planted.Sown` | `Sow` | semilla de la mochila | 1 acción | consume `grain_seed` en vez del atributo `Seeds` |
| Regar | `State.Wet` | `Water` | cubo o cuenco | cada `MoistureSeconds` | la lluvia riega |
| Brote | `Planted.Sprout` | (crece) | | `StageSeconds` por la estación | detenido en invierno |
| Madura | `Planted.Grown` | (crece) | | | |
| Cosechar | `State.Soil.Dug` | `Harvest` (nueva) | hoz (`Item.Tool.Sickle`) o a mano | 1 acción | da `grain` y `grain_seed` |
| Barbecho | | | | | repone o se agota (opcional) |

### 8.2 Datos y código

- Definición de cultivo en datos: `crops: [ { id, seed, yield: {grain: 3, grain_seed: 1}, stageSeconds, seasons: ["spring","summer"], water: 1 } ]`. Una parcela pasa a llevar `Crop.<id>` como etiqueta. Hoy `GardenSystem` tiene un solo cultivo implícito (S–M de código para generalizarlo).
- Estaciones: sembrar en primavera, cosechar a fin de verano y otoño; sembrar fuera de tiempo da `Growth` sin avance (o se muere con el frío). `SeasonClock` ya da el factor.
- **Generar el campo**: la acción `PlanField` del constructor/agricultor elige una zona llana cercana al campamento y al agua (la misma puntuación del lugar de H8), traza N parcelas en rejilla y las deja marcadas `State.Soil.Grass`, listas para `Dig`. Es el tablón quien dice "falta campo" (E8). Usa `BuildGarden` generalizado a `BuildField(origin, rows, cols)`.
- **Aprender a cultivar**: saber `skill:farming` (sección 6) que se descubre al ver crecer `wild_grain`/semillas caídas (observación) o plantando semillas silvestres (experimento).
- Habilidad nueva: `Harvest`. Efecto: `Cost.Harvest`. Montaje existente: `pull_plant` (arrancar). Ver animaciones.
- Riesgos: el huerto del jugador y el del clan no deben competir por reglas distintas (mantener el mismo `GardenSystem`); el coste por parcela con campos grandes (usar un solo sistema, no una entidad pesada por parcela, o agrupar).

## 9. Conservación de comida (E6)

### 9.1 Caducidad por objeto

- `ItemDef::ShelfLife` (segundos de simulación; 0 = no caduca) y `ItemDef::Spoiled` (objeto en que se convierte, por ejemplo `rotten_food` o nada).
- `ItemSlot` pasa a guardar `Age` (o fecha de entrada). La mochila y el almacén de la tribu guardan pilas con su edad; cuando se mezclan se promedia hacia la más vieja (la regla más conservadora y determinista). Cambia el guardado (`Inventory::ToJson`): hace falta migración (H4).
- Vidas útiles orientativas (día de juego = 1200 s, estación = 240 s según ECOLOGIA.md):

| Alimento | Fresco | Procesado |
|---|---|---|
| Baya | 900 s (3/4 de día) | `dried_berry` (secado): 4 estaciones |
| Grano | 4 estaciones | molido: 1 estación |
| Carne/pescado (futuro) | 600 s | seco o ahumado: 3 estaciones; salado: 4 |
| Agua | no caduca | |

### 9.2 Conservación

| Método | Objeto o estación | Efecto | Ingredientes |
|---|---|---|---|
| Secado | estación `drying_rack` (secadero) | `berry` x3 → `dried_berry` x2 (receta en el JSON) | tiempo + sol; no funciona con lluvia (condición de clima) |
| Salado | estación o cuenco | multiplica la vida útil x3 | `salt` (nodo `salt_flat`) |
| Ahumado | hoguera + secadero cubierto | carne x3; olor atrae animales | `Fire Pit` encendida |
| Almacén cubierto | edificio (E9) | vida útil x0,5 (se estropea más despacio); x0,25 si frío | madera, ladrillo |
| Bodega o cueva fría (futuro) | edificio / sitio | x0,1 | piedra |

La conservación es un **multiplicador de edad por contenedor** (`Container.SpoilRate`, 1 = normal), así que un almacén con secadero cerca y la misma comida duran distinto. Se aplica en una `SpoilageSystem::Update(dt)` que envejece las pilas (determinista, por nombre).

### 9.3 Entrada en las necesidades y la IA

- Hambre y la opción `eat`: prefiere lo que **antes caduca** (la baya fresca antes que la seca); evita lo podrido (`Hunger` no sube y baja `Health` un poco).
- Nueva consideración `winter_reserve`: en otoño (según `SeasonClock`), `urgencia = max(0, días de comida deseados - días de comida del almacén)`; alimenta las opciones `gather berry`, `harvest`, `process` (secar) y los trabajos del tablón. Hoy no hay presión de estación en las decisiones.
- `Warmth` y `Safety` no cambian, pero el almacén cubierto reduce el riesgo de que los animales (conejos) coman reservas.
- Tamaño M–L (guardado incluido). Riesgo: coste de CPU con muchos stacks (envejecer por bloques de 10 s) y migración del guardado.

## 10. Edificios nuevos (E9, E10)

Parte de H8 (construcción por fases). Se propone que un edificio sea **datos + un interactivo con estado** como el pozo y la fogata:

```json
{ "id": "drying_rack", "name": "Drying rack",
  "knowledge": ["craft:rack"],
  "cost": { "branch": 6, "cordage": 3 },
  "phases": [ { "name": "frame", "work": 20, "needs": { "branch": 6 } },
              { "name": "roof",  "work": 20, "needs": { "cordage": 3 } } ],
  "size": [3, 2], "ability": "Build",
  "stationOf": ["dried_berry"],
  "effects": { "SpoilRate": 0.5 } }
```

El estado de obra son atributos (`BuildProgress`, `Phase`) y etiquetas (`State.Building.Site|Frame|Done`), así que se guarda con el resto. Cada fase consume los materiales del almacén y pide horas de trabajo de agentes con la aptitud `Building` (su trabajo viene del tablón).

| Edificio | Coste | Efecto sobre necesidades | Otro efecto | Fases | Hito |
|---|---|---|---|---|---|
| **Refugio** (choza) | 8 ramas, 6 junco, 2 cordel | `Rest` +0,6/s dentro; `Warmth` pérdida x0,5 en invierno y noche; `Safety` +0,2/s | capacidad 2–4 | cimiento, armazón, cubierta | E9 |
| **Almacén** | 6 tablones, 4 ladrillos | | pilas de mayor capacidad; `SpoilRate` x0,5; protege de animales | suelo, paredes, techo | E9 |
| **Secadero** | 6 ramas, 3 cordel | | estación `drying_rack`; `SpoilRate` x0,5 en su inventario | armazón, techo | E9 |
| **Corral** | 10 ramas, 4 cordel | `Safety` | cría de animales cuando existan; protege cultivos de conejos | postes, valla, puerta (`Gate`) | E10 |
| **Taller** | 8 tablones, 4 piedras | | estación `workbench` fija; +velocidad de craft | suelo, techo | E10 |
| **Horno** (arcilla) | 20 ladrillos, resina | | estación `kiln`/`forge`: ladrillo cocido, cobre | base, cúpula | E10 |
| **Hogar comunal** (fogata cubierta) | piedra, tablones | `Warmth` radio mayor | mantiene brasas más tiempo | | E10 |

Efectos sobre necesidades: un **aura** (`AuraDef`: radio, atributo, ritmo) que `Needs.h` ya sabe aplicar para la hoguera (+0,5/s de `Warmth` junto a ella). Se generaliza a `BuildingDef::Auras` evitando código por edificio.

Habilidades: `Build` (trabajar una obra: martillear) y `Haul` (llevar materiales de la obra: `Gather`/`Deposit` ya existen). Animaciones: ver sección 13.

Riesgos: colisiones con árboles y rocas (despeje de H8); la **navegación** (rehacer celdas al colocar: `SyncObstacles` ya existe); coste visual hasta que haya modelos (empezar con cubos y fases visibles por escalado, como las parcelas).

## 11. Escala de los clanes (E11)

- **Recolección ampliada**: cada tipo de nodo se agrupa en *yacimientos* (campo de bayas, pedrera, bosque) con un presupuesto: cuando un yacimiento baja del 30 % el tablón pide *explorar* o *plantar*. Los nodos que no regeneran (roca, pedernal, cobre) generan presión de migración (H12).
- **Capacidad de carga** del clan: `comida producida por día / hambre por persona`; el nacimiento (H9) se frena si el almacén no cubre N días.
- **Logística**: varios almacenes (y el campamento) con distancia; el agente deposita en el más cercano que acepta ese objeto (etiquetas de almacén: comida, madera, piedra). Hoy hay un único inventario común.
- **Rendimiento**: ya hay O(miembros x cosas) en percepción (IA.md); con clanes de 30 y varios yacimientos hace falta la rejilla espacial que IA.md menciona.
- **Reparto**: la fracción de agentes por oficio la fija el tablón (por necesidad), no una tabla fija.

## 12. Futuro: terreno suave y mallas procedurales (E12)

### 12.1 Alisar terreno

- Herramienta `Flatten` (modo del editor y habilidad `Level`): aplana un disco/rectángulo del heightmap a la media (o a una altura objetivo) con un borde en rampa; el radio y la fuerza se pueden pintar.
- **Zonas suaves**: máscara (`TerrainZone`) con alturas objetivo y tipo de suelo (tierra apisonada, camino). La zona se guarda como cambios (delta) sobre el terreno, para no regenerar nada.
- Lo usan: cimientos de edificios (H8 "nivelación del suelo"), campos y caminos que los NPC prefieren (la `NavGrid` baja el coste en camino).
- Riesgos: determinismo del terreno con edición; actualizar físicas, vegetación y nav (`UpdateRegion` ya existe); deshacer.

### 12.2 Bases con malla procedural según el material

- Un edificio se arma desde **módulos** (cimiento, muro, esquina, techo, puerta) que una función genera dado `Material { color, rugosidad, textura, forma }`: tronco/rama (cilindros apilados con ruido), tablón (paneles), piedra (bloques con juntas irregulares por hash), barro/ladrillo (volumen con aristas redondeadas), paja/junco (planos con fleco).
- El **material de cada edificio sale de lo que se gastó** (porcentaje por objeto en `cost`): una choza con 70 % de ramas se genera con muro de ramas. Eso hace que el aspecto delate la tecnología y los recursos del clan.
- El generador usa los mismos primitivos del motor (como `BuildResources`) y la misma semilla para ser reproducible; no se guardan mallas, sólo `BuildingId + semilla + materiales`.
- Modelos del artista: cuando lleguen, sustituyen a los módulos procedurales por tipo de material, y los procedurales quedan como reserva.
- Riesgo: coste de malla y de colisiones con muchos edificios (instanciar módulos).

## 13. Animaciones: qué encaja y qué pedir

Fuente: `assets/animation/mixamo.manifest.json`. Montajes con los que ya cuenta el motor: `dig_plant`, `plant`, `plant_tree`, `pick_fruit` (y `_2`, `_3`), `pull_plant` (y `_2`), `water`, `milk_cow`, `wheelbarrow_dump`, `stand_to_crouch`, `crouch_to_stand`, `kneel_idle`, `idle_variant_2..4`, gestos, `sword_slash_1..5`, `sword_attack_1..4`, `bow_*`, `block_*`, `react_*`, `death_*`.

El problema que se ve ahora: `pick_fruit` alcanza hacia arriba (fruta de un árbol) y por eso en un arbusto bajo parece invertido; `dig_plant` sirve de pala pero no de hacha.

| Acción | Montaje hoy | Mejor encaje existente (a probar en el visor) | Pedir al artista |
|---|---|---|---|
| Talar (`Chop`) | `dig_plant` | `sword_slash_2` o `sword_attack_1` a `playRate` 0,7 y la herramienta en `RightHand` (golpe de arriba abajo con un arma de una mano; interino) | **Talar con hacha en bucle** (2 manos, golpes y final de caída) |
| Picar roca (`Mine`) | `dig_plant` | `dig_plant` (golpe de herramienta al suelo, interino) o `sword_slash_3` | **Picar con pico**, bucle |
| Bayas en arbusto bajo (`Gather`) | `pick_fruit` (invertido) | `pull_plant` o `plant` (se agacha y coge), bayas altas siguen con `pick_fruit` | **Recoger de planta baja agachado**, bucle |
| Fruta de árbol (`PickFruit`) | `pick_fruit` | sin cambio | |
| Fibra, junco, grano silvestre | `pick_fruit` | `pull_plant` / `pull_plant_2` (arrancar) | **Segar con hoz** |
| Cavar con azada (`Dig`) | `dig_plant` | `dig_plant` (ya encaja) | bucle de paladas (ya pedido) |
| Cavar arcilla (`Gather` en `clay_bank`) | `pick_fruit` | `dig_plant` | agacharse a coger barro |
| Sembrar (`Sow`) | `plant` | sin cambio | agachado a ras de suelo (ya pedido) |
| Regar | `water` | sin cambio | regar agachado (ya pedido) |
| Cosechar (`Harvest`) | (nueva) | `pull_plant` | **segar con hoz** |
| Plantar árbol | `plant_tree` | sin cambio | |
| Recoger del suelo / dejar | (ninguno) | `stand_to_crouch` + `crouch_to_stand` encadenados (interino) | **recoger y dejar** (ya pedido) |
| Tejer (cestería) | (ninguna) | `kneel_idle` en bucle con la cesta en la mano (interino) | **tejer sentado** |
| Tallar piedra (`Craft` pedernal) | (ninguna) | `kneel_idle` / `idle_variant_2` | **tallar piedra** (golpear con percutor) |
| Colgar a secar | (ninguna) | `pick_fruit` (alzar los brazos, aquí sí encaja) | **colgar en el secadero** |
| Construir (`Build`) | (ninguna) | `dig_plant` / `sword_slash` (golpe) | **martillear, levantar viga, cargar tablón** (ya en PLAN) |
| Experimentar (`Experiment`) | (ninguna) | `gesture_thoughtful` | |
| Enseñar / aprender (`Teach`, `Study`) | (ninguno) | `gesture_acknowledge`, `gesture_nod_yes`, `gesture_thoughtful` | **demostrar y observar** |
| Moler grano | (ninguna) | | **moler con piedra** |

Todo lo que dice "pedir al artista" se ha añadido a [ANIMACIONES_PENDIENTES.md](ANIMACIONES_PENDIENTES.md). Modelos que se piden: hacha de pedernal y de cobre, pico de hueso, azada, hoz, cesta de mimbre (en la mano y en la espalda), cuerda, tablones, ladrillos, secadero, refugio por fases, almacén, corral, taller, horno, árboles joven y viejo, matas de junco, arcilla, resina.

## 14. Orden de implementación recomendado

1. **E0** (hecho): catálogo en `era1_ampliado.json`, sin código. Probarlo con `content load` y mirar `content status`.
2. **E2** primero que E1: potencia de herramienta y cesta (S–M). Es lo más visible para el jugador (hacha mejor, mochila mayor) con poco riesgo y sin tocar el guardado.
3. **E1**: etiquetas de tipo desde datos y colocación de nodos desde datos, para ver `young_tree`/`old_tree`/arcilla en el mundo.
4. **E3**: aptitudes por práctica (atributos), sin oficios todavía; luego derivar los pesos de rol.
5. **E4 + E5**: conocimiento y descubrimiento (el núcleo de "los NPC aprenden").
6. **E6**: caducidad y secado (toca el guardado: hacerlo con la migración de H4 y avisar).
7. **E7**: agricultura (semillas objeto, `Harvest`, `PlanField`).
8. **E8**: tablón.
9. **E9 y E10**: edificios (con cubos primero, modelos después).
10. **E11**: escala, cuando haya clanes de más de 15.
11. **E12**: terreno suave y mallas procedurales (puede adelantarse el aplanado para cimientos).

Los hitos son independientes salvo las flechas de la tabla del apartado 2: E4 a E5 y E6 se pueden hacer en paralelo por carriles distintos (simulación social frente a gameplay).

## 15. Decisiones del usuario

**Respondidas** (todas las de abajo salvo la lectura dudosa de la 7). Las preguntas originales se conservan después de la tabla.

| # | Decisión | Respuesta | Qué implica |
|---|---|---|---|
| 1 | Árbol grande: nodos hermanos o `Yields` | **Nodos hermanos ahora, `Yields` como destino** (recomendación aceptada) | E1 coloca `young_tree` y `old_tree` (ramas, troncos) como nodos distintos sin código nuevo de salidas; `Yields` queda para un hito posterior |
| 2 | ¿La rama alimenta la hoguera? | **Sí, pero dura menos**: hace falta más rama que madera normal para el mismo calor | Campo de datos `fuel` por objeto (combustible que da en `Stoke`): madera 2, rama 1 (o menos); `FuelPerWood` de `InteractiveSystem` pasa a salir del objeto |
| 3 | Caducidad obligatoria | **Solo la comida de la mochila y del almacén** al principio (recomendación aceptada); los edificios mejoran la vida útil después | E6 empieza por comida en `Inventory` (frescura por casilla); toca el guardado con migración de H4; el hueco ya está previsto en `docs/GUARDADO.md` |
| 4 | Aptitudes | **Por agente; el oficio las potencia** | E3: atributos de aptitud por agente (se heredan en H9); el oficio multiplica la ganancia y el rendimiento |
| 5 | Conocimiento | **Ambos** (propuesta aceptada): el clan posee lo que alguien conoce, el agente solo hace lo que sabe, y se puede perder | E4 |
| 6 | Descubrimiento | **Azar sembrado** (propuesta aceptada): probabilidad derivada del hash del nombre y del reloj, reproducible con la semilla | E5 |
| 7 | Cultivos de partida | Grano, bayas **y raíces**; **las estaciones pueden arruinar una cosecha** | E7: tres cultivos; helada, sequía y exceso de lluvia estropean la cosecha según el estado de la parcela (leído «raíces» donde la respuesta decía «brauces»: confirmar) |
| 8 | Quién decide los edificios | **El clan decide solo**; **el jugador puede proponer** si está en el clan | E8/E9: la obra nace del tablón; el jugador publica propuestas en el tablón, sin ser necesario para construir |
| 9 | Cobre y era 2 | **Añadirlo** al JSON desde ahora | El cobre y sus objetos entran en `assets/data/era1.json` (aunque la forja llegue en E10) |
| 10 | Animaciones para el artista | **Se guarda el listado** para buscarlas | `docs/ANIMACIONES_PENDIENTES.md` es la lista de encargo |
| 11 | Mallas procedurales | **Modelos del artista a la larga; ahora se prototipa** y los gráficos se cambian cuando estén las mecánicas | E12 queda como reserva y para cimientos; nada de pulir mallas ahora |

**Preguntas originales.**

1. **Árbol grande: ¿nodos hermanos (ya, sin código) o `Yields` (un nodo, varias salidas)?** Se recomienda hermanos para probar y `Yields` como destino.
2. **¿La rama cuenta como combustible de la hoguera?** La etiqueta `Item.Resource.Wood.Branch` hereda de `Item.Resource.Wood`; sin más, `Stoke` la aceptaría. Se puede aceptar (más realista) o forzar sólo troncos.
3. **¿La caducidad es obligatoria desde el principio?** Toca el formato de guardado. Alternativa: sólo la comida de la mochila y el almacén, y los edificios mejoran la vida útil después.
4. **¿Las aptitudes son por agente (se heredan en H9) o sólo del oficio?** Se propone por agente, con herencia parcial.
5. **¿Conocimiento: lo aprende cada agente o el clan lo posee entero?** Se propone ambos (el clan posee lo que alguien conoce, pero un agente sólo hace lo que él sabe) y que se pueda perder.
6. **¿El descubrimiento es seguro con tiempo (determinista) o con azar sembrado?** Se propone probabilidad derivada del hash del nombre y del reloj, reproducible con la semilla.
7. **Cultivos de partida:** ¿sólo grano y bayas, o también raíces? Y si las estaciones deben poder arruinar una cosecha.
8. **Edificios: ¿el jugador decide el sitio y el tablón la obra, o lo decide el clan solo?** Y si la obra necesita al jugador cuando hay uno en el clan.
9. **Cobre y era 2:** el cobre queda detrás de la forja (E10); ¿se quiere dejar el contenido en el JSON desde ahora o quitarlo hasta E10?
10. **Prioridad de animaciones al artista:** talar, picar, recoger agachado y martillear desbloquean más cosas (ver tabla de la sección 13).
11. **Mallas procedurales:** si se prefieren modelos del artista desde el principio y las mallas generadas sólo como reserva.

## 16. Contenido de `era1_ampliado.json`

23 objetos, 12 nodos y 10 recetas, todo sobre el formato actual. Se carga encima de la era 1 sin cambiar nada (el test lo comprueba) y lo único que no se puede *usar* todavía es lo que depende de código (los nodos nuevos no se colocan hasta E1; las estaciones `drying_rack` y `forge` no existen hasta E9 y E10; `copper_axe` y `dried_berry` no se pueden fabricar mientras tanto). Las habilidades `Dig` y `Gather` ya existen; `Build`, `Harvest`, `Teach`, `Study`, `Experiment` y `PlanField` son nombres tentativos.

| Nombre tentativo | Etiquetas requeridas | Efectos | Hito |
|---|---|---|---|
| `Harvest` | `Item.Tool.Sickle` opcional; objetivo `State.Soil.Planted.Grown` | `Cost.Harvest`, devuelve suelo a `Dug` | E7 |
| `Build` | `State.Building.Site`; conoce la obra | `Skill.Practice.Building`, `BuildProgress` | E9 |
| `PlanField` | conoce `skill:farming`, `Item.Tool.Hoe` | crea parcelas | E7 |
| `Teach`, `Study` | maestro y aprendiz cerca, libres | `Knowledge.Learn.<id>` | E4 |
| `Experiment` | materiales de una receta bloqueada y saber de la familia | gasta materiales, `Knowledge.Progress` | E5 |
| `Level` (alisar) | herramienta o modo del editor | modifica el heightmap | E12 |
