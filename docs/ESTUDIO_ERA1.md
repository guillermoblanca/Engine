# Estudio de contenido de la era 1: qué falta para una Edad de Piedra creíble

> Documento de diseño. No cambia el motor ni los datos: propone qué tecnologías, edificios, recursos, objetos, recetas, especies, oficios y comportamientos
> generar para la fase en la que está el juego, con ids y esbozos de datos para que un carril de contenido los implemente. Marca **[D]** solo datos,
> **[C]** necesita código, **[A]** necesita animaciones o modelos. Prioridades: **P1** imprescindible para una era 1 creíble, **P2** la enriquece,
> **P3** adorno o puente a la era 2.

[← Volver al README](../README.md) · relacionado: [PROGRESION.md](PROGRESION.md) (carril de tecnologías y edificios), [PLAN_SOCIEDAD_VIVA.md](PLAN_SOCIEDAD_VIVA.md) (H17),
[PROPUESTA_HABILIDADES_Y_ECONOMIA.md](PROPUESTA_HABILIDADES_Y_ECONOMIA.md) (E0–E12), [RECURSOS_MUNDO.md](RECURSOS_MUNDO.md), [ANIMACIONES_USO.md](ANIMACIONES_USO.md)

## Resumen

1. **El juego es un mosaico.** Por herramientas y edificios está en el **Mesolítico** (hachas enmangadas, cestería, chozas de zarzo y barro, ahumadero, silo)
   e incluso en el **Neolítico** (casa larga, granero elevado, adobe, sal, empalizada, horno). Por subsistencia, ropa y armas sigue en el **Paleolítico Medio**:
   se come baya y conejo, no hay ropa cosida, ni pesca, ni armas arrojadizas, ni perro, ni frutos secos, raíces o marisco.
2. **La era 1 debe abarcar del Paleolítico Superior al Mesolítico/Natufiense** y **terminar en el umbral del Neolítico precerámico (PPNA)**: un clan
   sedentario que almacena cereal silvestre, lo muele y empieza a sembrarlo. La cerámica abre la era 2 y el cobre fundido pertenece a la era 3 (metal) de H17,
   no a la 2 como está hoy en los datos.
3. **Tras los dos carriles en curso** habrá 46 objetos, 25 recetas, 18 nodos, 20 tecnologías y 16 edificios, pero solo **2 especies**, **1 presa**, **3 roles**
   y **6 comidas** que llenan el hambre exactamente igual (+25 cada una).
4. **Huecos P1:** ropa y costura (aguja de hueso, capa de pieles que abrigue: el invierno es la prueba de la era), cuchillo de sílex, palo de cavar y comida
   vegetal de verdad (avellanas, raíces), carne seca, **subproductos de caza múltiples** (hueso, tendón, asta, grasa), **oficio de cazador** que use la lanza,
   valor nutritivo por objeto y **piedra y leña sueltas** que no pidan herramienta.
5. **Incoherencias a corregir antes de añadir nada:** los 7 nodos de `era1_ampliado` no tienen `spawn` ni `patch` (el grano, la resina, el hueso o la sal no aparecen
   en el mundo); el pedernal es finito y escaso (≈15 unidades en todo el mapa templado 5); las pieles solo salen de conejos; la estación dura 240 s mientras el
   año de la sociedad dura 4800 s; y varias piezas del carril de edificios son neolíticas (casa larga, granero, empalizada).
6. **Plan:** cinco carriles de contenido en orden: *Coherencia y arranque* (S–M) → *Subsistencia mesolítica* (M) → *Ropa, costura y adhesivos* (M) ∥
   *Caza a distancia, trampas y pesca* (L) → *Puente a la era 2 y su puerta* (M). Ninguno toca el árbol de PROGRESION ni los ritos y creencias de la vía social.

## 1. En qué fase estamos

### 1.1 La secuencia real y dónde cae cada pieza del juego

Fechas aproximadas en miles de años antes del presente (ka). Varían mucho por región; las del Levante (Natufiense, PPNA) son las que mejor encajan con
«del forrajeo a la agricultura».

| Fase | Fechas (aprox.) | Rasgos | Lo que ya tiene el juego (tras los carriles) |
|---|---|---|---|
| Paleolítico Inferior | 2600–300 ka | bifaces achelenses, fuego (≥ 1 Ma, habitual desde ~400 ka), lanzas de madera (Schöningen, ~300 ka) | `stone_tools`, `fire_making` (conocidas) |
| Paleolítico Medio | 300–45 ka | talla Levallois, **enmangue con adhesivo**, puntas de lanza, raspado de pieles, ocre, primeros enterramientos, cordel (fibra neandertal ~41–52 ka) | `flint_knapping`, `spear_making`, `hide_working`, `cordage`, `herb_lore`(?), `ritual` |
| Paleolítico Superior | 45–12 ka | láminas, **agujas de hueso y ropa cosida**, **propulsor**, arco(?), arte, adornos, cestería y textiles (~27 ka), redes, molienda de semillas (~30 ka), **perro** (≥ 15 ka), chozas de huesos de mamut | `basketry`, `weaving`; **falta casi todo lo demás** |
| Epipaleolítico / Mesolítico (Natufiense 15–11,5 ka) | 15–8 ka (6 ka en el norte de Europa) | **microlitos y arco**, pesca intensiva, marisco, avellanas tostadas, ahumado, sedentarismo parcial, casas semienterradas, **hoces y morteros**, almacenes, pan de cereal silvestre (~14,4 ka) | `smoking`, `wattle_and_daub`, `food_storage`, `tillage` y hoz, grano silvestre (ampliado) |
| Neolítico precerámico (PPNA/PPNB) | 11,5–8,5 ka | cultivo, domesticación de cabras y ovejas, **adobe**, graneros (Dhra', ~11,3 ka), casas rectangulares, hachas pulimentadas | `mud_brick`, `granary`, `timber_framing`(?) |
| Neolítico cerámico | desde ~8,5 ka (Levante) | **cerámica**, hornos, casas largas (LBK ~7,5 ka), sal, empalizadas y fosos | `pottery`, `kiln`, `longhouse`, `salt`, `palisade` |
| Calcolítico | ~7–5 ka | **fundición de cobre** (Belovode ~7 ka) | `smelting`, `forge`, `copper_ingot`, `copper_axe` |

Nota: la cerámica aparece **entre cazadores** en Asia oriental mucho antes (Xianrendong ~20 ka, Jōmon ~16 ka). Ponerla en la era 2 es una decisión de
diseño válida (sigue el camino del Próximo Oriente), no un error.

### 1.2 Diagnóstico

- **Dónde está el juego:** un *Mesolítico sin la caja de herramientas del Paleolítico Superior*. Los edificios y la cadena de herramientas corren por delante,
  y la vida diaria (qué se come, cómo se abriga uno, cómo se caza) se quedó en el Paleolítico Medio.
- **Dónde debe empezar la era 1:** en un grupo que ya sabe hacer fuego, cordel y herramientas de piedra (lo que hay conocido). Es razonable: el jugador
  no debe vivir 2 millones de años de bifaces.
- **Dónde debe terminar (puerta a la era 2):** en el **umbral natufiense–PPNA**: asentamiento de todo el año, almacenes llenos al llegar el invierno,
  cereal silvestre segado y molido, primeras siembras deliberadas y perro. La era 2 abre con **cultivo con excedente, domesticación animal y cerámica**.
- **Correcciones de era recomendadas** (en los datos del carril de edificios, cuando acabe; no en este estudio):

| Pieza | Era hoy | Recomendado | Por qué |
|---|---:|---:|---|
| `smelting`, `forge`, `copper_ingot`, `copper_axe` | 2 | **3** | H17: la era 3 es la del metal; el cobre fundido es calcolítico |
| `longhouse`, `timber_framing` | 1 | 2 (o renombrar a «casa grande mesolítica») | las casas largas son neolíticas (LBK); para la era 1, la `pit_house` de 4.5 |
| `granary` | 1 | 1 tardía (puente) | Dhra' es PPNA, anterior a la domesticación: válido como última pieza de la era 1 |
| `palisade` | 1 | 1 tardía (puente) | las fortificaciones son sobre todo neolíticas; la violencia mesolítica sí existe (Jebel Sahaba ~13 ka) |
| `mud_brick`, `salt` (ampliado) | 1 | 2 | adobe PPNA (Jericó); sal explotada ~8 ka (Poiana Slatinei) |
| `kiln`, `pottery` | 2 | 2 | correcto |

## 2. Inventario de hoy (después de los dos carriles)

Medido sobre `assets/data/*.json` de esta rama (`mundo/recursos-procedurales`) y sobre el `era1.json` del carril `contenido/tecnologias-edificios`.
El carril `contenido/vida-social` **no tiene cambios ni `docs/VIDA_SOCIAL.md` todavía** (rama en 3975677 sin modificaciones): sus ritos, creencias,
comidas nuevas y caza de ciervo, jabalí, aves y peces se dan por cubiertos según su encargo y aquí solo se referencian.

### 2.1 Recuentos

| Categoría | Hoy (`era1` + `ampliado`) | Tras el carril de edificios | Detalle |
|---|---:|---:|---|
| Objetos | 19 + 15 = 34 | **31 + 15 = 46** | 2.2 |
| Recetas | 8 + 7 = 15 | **18 + 7 = 25** | 2.3 |
| Nodos | 10 + 7 = 17 | **11 + 7 = 18** | con `spawn`: 10 → 11; **sin `spawn` ni `patch`: los 7 del ampliado** |
| Tecnologías | 6 + 2 = 8 | **18 + 2 = 20** | 3 conocidas; 2 de era 2 (cerámica, fundición) |
| Edificios | 4 | **16** | 2.4 |
| Especies (`world.json`) | 2 | 2 | `rabbit`, `fox` |
| Presas / depredadores (`combat`) | 1 / 1 | 1 / 1 | caza `rabbit` (carne 2, + piel 1 con el carril); depredador `fox` |
| Armas | 5 | 5 | `fists`, `axe`, `pickaxe`, `spear`, `bite` |
| Roles | 3 | 3 | `forager`, `woodcutter`, `miner` |
| Aptitudes (familias) | 6 | 6 | `foraging`, `woodcutting`, `mining`, `crafting`, `firekeeping`, `building` |
| Rasgos / genes / normas | 4 / 5 / 4 | igual | rasgos `sociability`, `diligence`, `curiosity`, `boldness`; normas `sharing`, `industry`, `inquiry`, `daring` |
| Tradiciones | 5 | 5 (+ las de la vía social) | `fire_vigil`, `naming_feast`, `mourning`, `lore_circle`, `toolmakers` |
| Interactivos | 3 | 3 | `fire_pit`, `well`, `gate` |
| Cultivos | 1 | 1 | `grain` (ampliado) |
| Opciones del agente | ≈ 30 | ≈ 30 | eat, drink, sleep, warm, regroup, socialize, rest, gather, deposit, tool, craft, make, fire, stoke, explore, expedition, migrate, experiment, observe, teach, build, deliver, haul, farm, hunt, fight, flee, defend, recover, care, raid, trade, visit |
| Comidas | 4 (+agua) | **6** (+agua) | baya, carne, carne asada, carne ahumada; baya seca y grano (ampliado). **Todas dan +25 de hambre** (`Needs.FoodHunger`) |

### 2.2 Objetos

| Tipo | Ids |
|---|---|
| Materia prima (era1) | `branch`, `wood`, `stone`, `flint`, `fiber`, `reed`, `clay`, `cordage`, `copper_ore`, `copper_ingot` · carril: `herbs`, `hide`, `leather`, `daub`, `reed_mat`, `stake`, `fired_brick` |
| Materia prima (ampliado) | `log`, `plank`, `resin`, `granite`, `mud_brick`, `bone`, `salt`, `grain_seed`, `acorn` |
| Herramientas y armas | `stone_axe`, `stone_pickaxe`, `flint_axe`, `flint_pickaxe` · carril: `flint_spear` · ampliado: `bone_pickaxe`, `stone_hoe`, `flint_sickle`, `copper_axe` |
| Contenedores | `basket_wicker` · carril: `leather_bag` |
| Comida y bebida | `berry`, `meat`, `water` · carril: `cooked_meat`, `smoked_meat` · ampliado: `grain`, `dried_berry` |
| Otros | `campfire` (kit), carril: `poultice` |

### 2.3 Recetas por estación

| Estación | Recetas |
|---|---|
| A mano | `cordage`, `basket_wicker`, `campfire` (con hacha), `daub`, `reed_mat`, `stakes` (hacha), `poultice` |
| `workbench` | `stone_axe`, `stone_pickaxe`, `flint_axe`, `flint_pickaxe`, `flint_spear`, `leather_bag`; ampliado: `plank`, `bone_pickaxe`, `stone_hoe`, `flint_sickle` |
| `hearth` / `smoking_rack` / `tannery` | `cooked_meat` / `smoked_meat` / `leather` |
| `drying_rack` | ampliado: `dried_berry`, `mud_brick` |
| `kiln` / `forge` | `fired_brick` / `copper_ingot`, `copper_axe` |

### 2.4 Edificios (del carril)

`shelter`, `hut`, `longhouse`, `storage`, `cache_pit`, `granary`, `workbench`, `drying_rack`, `hearth`, `smoking_rack`, `hide_frame`, `healer_hut`, `totem`,
`palisade`, `kiln`, `forge`. Provisiones genéricas: `shelter`, `rest`, `warmth`, `station`, `store`, `storeCap`, `keep`, `defense`, `healing`, `culture`,
`aura`, `supersedes`. Con modelo en `visuals.json` hoy: `shelter`, `storage`, `workbench`, `drying_rack`, `kiln`, `palisade` (el carril añade el resto).

### 2.5 Nodos y dónde aparecen

| Nodo | Objeto | Herramienta | Biomas (`spawn`) | Observación |
|---|---|---|---|---|
| `tree` / `old_tree` | `wood` | hacha | Forest, Meadow / Forest | |
| `young_tree` | `branch` | — | Forest, Meadow | |
| `berry_bush` | `berry` | — | Meadow, Forest | 71 en templado 5 |
| `fiber_grass` | `fiber` | — | Meadow, Shore | |
| `reed_bed` | `reed` | — | Shore, Meadow (≤ 6 m del agua) | **2** en templado 5 |
| `clay_bank` | `clay` | — | Shore, Meadow (≤ 9 m del agua) | 6 en templado 5 |
| `rock` | `stone` | pico | Rock, Meadow, Shore, Dunes | no se regenera |
| `flint_outcrop` | `flint` | pico | Rock, Shore, Dunes | **3** en templado 5, no se regenera |
| `copper_vein` | `copper_ore` | pico | Rock, Snow | 0 en templado, 3 en alpino |
| `herb_patch` (carril) | `herbs` | — | Meadow, Forest | |
| ampliado: `oak_acorns`, `granite_rock`, `pine_resin`, `wild_grain`, `salt_flat`, `old_bones`, `fallen_trunk` | | | **ninguno** | sin `spawn` ni `patch`: no se colocan (verificar con `resources status`) |

## 3. Referencia: lo que tenían los humanos de esta fase

### 3.1 Por categoría

| Categoría | Paleolítico Superior | Mesolítico / Natufiense |
|---|---|---|
| Herramientas | láminas, buriles, raspadores, perforadores; **agujas con ojo**; propulsores; arpones de asta | **microlitos** (armaduras de flecha), hachas de sílex talladas (tranchet), azuelas, hoces con brillo de cereal, morteros y molinos |
| Materiales | sílex, obsidiana (comerciada), hueso, **asta**, marfil, madera, tendón, cuero, fibra, **brea de abedul**, ocre | lo mismo + corteza de abedul (recipientes), piedra pulida (al final) |
| Comida | caza mayor (reno, caballo, bisonte), caza menor, aves, peces, plantas silvestres, semillas molidas | caza de bosque (ciervo, corzo, jabalí, uro), **pesca intensiva, marisco** (concheros), **avellanas tostadas**, bellotas, raíces, cereal silvestre segado |
| Conservación | secado, ahumado(?), congelación natural, escondrijos | ahumado y secado de pescado, **almacenes en hoyos**, graneros (PPNA) |
| Abrigo | tiendas de pieles, cabañas de huesos de mamut, cuevas y abrigos; **ropa cosida** | cabañas circulares de postes (Star Carr ~11 ka, Mount Sandel ~10 ka), **casas semienterradas** natufienses de piedra |
| Sociedad | bandas de 25–50 en redes de cientos; reparto de caza; reuniones estacionales | grupos más sedentarios, territorialidad, cementerios, desigualdad incipiente |
| Creencias y rito | arte rupestre y mobiliar, figurillas, adornos, **entierros con ajuar y ocre** | cementerios, entierros con perros (Ain Mallaha), banquetes |
| Comercio | conchas y sílex a cientos de km (intercambio de regalos) | obsidiana anatolia en el Levante, piedra de hachas |
| Conflicto | escaso registro | violencia intergrupal documentada (Jebel Sahaba ~13 ka) |
| Transporte | a pie; travesías marítimas (Australia ≥ 50 ka) | **piraguas monóxilas** (Pesse ~10 ka), remos, trineos(?) |

### 3.2 Secuencias y dependencias clave (las que el árbol debe respetar)

| Cadena | Requiere | Notas |
|---|---|---|
| Enmangue (hacha, lanza) | cordel o tendón **+ adhesivo** (brea, resina con ocre o carbón) + talla | brea de abedul ~200 ka; mezclas de resina y ocre ~70 ka (Sibudu) |
| Brea de abedul | corteza de abedul + fuego controlado (destilación en hoyo o sobre piedra) | |
| Ropa ajustada | piel raspada + **perforador o aguja de hueso** + tendón o cordel | agujas con ojo ~45 ka (Siberia), ~26 ka en Europa |
| Propulsor | lanza ligera (azagaya) + talla de asta o madera | evidencia directa ~17–20 ka; posible antes (?) |
| Arco | madera adecuada + cuerda (tendón o fibra) + flechas con puntas pequeñas, **adhesivo** y plumas | puntas compatibles ~64 ka (debatido); arcos conservados ~12 ka (Stellmoor). Suele ir **después** del propulsor en Europa |
| Pesca | cordel (redes, sedal) + hueso o asta (arpón, anzuelo) o cestería (nasas) | redes ~27 ka (improntas); anzuelos ~23 ka; nasas y presas mesolíticas |
| Molienda | piedra dura + semillas silvestres | ~30 ka; pan ~14,4 ka; **precede** a la agricultura |
| Cerámica | arcilla + desgrasante + **control del fuego en hoyo u horno** | no necesita agricultura (Jōmon), pero en el Levante llega tras ella |
| Perro | grupos de cazadores + lobos atraídos por restos | ≥ 15 ka; **antes** que cualquier planta o animal domesticado |
| Cultivo | siega y almacenamiento de cereal silvestre + sedentarismo | PPNA ~11,5 ka; domesticación morfológica ~10,5 ka |
| Cobre | cobre nativo martillado en frío (~10 ka) → fundición en horno (~7 ka) | por eso la fundición va tras la cerámica |

Lo incierto queda marcado con (?): el encendido de fuego a voluntad antes de ~50 ka, la fecha del primer arco y del propulsor, y si el ahumado de comida
es paleolítico.

## 4. Huecos y propuestas

Formato de los esbozos: el de `era1.json`. Las tecnologías siguen la notación de PROGRESION (`exp / observar / enseñar / s`). Los nombres en inglés en los
datos (`name`), como el resto del contenido.

### 4.1 Recursos y nodos

| P | Nodo | Esbozo | Tipo |
|---|---|---|---|
| P1 | `loose_stones` (cantos rodados) | `{"id":"loose_stones","item":"stone","kind":"Loose.Stone","ability":"Collect","capacity":3,"perGather":1,"regen":1,"regenSeconds":600,"spawn":{"biomes":["Meadow","Rock","Dunes","Shore"],"perHectare":3,"spacing":6,"maxSlope":30,"clump":0.4}}` | [D] |
| P1 | `deadfall` (leña muerta) | `{"id":"deadfall","item":"wood","kind":"Loose.Wood","ability":"Collect","capacity":2,"perGather":1,"regen":1,"regenSeconds":300,"spawn":{"biomes":["Forest"],"perHectare":4,"spacing":6,"maxSlope":30,"clump":0.3}}` | [D] |
| P1 | `hazel_shrub` (avellano) | `{"id":"hazel_shrub","item":"hazelnut","kind":"Plant","ability":"Gather","capacity":8,"perGather":2,"regen":2,"regenSeconds":120,"spawn":{"biomes":["Forest","Meadow"],"perHectare":3,"spacing":6,"maxSlope":30,"clump":0.5}}`; solo en otoño con `"seasons":["autumn"]` | [D] + [C] S (estación del nodo) |
| P1 | `tuber_patch` (raíces silvestres) | `{"id":"tuber_patch","item":"tuber","kind":"Plant.Root","ability":"Collect","requires":"Item.Tool.DiggingStick","capacity":4,"perGather":1,"regen":1,"regenSeconds":120,"spawn":{"biomes":["Meadow","Shore"],"perHectare":3,"spacing":6,"maxSlope":20,"waterWithin":25,"clump":0.4}}` | [D] |
| P1 | dar `spawn` a los 7 del ampliado | `wild_grain` {Meadow, 4/ha, clump 0,6, pendiente 15}; `oak_acorns` {Forest, 2/ha}; `pine_resin` {Forest, Snow, 2/ha}; `fallen_trunk` {Forest, 2/ha}; `old_bones` {Meadow, Dunes, Snow, 0,5/ha, máx. 8}; `granite_rock` {Rock, Snow, 2/ha}; `salt_flat` {Dunes, Shore, máx. 4} | [D] |
| P1 | ajustar `flint_outcrop` | `regen 1, regenSeconds 1800` (la erosión saca nódulos) o `perHectare 6, max 24`; y nodo P2 `flint_cobbles` | [D] |
| P1 | ajustar `reed_bed`, `clay_bank` | `waterWithin` 12 y 15: la orilla (`Shore`) es una franja de < 1 m y da 2 juncales en templado 5 | [D] (medir con `resources world`) |
| P2 | `flint_cobbles` (nódulos sueltos en graveras) | `{"id":"flint_cobbles","item":"flint","kind":"Loose.Flint","ability":"Collect","capacity":2,"perGather":1,"spawn":{"biomes":["Shore","Meadow"],"perHectare":1,"waterWithin":20,"max":10,"spacing":8}}` | [D] |
| P2 | `birch_tree` (abedul: corteza) | `{"id":"birch_tree","item":"birch_bark","kind":"Plant.Bark","ability":"Gather","capacity":3,"perGather":1,"regen":1,"regenSeconds":240,"spawn":{"biomes":["Forest","Snow","Meadow"],"perHectare":2,"spacing":8,"clump":0.5}}` | [D] + [A] modelo |
| P2 | `ochre_deposit` (ocre) | `{"id":"ochre_deposit","item":"ochre","kind":"Loose.Ochre","ability":"Collect","capacity":4,"perGather":1,"spawn":{"biomes":["Rock","Dunes"],"perHectare":0.5,"max":6,"spacing":10,"clump":0.6}}` | [D] |
| P2 | `mussel_bed` (mejillonera / concheros) | `{"id":"mussel_bed","item":"mussel","kind":"Loose.Shellfish","ability":"Collect","capacity":6,"perGather":2,"regen":2,"regenSeconds":180,"spawn":{"biomes":["Shore"],"waterWithin":3,"perHectare":6,"spacing":5}}` (la concha, subproducto: ver 4.6) | [D] |
| P3 | `obsidian_outcrop` | `item: obsidian`, `Mine`, Rock/Snow, máx. 3: bien de comercio (`trade.values.obsidian 4`) | [D] |
| P3 | `native_copper` (cobre nativo) | `Collect`, Rock, máx. 2: cuentas martilladas en frío (puente a la era 3) | [D] |
| P3 | `bee_nest` (miel), `mushroom_ring` (setas, otoño) | comidas raras de estación; la miel con riesgo (`Safety` −) | [D] + [C] S |

La pesca (peces) y las aves son de la vía social; aquí solo los aparejos (4.3).

### 4.2 Objetos

| P | Id | Esbozo | Tipo |
|---|---|---|---|
| P1 | `flint_knife` (cuchillo / lasca retocada) | `{"category":"tool","tags":["Item.Tool.Knife"],"weight":0.3,"stack":1,"socket":"RightHand"}` | [D] |
| P1 | `digging_stick` (palo de cavar) | `{"category":"tool","tags":["Item.Tool.DiggingStick"],"ability":"Collect","weight":0.8,"stack":1,"socket":"RightHand","fuel":1}` | [D] |
| P1 | `hazelnut` / `roasted_hazelnut` | `Item.Food.Nut`, 0,05 kg, pila 40, `shelfLife` 28 800 / `Item.Food.Nut.Roasted`, 57 600 | [D] |
| P1 | `tuber` / `roasted_tuber` | `Item.Food.Root`, 0,3, pila 20, 3600 / `Item.Food.Root.Cooked`, 2400 | [D] |
| P1 | `dried_meat` (cecina) | `Item.Food.Meat.Dried`, 0,2, pila 20, 14 400 | [D] |
| P1 | `bone` → **a `era1`** | hoy solo en el ampliado; la costura y los subproductos de caza lo necesitan en el paquete base | [D] (mover) |
| P1 | `sinew` (tendón) | `Item.Resource.Sinew`, 0,05, pila 20 (de la caza mayor) | [D] |
| P1 | `bone_needle` (aguja de hueso) | `Item.Tool.Needle`, 0,02, pila 10 | [D] |
| P1 | `fur_cloak` (capa de pieles) | `{"category":"clothing","tags":["Item.Clothing.Cloak"],"weight":2.0,"stack":1,"warmth":0.5}`: reduce a la mitad la pérdida de `Warmth` en la mochila o puesta | [D] + **[C] S** (ropa que abriga) |
| P1 | `fat` (grasa) | `Item.Resource.Fat`, 0,3, pila 20, `shelfLife` 7200, `fuel` 2 | [D] |
| P2 | `fur_boots`, `hide_tunic` | `Item.Clothing.*`, `warmth` 0,2 / 0,3 (se suman hasta 0,8) | [D] + [C] |
| P2 | `birch_bark`, `pitch` (brea), `charcoal` | `Item.Resource.Bark` 0,2 / `Item.Resource.Adhesive` 0,1 pila 20 / `Item.Resource.Charcoal` 0,3 `fuel` 3 | [D] |
| P2 | `antler` (asta), `feather` (pluma, de las aves de la vía social) | `Item.Resource.Antler` 0,8 / `Item.Resource.Feather` 0,01 pila 50 | [D] |
| P2 | `ochre` | `Item.Resource.Pigment.Ochre`, 0,5, pila 20 (la vía social lo usa en ritos) | [D] |
| P2 | `spear_thrower`, `dart` | `Item.Weapon.SpearThrower` 0,4 / `Item.Weapon.Ammo.Dart` 0,3 pila 6 | [D] + [C] (alcance) |
| P2 | `bow`, `arrow` | `Item.Weapon.Bow` 0,8 / `Item.Weapon.Ammo.Arrow` 0,05 pila 20 | [D] + [C] (proyectiles) |
| P2 | `harpoon`, `fish_net`, `fish_trap` | `Item.Tool.Fishing.Harpoon` 0,4 / `Item.Tool.Fishing.Net` 2,0 / `Item.Trap.Fish` 3,0 (colocable) | [D] + [C] |
| P2 | `snare` (lazo) | `Item.Trap.Snare`, 0,1, pila 10, `ability":"Place"` | [D] + [C] M (trampa pasiva) |
| P2 | `waterskin` (odre) | contenedor de agua: `{"category":"container","tags":["Item.Container.Water"],"bagSlots":0,"waterStack":10}` | [D] + [C] S |
| P2 | `mussel`, `shell`, `shell_beads` | `Item.Food.Shellfish` 0,1 pila 20 `shelfLife` 600 / `Item.Resource.Shell` / `Item.Ornament.Beads` 0,05 (`trade.values.shell_beads 4`) | [D] |
| P3 | `grinding_stone` (mano y molino) | `Item.Tool.Quern`, 6,0 kg, pila 1 | [D] |
| P3 | `flour`, `flatbread`, `acorn_meal` | `Item.Food.Flour` 7200 (no se come) / `Item.Food.Bread` 4800 / `Item.Food.Meal` 9600 | [D] |
| P3 | `polished_axe` (hacha pulimentada) | `Item.Tool.Axe.Polished`, `power` 1, `speed` 1,3 | [D] |
| P3 | `dugout_canoe` (piragua) | `Item.Vehicle.Canoe`, 40 kg | [D] + [C] L (agua navegable) + [A] |
| P3 | `torch`, `stone_lamp` | `Item.Light.*`: alumbra de noche, menos `Safety` perdido | [D] + [C] S |

**Valor nutritivo** (P1, [C] S): un campo opcional `nutrition` por objeto (por defecto `Needs.FoodHunger` = 25): baya 10, avellana 8, tubérculo 20,
asado 30, carne 25, carne asada 40, cecina y ahumada 30, pan 35, mejillón 8. Sin él, cocinar solo alarga la vida útil y no hay motivo para preferir nada.

### 4.3 Recetas

| P | Id | Entradas → salida | Estación / herramienta | Tecnología |
|---|---|---|---|---|
| P1 | `digging_stick` | 1 `branch` → 1 | a mano | `stone_tools` (conocida) |
| P1 | `flint_knife` | 1 `flint` → 2 | a mano | `flint_knapping` |
| P1 | `stone_axe` (cambio) | `fiber 3, stone 2, branch 2` (antes `wood 2`) | `workbench` | rompe el círculo hacha↔madera |
| P1 | `roasted_hazelnut` | 4 `hazelnut` → 4 | `hearth` | `cooking` |
| P1 | `roasted_tuber` | 1 `tuber` → 1 | `hearth` | `cooking` |
| P1 | `dried_meat` | 2 `meat` → 1 | `drying_rack` | `food_storage` (deja de ser «idea») |
| P1 | `bone_needle` | 1 `bone` → 2 | a mano, `tool: Item.Tool.Knife` | `sewing` |
| P1 | `fur_cloak` | 3 `hide` + 1 `cordage` (o 1 `sinew`) → 1 | a mano, `tool: Item.Tool.Needle` | `sewing` |
| P2 | `hide_tunic`, `fur_boots` | 1 `leather` + 2 `hide` + 1 `sinew` / 2 `hide` + 1 `cordage` | aguja | `sewing` |
| P2 | `charcoal` | 2 `wood` → 3 | `hearth` | `adhesives` |
| P2 | `pitch` | 3 `birch_bark` → 1 · alternativa: 2 `resin` + 1 `charcoal` → 1 | `hearth` | `adhesives` |
| P2 | `spear_thrower` | 1 `antler` (o 1 `wood`) → 1 | `workbench`, cuchillo | `spear_thrower` |
| P2 | `dart` | 2 `branch` + 1 `flint` + 1 `pitch` → 3 | `workbench` | `spear_thrower` |
| P2 | `bow` | 1 `wood` + 1 `sinew` (o 2 `cordage`) → 1 | `workbench`, cuchillo | `archery` |
| P2 | `arrow` | 2 `branch` + 1 `flint` + 1 `pitch` + 1 `feather` → 6 (microlitos: un sílex da varias puntas) | `workbench` | `archery` |
| P2 | `harpoon` | 1 `antler` (o 1 `bone`) → 1 | cuchillo | `fishing` |
| P2 | `fish_net` | 6 `cordage` + 2 `stone` (pesas) → 1 | a mano | `fishing` |
| P2 | `fish_trap` (nasa) | 6 `branch` + 4 `reed` + 1 `cordage` → 1 | a mano | `fishing` (requiere `basketry`) |
| P2 | `snare` | 1 `cordage` + 1 `branch` → 2 | a mano | `trapping` |
| P2 | `waterskin` | 2 `leather` + 1 `cordage` → 1 | aguja | `sewing` |
| P2 | `shell_beads` | 4 `shell` → 1 | cuchillo | `ornaments` |
| P2 | `torch` | 1 `branch` + 1 `fat` (o `pitch`) → 1 | a mano | `adhesives` |
| P3 | `grinding_stone` | 1 `granite` (o 3 `stone`) → 1 | a mano, pico | `plant_processing` |
| P3 | `flour` | 3 `grain` → 2 | `tool: Item.Tool.Quern` (o estación `quern`) | `plant_processing` |
| P3 | `flatbread` | 2 `flour` + 1 `water` → 2 | `hearth` | `plant_processing` |
| P3 | `acorn_meal` | 4 `acorn` + 1 `water` → 2 (lavado) | `quern` | `plant_processing` |
| P3 | `polished_axe` | 1 `granite` + 1 `branch` + 1 `cordage` → 1 | `workbench` (afilar con agua y arena: lento, `seconds` ×3) | `ground_stone` |
| P3 | `dugout_canoe` | 2 `log` + 2 `wood` (quemado) → 1 | `hearth` + azuela | `boat_building` |

### 4.4 Tecnologías

Las 20 de PROGRESION se mantienen. Las nuevas encajan en sus ramas:

| P | Id | Requiere | Desbloquea | Descubrimiento (exp/obs/ens/s) | Pista de descubrimiento |
|---|---|---|---|---|---|
| P1 | `sewing` Sewing | `hide_working`, `bone_working` (mover a `era1`) | `bone_needle`, `fur_cloak`, `waterskin`; luego `hide_tunic`, `fur_boots`; edificio `skin_tent` | 0,12 / 0,3 / 0,25 / 25 | más probable en otoño e invierno con `Warmth` baja ([C] S: `discovery.season`) |
| P1 | (añadidos) | — | `digging_stick` en `stone_tools`; `flint_knife` en `flint_knapping`; asados en `cooking`; `dried_meat` en `food_storage` | — | [D] |
| P2 | `adhesives` Birch pitch | `fire_making`, `cooking` | `charcoal`, `pitch`, `torch` | 0,1 / 0,3 / 0,25 / 25 | accidente: corteza de abedul en la hoguera ([C] M, «accidente» de CONOCIMIENTO) |
| P2 | `spear_thrower` Spear-thrower | `spear_making`, `adhesives` | `spear_thrower`, `dart` | 0,08 / 0,3 / 0,2 / 30 | tras varias cazas fallidas (presa que huye) |
| P2 | `archery` Bow and arrow | `spear_thrower`, `cordage` | `bow`, `arrow` | 0,05 / 0,3 / 0,2 / 40 | lenta; se difunde mucho por observación |
| P2 | `trapping` Snares | `cordage`, `basketry` | `snare` | 0,15 / 0,3 / 0,25 / 20 | observar a un zorro cazar conejos ([C] M, evento → técnica) |
| P2 | `fishing` Fishing gear | `cordage`, `bone_working`, `basketry` | `harpoon`, `fish_net`, `fish_trap`; edificio `fish_weir` | 0,12 / 0,3 / 0,25 / 25 | vivir a < 30 m del agua (bonificación) |
| P2 | `ornaments` Shell and bone ornaments | `flint_knapping` | `shell_beads` | 0,15 / 0,3 / 0,25 / 20 | — |
| P2 | `pigments` Ochre | `fire_making` | molienda de ocre (la consume la vía social) | 0,15 / 0,3 / 0,25 / 20 | calentar ocre amarillo da rojo |
| P2 | `dog_taming` Taming wolves (idea) | `cooking` (y un cazador en el clan) | perro (4.6) | 0,03 / 0,2 / 0 / 60 | solo con lobos que rondan el campamento ([C] L) |
| P3 | `plant_processing` Grinding | `stone_tools`, `tillage` (o `food_storage`) | `grinding_stone`, `flour`, `flatbread`, `acorn_meal`; edificio `quern_stone` | 0,1 / 0,3 / 0,25 / 30 | haber recogido > 20 `grain_seed` |
| P3 | `ground_stone` Polished stone | `plant_processing` | `polished_axe` | 0,06 / 0,25 / 0,2 / 40 | — |
| P3 | `boat_building` Dugout canoes | `ground_stone`, `fire_making` | `dugout_canoe` | 0,03 / 0,3 / 0,2 / 60 | — |
| P3 | `sedentism` Year-round village (idea, puerta) | `food_storage`, `wattle_and_daub` | edificio `pit_house` y la puerta de la era 2 (4.10) | 0,05 / 0 / 0,3 / 60 | un año entero sin migrar |

Árbol añadido (sobre el de PROGRESION):

```
hide_working ─┬─ sewing ── (skin_tent, ropa, odre)
bone_working* ┘
fire_making ── cooking ── adhesives ─┬─ spear_thrower ── archery
spear_making ───────────────────────┘
cordage + basketry ─┬─ trapping
                    └─ fishing (+ bone_working) ── (fish_weir)
flint_knapping ── ornaments
tillage / food_storage ── plant_processing ── ground_stone ── boat_building
food_storage + wattle_and_daub ── sedentism ═══> puerta de la era 2
cooking + caza ── dog_taming (idea)
                             * mover de era1_ampliado a era1
```

### 4.5 Edificios

| P | Id | Tecnología | Fases (materiales, trabajo) | Da | Tipo |
|---|---|---|---|---|---|
| P1 | `windbreak` (paravientos) | — | pantalla (4 ramas, 2 fibra, 5 s) | `warmth` 0,2, `aura` 3: el refugio de un campamento de paso | [D] |
| P2 | `skin_tent` (tienda de pieles) | `sewing` | postes (6 ramas, 8 s) · cubierta (4 piel, 2 cordel, 10 s) | 3 camas, `rest` 0,2, `warmth` 0,45; `supersedes: ["shelter"]` | [D]; desmontable al migrar: [C] M |
| P2 | `pit_house` (casa semienterrada, natufiense/mesolítica) | `wattle_and_daub` | hoyo (2 piedra, 16 s) · postes (4 madera, 12 s) · paredes (6 ramas, 6 barro, 12 s) · cubierta (6 juncos, 2 esteras, 10 s) | 5 camas, `rest` 0,3, `warmth` 0,55; `supersedes: ["shelter","hut"]`: sustituye a la casa larga en la era 1 | [D] + [A] |
| P2 | `fish_weir` (presa de pesca) | `fishing` | estacas (8 estacas, 12 s) · nasa (1 `fish_trap`, 4 s) | produce pescado cada N s si está en la orilla ([C] M: edificio productor, `yields`) | [D] + [C] |
| P2 | `earth_oven` (horno de tierra) | `cooking` | hoyo (6 piedra, 8 s) | estación `hearth` secundaria, cocina por tandas de 4 | [D] |
| P2 | `snare_line` | `trapping` | sin obra: los lazos se colocan como objeto (`Place`) | — | [C] M |
| P3 | `quern_stone` (molino fijo) | `plant_processing` | base (2 granito, 2 piedra, 10 s) | estación `quern` | [D] |
| P3 | `lookout` (otero) | `palisade_building` | poste (3 madera, 2 cordel, 10 s) | vista ×1,5 para amenazas a 20 m (`aura`) ([C] S: provisión `sight`) | [D] + [C] |
| P3 | `canoe_landing` | `boat_building` | rampa (4 madera, 4 piedra) | punto de embarque | [C] L |

Corral, establo y granero de grano domesticado: era 2. El cementerio y los lugares de culto los diseña la vía social.

### 4.6 Especies y animales

| P | Especie | Comportamiento | Rinde | Tipo |
|---|---|---|---|---|
| P1 | **subproductos múltiples** en `hunts` | `"byproducts":{"hide":1,"bone":1}` en lugar de un solo `byproduct` | — | [C] S |
| P1 | conejo (`rabbit`) | igual; que huya del zorro (límite conocido de ECOLOGIA) | `meat 2`, `hide 1`, `bone 1` | [D] + [C] S |
| P1 | ciervo, jabalí, aves (vía social) | — | pedir a la vía social: ciervo `meat 6, hide 2, bone 2, sinew 1, fat 1, antler 1` (macho, otoño); jabalí `meat 5, hide 1, bone 1, fat 2` (lucha: `fightsBack`); ave `meat 1, feather 3` | coordinar |
| P2 | lobo (`wolf`) | manada de 2–4 (`maxAnimals` 4), come conejos y ciervos, ataca a un miembro solo en invierno y con hambre (`predators.wolf`: `prey: "weak"`, `alone` 12); ronda el campamento si hay carne en el almacén | `hide 2` (si se caza) | [D] (especie + depredador); merodeo: [C] S |
| P2 | perro (`dog`) — **primera domesticación** | camino: un lobo joven que ronda (`State.Lingering`) + `dog_taming` + darle carne 3 veces → `Animal.Dog` del clan; sigue al cazador (+30 % de alcance de detección de presas), ladra ante amenazas (`Safety` y aviso), come sobras (1 carne/día) | ninguno | [C] L + [A] (sirve el cuerpo `quadruped`) |
| P3 | uro o bisonte | rebaño, caza en grupo (`hunters` 4), peligroso | `meat 20, hide 3, bone 4, sinew 2, fat 4` | [D] |
| P3 | abejas | nido en árbol; recoger miel pica (`Safety` −30) | `honey` | [D] + [C] S |

### 4.7 Oficios, comportamientos y actitudes

**Oficios** (`roles` + `work.skills.families`). Hoy los pesos de rol son por tipo de nodo, así que los oficios de trabajos (cazar, fabricar, curar) necesitan
pesos por trabajo ([C] S, `weights: {"job:hunt": 1.0}`).

| P | Rol | Esbozo | Tipo |
|---|---|---|---|
| P1 | `hunter` (cazador) | `{"weights":{"job:hunt":1.0,"Resource.Plant":0.3},"tool":"flint_spear","toolTag":"Item.Weapon.Spear","skill":"hunting","min":1}`; resuelve «la lanza no se reparte». La caza no espera a «menos de 3 comidas por miembro»: un cazador sale cada día si hay presa (`hunts.*.routine: true`) | [D] + [C] S |
| P2 | `crafter` (tallador) | `{"weights":{"job:craft":1.0,"job:make":1.0,"Resource.Stone.Flint":0.6},"tool":"flint_knife","skill":"crafting"}`; preferido por mayores | [D] + [C] S |
| P2 | `healer` (curandero) | `{"weights":{"job:care":1.0,"Resource.Plant.Herb":1.0},"skill":"healing"}`; liga `herb_patch`, `poultice`, `healer_hut` | [D] + [C] S |
| P2 | `fisher` (pescador) | con `fishing`: nasas, red, arpón | [D] + [C] |
| P3 | `firekeeper` | hoy es aptitud; como oficio: vela nocturna | [D] |

Nuevas familias de aptitud: `hunting`, `healing`, `fishing`, `sewing` (práctica en `work.skills.practice`). [D] si el código acepta familias libres; si no, [C] S.

**Comportamientos y actitudes** (opciones de utilidad):

| P | Opción / regla | Qué hace | Tipo |
|---|---|---|---|
| P1 | `share_food` (reparto) | quien vuelve con carne da a los parientes con hambre antes de depositar; refuerza el vínculo (`relations.events.food`) y la norma `sharing`. Base de la economía del regalo | [C] S |
| P1 | provisión estacional | en otoño la existencia objetivo de comida se multiplica (`tribe.stock.winterFactor 2.5`) y la conservación (`preserveAbove`) se adelanta | [D] + [C] S |
| P1 | vestirse | en otoño e invierno el miembro coge del almacén una prenda (`Item.Clothing`) como hoy coge su herramienta | [C] S |
| P2 | división por edad | niños: solo recolectar plantas, frutos secos y hierbas a < `childRadius` (40 m) del campamento y `play`; mayores: tallar, enseñar, avivar | [D] (`society.life.childJobs`, `elderJobs`) + [C] S |
| P2 | `play` (niños) | juego cerca del campamento; sube `Company` y la velocidad de aprendizaje (`childLearn`) | [C] S + [A] |
| P2 | `night_watch` | un miembro vela el fuego de noche si hay lobos o incursiones recientes; enlaza con `fire_vigil` | [C] S |
| P2 | descubrimiento por observación de eventos | `discovery.triggers: [{"event":"predator_kill","tech":"trapping","chance":0.05}, {"event":"burn","item":"birch_bark","tech":"adhesives","chance":0.1}]`: curiosidad e innovación ligadas al mundo | [C] M |
| P2 | ronda estacional | campamento de invierno y de verano: migración planificada por estación (hoy solo por escasez) | [C] M |
| P2 | caza cooperativa | presas grandes piden `hunters` ≥ 3 y reparten entre todos | [D] |
| P3 | prestigio por regalos | dar `shell_beads` a otro clan vale más que su valor de trueque (`trade.values` × `prestige`) | [C] S |
| P3 | mesura en la caza | norma `restraint` que baja la caza cuando la especie está por debajo de la mitad (evita exterminar los conejos) | [D] (norma) + [C] S |
| P3 | marcar territorio | visitar los bordes de lo reclamado (`patrol`) | [C] S |

El luto, los ritos de paso, las creencias y las conversaciones son de la vía social.

### 4.8 Social y espiritual (solo lo que la vía social no cubre)

- **Materiales para sus ritos:** `ochre`, `shell_beads`, `feather`, `antler` y el edificio `totem` ya diseñado. Pedir a la vía social que los consuma
  (ofrendas, ajuar) y no que defina recursos nuevos propios.
- **Objetos de prestigio** como bienes de intercambio entre clanes (`trade.values`), no solo ritual.
- **Reunión estacional de clanes** (agregación del Paleolítico Superior): cuando dos clanes amistosos coinciden en otoño, la reunión dura más y casi garantiza
  boda e intercambio de técnica. Es diplomacia de H13 ([C] S); si la vía social hace fiestas, que la reutilice.

### 4.9 Metas del jugador en la era 1

Una sección `milestones` en `player.json` ([C] S: leer condiciones simples sobre contadores que ya existen) y aviso en el HUD.

| # | Hito | Condición |
|---:|---|---|
| 1 | Primer fuego | hoguera encendida |
| 2 | Herramienta propia | fabricar un hacha o un pico |
| 3 | Mochila mayor | cesta o bolsa de cuero |
| 4 | Primera caza | matar una presa |
| 5 | Cocinero | 10 comidas cocinadas |
| 6 | Pasar el primer invierno | ningún miembro muerto de hambre o frío en un invierno |
| 7 | Vestido | capa de pieles puesta |
| 8 | Casa | levantar una cabaña o casa semienterrada |
| 9 | Despensa | 40 comidas conservadas en el almacén al empezar el invierno |
| 10 | Maestro | enseñar 3 técnicas a otros |
| 11 | Amigo de otro clan | pacto o boda con otro clan |
| 12 | Cazador a distancia | cazar con propulsor o arco |
| 13 | Compañero | domesticar un perro |
| 14 | Primera siembra | cosechar un campo |
| 15 | **Umbral del Neolítico** | cumplir la puerta de la era 2 (4.10) |

### 4.10 La puerta de la era 2

Qué debe existir y cumplirse (sección nueva `eras` en el paquete, `gate` con condiciones; [C] S–M en H17):

| Condición | Valor propuesto | Por qué |
|---|---|---|
| Sedentarismo | el clan no ha migrado en 1 año y tiene ≥ 2 viviendas permanentes (`hut` o `pit_house`) | Natufiense |
| Excedente | al empezar el invierno, comida conservada ≥ 6 × miembros en almacenes con `keep` < 1 (`cache_pit` o `granary`) | almacenamiento antes de domesticar |
| Cereal | ≥ 3 cosechas de `grain` y `plant_processing` conocida | siega, molienda, siembra |
| Tecnologías | `tillage`, `food_storage`, `plant_processing`, `sedentism` | |
| Población | ≥ 12 miembros | el excedente sostiene más bocas |
| Opcional | `dog_taming` | la domesticación empieza por el perro |

Al cumplirse, se pueden descubrir las tecnologías de la era 2: **domesticación de cabras y ovejas** (corral), **cultivo de grano domesticado** (más
rendimiento), **cerámica y horno** (ya diseñados) y **adobe**. La fundición de cobre, a la era 3.

## 5. Comprobaciones de coherencia

### 5.1 Cadenas imposibles o rotas hoy

| # | Problema | Efecto | Arreglo |
|---:|---|---|---|
| 1 | Los 7 nodos de `era1_ampliado` no tienen `spawn` ni `patch` | grano, bellotas, resina, hueso, sal, granito y troncos no aparecen en el mundo: `tillage` y el campo dependen de semillas inexistentes; `bone_pickaxe` y `plank` no se pueden hacer | 4.1 P1 [D] |
| 2 | `rock` y `flint_outcrop` piden pico; `tree` y `old_tree`, hacha; las herramientas de piedra piden `stone` y `wood` | solo funciona por el `given` del paquete (lo que hay tirado el primer día); un clan nuevo o que pierde sus herramientas tras migrar se bloquea | `loose_stones`, `deadfall`, `stone_axe` con ramas [D] |
| 3 | Pedernal finito: 3 afloramientos de 5 sin regeneración en templado 5 (≈15 unidades) | hacha 2, pico 3, lanza 1, hoz 1 por miembro: con 2 clanes de 8 se agota en días; flechas y cuchillos lo empeoran | regeneración lenta, `flint_cobbles`, comercio (`trade.values.flint` 2 ya existe) |
| 4 | La orilla es una franja de < 1 m: 2 juncales y 6 bancos de arcilla en templado 5 | el junco va en 10 edificios y recetas (refugio 4, almacén 4, cabaña 8, casa larga 10 + esteras, granero 6, ahumadero 4, silo 4, curandero 6, cesta 5); con todo lejos, expediciones constantes | `waterWithin` mayor; medir |
| 5 | Pieles solo de conejos (máx. 8, 1 piel cada uno, no crían en invierno) | cuero 2 pieles, bolsa 4, forja 2 y capa 3: la rama de pieles se ahoga | subproductos múltiples y caza mayor (vía social) |
| 6 | `sewing` (propuesta) dependería de `bone_working`, que está en el paquete opcional | el validador fallaría en `era1` solo | mover `bone` y `bone_working` a `era1` y `bone` como subproducto de caza |
| 7 | La caza usa `fists` y solo se publica con menos de 3 comidas por miembro | la lanza del carril no sirve y la carne llega tarde, cuando ya hay hambre | oficio `hunter` y caza rutinaria |
| 8 | `SeasonSeconds` = 240 s, pero un año de la sociedad son 4 días de 1200 s = 4800 s | caben 20 estaciones en un año de edad; un invierno dura una quinta parte de un día; la siembra de grano (fases de 90 s, solo primavera y verano) apenas cabe | alinear: estación = `daysPerYear` × día / 4 (1200 s) o leer la estación del año de la sociedad ([C] S, y reequilibrar regeneración) |
| 9 | Todas las comidas llenan +25 | cocinar, ahumar o moler no alimenta más | `nutrition` por objeto [C] S |
| 10 | `copper_vein` solo en Rock y Snow | en templado no hay cobre: bien para la era 3, pero el expedicionario sigue buscándolo (bucle observado en PLAN_IA_ENTRENABLE, anomalía 1) | no pedir cobre en la era 1 (no publicar `gather copper_ore` sin `smelting`) |
| 11 | Los nodos con estación (avellana, setas) no existen como concepto | sin código se recogen todo el año salvo en invierno | `seasons` por nodo [C] S |

### 5.2 Equilibrio

- **Demanda de comida:** el hambre baja de 100 a 0 en 900 s y una comida da 25, así que un miembro come ≈ 5,3 veces por día de 1200 s. Un clan de 8 necesita
  ≈ 43 comidas por día. Si el invierno pasa a durar un día (punto 8), en él no rebrota nada y los conejos no crían: hacen falta **≈ 43 comidas conservadas por
  cada 8 miembros** (y con `nutrition`, menos unidades de más valor). Eso es justo la lógica mesolítica (almacenar en otoño) y el hito 9.
- **Demasiados recolectores:** el recolector es el rol por defecto y el de los niños; sin cazador ni tallador, la revisión de oficios solo reparte entre
  tres. Con la propuesta: 1 cazador cada 4–5 miembros, 1 tallador cada 8, 1 curandero desde 10.
- **Conejos contra clan:** comen las mismas plantas (bayas, fibra). Con 8 conejos y 2 zorros por mapa, la caza de conejos no sostiene un clan: la carne
  tiene que venir de la caza mayor de la vía social.
- **Ritmo de tecnologías:** PROGRESION espera las primeras en 1–3 días y las ideas lentas en semanas. Las nuevas P2 (0,05–0,15) caen en ese rango;
  `archery`, `dog_taming` y `boat_building` (0,03–0,05) son de semanas, a propósito. Falta el barrido de semillas (H20) con 4, 8 y 20 miembros.
- **Pedernal y comercio:** con pedernal escaso, el clan pobre en sílex debe comerciar: es deseable, pero comprobar que `PlanBarter` lo ofrece.

### 5.3 Animaciones y visuales para las acciones nuevas

| Acción | Clip que sirve hoy | Falta (lista del artista) |
|---|---|---|
| Recoger cantos, leña muerta, mejillones | `collect_dig`, `gather_low` | agacharse y recoger del suelo (ya pedido, punto 3) |
| Cavar raíces con palo | `garden_dig` | — |
| Cascar o tostar avellanas | `act_kneel` | golpear con piedra sentado |
| Tallar cuchillo, agujas, asta | `act_knap` (dig ×0,8) | tallar sílex sentado (ya pedido, punto 6) |
| Coser | `act_bench` o `act_kneel` | coser sentado: nuevo |
| Raspar pieles | `act_kneel` | raspar una piel tendida: nuevo |
| Brea, carbón, tostar | `act_crouch` junto al fuego | avivar en cuclillas (pedido, punto 10) |
| Lanzar con propulsor, arrojar lanza | `sword_attack_2` (estocada) | **lanzamiento por encima del hombro**: nuevo (H14 ya lo pide) |
| Tirar con arco | `bow_draw`, `bow_recoil`, set `bow_aim` | — (hay clips; falta el código de proyectiles) |
| Pescar con arpón / red / nasa | `sword_attack_2` hacia abajo / `garden_pull_1` / `build_place` | arponear en el agua, echar la red: nuevos |
| Poner lazos | `garden_sow` | — |
| Moler grano | `act_bench` | moler de rodillas (vaivén): nuevo |
| Jugar (niños), velar el fuego | idle y gestos / `act_crouch` | correr en corro, perseguir: nuevo (H9) |
| Dar de comer al perro | `gather_low` | — |
| Ponerse la capa | — | prop: capa en el `look` del personaje ([A] ropa por era) |

Modelos: prendas de piel (visibles en el personaje), aguja y cuchillo como props, propulsor, arco y carcaj, red y nasa, perro y lobo (cuerpo `quadruped`
con color), abedul y avellano (variantes de `TreeFactory`), mejillonera, ocre, casa semienterrada y tienda de pieles por fases (`visuals.buildings`),
montones nuevos (`Item.Food.Nut`, `Item.Resource.Hide`, `Item.Resource.Bone`).

## 6. Plan de implementación recomendado

Cinco carriles de contenido. No tocan el árbol ni los 16 edificios de PROGRESION (solo los referencian) ni los ritos, creencias, interacciones, comidas y
cazas de la vía social (solo les piden subproductos). Siguiendo la memoria del proyecto: cada carril pasa solo los tests de su área; el `ctest` completo al final.

| # | Carril | Alcance | Tamaño | Depende de |
|---:|---|---|---|---|
| 1 | **Coherencia y arranque** | `spawn` de los 7 nodos del ampliado; `loose_stones`, `deadfall`, `flint_cobbles`; ajustes de pedernal, junco y arcilla medidos con `resources world` en templado 5, alpino 3 y desierto 2; `stone_axe` con ramas; mover `bone` y `bone_working` a `era1`; etiquetas de era (cobre a la 3, adobe y sal a la 2, casa larga a la 2); `nutrition` por objeto [C]; alinear estaciones y año [C]; no buscar cobre sin fundición | S–M | carril de edificios fusionado |
| 2 | **Subsistencia mesolítica** | avellano, raíces, palo de cavar, cuchillo, asados, cecina, mejillones y conchas; nodos de estación [C]; subproductos múltiples de caza [C]; oficio `hunter` con la lanza y caza rutinaria [C]; `share_food` [C]; provisión de otoño [C]; hitos 1–6 | M | 1; coordinar con la vía social (presas y subproductos) |
| 3 | **Ropa, costura y adhesivos** | `sewing`, aguja, capa, túnica, botas, odre; ropa que abriga y vestirse [C]; `adhesives` (corteza de abedul, brea, carbón, antorcha); `skin_tent`, `pit_house`, `windbreak`; pistas de descubrimiento por estación [C]; hitos 7–9 | M | 1, 2 (pieles y tendón) |
| 4 | **Caza a distancia, trampas y pesca** | propulsor y dardos, arco y flechas (proyectiles [C], clips de arco existentes), lazos como trampa pasiva [C], arpón, red, nasa y `fish_weir` productor [C], lobo como especie y depredador, oficios `fisher` y `crafter`; descubrimiento por eventos [C]; hito 12 | L | 2, 3 (brea); en paralelo con 3 desde que exista `adhesives` |
| 5 | **Puente a la era 2 y su puerta** | `plant_processing` (molino, harina, pan, harina de bellota), `ground_stone`, `sedentism`, `quern_stone`; perro (`dog_taming`, merodeo y doma) [C L]; reunión estacional de clanes; sección `eras` con la puerta [C]; `milestones` del jugador [C]; hitos 10–15; barrido de semillas del ritmo hasta la puerta | M (L con el perro) | 1–4 |

Orden: **1 → 2 → (3 ∥ 4) → 5**. El carril 1 es pequeño y desbloquea a todos; el 2 es el que más cambia cómo se ve la era (qué come y hace la gente);
el 3 y el 4 se reparten bien porque solo comparten la brea; el 5 cierra la era con una puerta medible. El arte (ropa, lanzamiento, coser, moler, perro)
se pide al empezar el carril 2 para que llegue a tiempo para el 3 y el 4.
