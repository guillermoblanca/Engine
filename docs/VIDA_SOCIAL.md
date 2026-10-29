# Vida social: conversaciones, ritos, creencias, comida variada y caza

Una rama de la civilización que no es trabajo: lo que el clan hace **juntos** cuando no recoge ni fabrica. Hablar junto al fuego, contar
historias, consolar a quien ha perdido a alguien, despedir a los muertos, celebrar el invierno y la primera caza, creer en algo que da sentido
a lo que pasa (una tormenta, un eclipse, una buena cosecha) y comer bien y variado. Encima de lo que ya había: necesidades y cerebro de
utilidad (H5/H6, [IA.md](IA.md)), relaciones, personalidad y cultura (H9/H10, [SOCIEDAD.md](SOCIEDAD.md)), conocimiento (H11,
[CONOCIMIENTO.md](CONOCIMIENTO.md)), clanes (H13, [CLANES.md](CLANES.md)), caza (H14, [CONFLICTO.md](CONFLICTO.md)), ecología (H3,
[ECOLOGIA.md](ECOLOGIA.md)) y economía (H18, [ECONOMIA.md](ECONOMIA.md)).

[← Volver al README](../README.md) · Plan: [PLAN_SOCIEDAD_VIVA.md](PLAN_SOCIEDAD_VIVA.md) · Árbol: [PROGRESION.md](PROGRESION.md)

Lógica pura de CPU, determinista (cada «dado» es un hash de la semilla, nombres y un contador) y casi todo en datos: la sección **`social`** de
`assets/data/social.json` (un fichero nuevo del paquete `era1`), más objetos, nodos, recetas, tecnologías, especies, cacerías y visuales en el
mismo fichero (secciones que ya existían y que suman por id).

**Encendido por los datos.** Una tribu sin reglas sociales (el `TribeConfig` de siempre, el de los tests de los otros sistemas) no tiene vida
social: `SocialRules::On` es falso. La sección `social` de un fichero la enciende (salvo `"on": false`) y `DefaultSocialRules()` también. Así
los tests de antes no cambian y el editor y el modo de juego, que cargan el paquete `era1`, la tienen.

## 1. Piezas

| Pieza | Dónde | Qué es |
|---|---|---|
| `SocialRules`, `LoadSocialRules`, `DefaultSocialRules` | `GB/Game/Social.h`, `SocialRules.cpp` | Las reglas como datos (sección `social`) |
| `SocialState` | `GB/Game/Social.h`, `Social.cpp` | Ánimo y duelo de cada uno, dieta, conversaciones, agenda de ritos, creencias, lugar sagrado, trampas; guardado |
| Ganchos en `Tribe` | `GB/Game/TribeSocial.cpp` (una línea en `Update`, `Decide`, `Execute`, `Finish`, `Eat`, `Die`, `ToJson`/`FromJson`) | Opciones `talk`, `rite`, `snare`; los ritos del clan; omen, tabúes |
| Caza ampliada | `HuntRule::Yields`, `Group` en `GB/Gameplay/Combat.h`; `TribeConflict.cpp` | Despiece, cacerías en grupo con papeles, tabú del lugar sagrado |
| Creencias entre clanes | `GB/Game/ClansBeliefs.cpp` (una línea en `ClanWorld::Greet`) | Las creencias se contagian y las compartidas dan confianza |
| Editor | `Editor/src/EditorSocial.cpp` | `social`, `rite`, `belief`, `hunt`; sección **Social life** del panel Tribe |

## 2. Ánimo y duelo

Cada miembro tiene un **ánimo** (0..1, empieza en `mood.start`) y un **duelo** (0..1). El ánimo vuelve despacio a su punto de reposo
(`mood.rest` menos `mood.grief` × duelo) y lo mueven los hechos: una buena charla, un rito, una comida variada, un buen augurio; una comida
monótona, un mal augurio, una muerte cercana. El ánimo escala las opciones del cerebro como una norma (`mood.effects`, factor
`1 + fuerza × (2v − 1)` con el factor `mood` en el inspector): alegre trabaja más y descansa menos; abatido, al revés. El duelo nace con la
muerte de un pariente (`grief.kin`) o un amigo (`grief.friend` × afinidad), se apaga con los días (`grief.decayPerDay`) y lo alivian el consuelo
y el funeral.

## 3. Conversaciones (interacciones como comportamiento)

La opción **`talk`** existe cuando el miembro está libre o descansando y hay alguien a menos de `talk.radius` metros también libre. Vale
`talk.score` × la urgencia de compañía × sociabilidad (y más al atardecer junto al fuego, por la agenda). Al elegirla los dos van a un punto
entre ambos y hablan `seconds` segundos; al acabar se aplican los efectos del **tipo** de conversación, que se elige de forma determinista por el
contexto, en este orden:

| Tipo | Cuándo | Efecto (datos) |
|---|---|---|
| `console` | el otro está de duelo | −duelo, +vínculo, +ánimo de ambos |
| `settle` | su vínculo es negativo (una disputa) | +vínculo grande (zanjar la disputa) |
| `share` | lleva comida y el otro tiene hambre | le da una unidad; evento `food` |
| `play` | los dos son niños | +ánimo, +vínculo |
| `teach` | sabe una tecnología que el otro no (H11) | `Instruct` con `teach` de comprensión |
| `story` | es mayor (o muy sociable) y es de noche o al atardecer | +ánimo y un poco de comprensión al oyente |
| `greet` | hace `greetAfter` segundos que no se veían | +vínculo pequeño, saludo con la mano |
| `groom` | parientes o muy amigos | +compañía grande, +vínculo |
| `gossip` | ambos sociables y hay un tercero que conocen | +vínculo entre ellos, − con el tercero |
| `chat` | lo demás | +vínculo, +compañía, +ánimo pequeños |

Todos suben la necesidad `Company` de los dos (`company`). Cada tipo tiene su gesto (actividad `talk.<tipo>` del manifiesto de animación).

## 4. Ritos y ritos de paso

Un rito es dato (`social.rites`): un **disparador** (`event`: `birth`, `adult`, `pair`, `death`, `first_kill`, `wounded`, `harvest`, una
`tradition`; o una estación con `season` y `at`, la fracción de la estación), un **lugar** (`place`: `fire`, `totem`, `sacred`, `camp`), cuánto
dura (`seconds`), cuánto se espera a que lleguen (`gatherSeconds`), qué parte del clan debe asistir (`quorum`), su **coste** (`cost`: objetos
del almacén; `food` = unidades de cualquier comida) y sus **efectos** en quienes asisten: ánimo, vínculo entre todos (cohesión), duelo,
deriva de normas (`norms`), creencias (`beliefs`), curación (`heal`). Una tecnología (`tech`) puede hacer falta.

El clan lleva una **agenda** de ritos pendientes. Cuando un rito vence y no hay otro en curso, empieza: paga el coste (si no puede, se aplaza
`retry` segundos; a la tercera, se celebra humilde con la mitad de efecto), elige el sitio y llama a todos los vivos que no estén luchando, en una
incursión o una expedición. La opción **`rite`** (puntuación alta, por debajo de una emergencia) lleva a cada uno a su sitio en el **círculo**
alrededor del lugar (ángulo por orden) y allí espera hasta el final. Al final, los que están en el círculo reciben los efectos; el registro
lo cuenta («the clan holds a funeral for Biel at the sacred place: 5 of 6 attend»). Un rito nunca dura más de `gatherSeconds + seconds`.

Ritos de era 1: `naming` (nacimiento), `coming_of_age` (adulto), `pairing` (pareja), `funeral` (muerte; alivia el duelo), `solstice`
(invierno), `spring_feast` (primavera), `harvest_feast` (cosecha del campo o fin del otoño), `first_hunt` (primera caza de un miembro),
`healing` (un herido grave; con chamán). Las **tradiciones** de H10 que nacen se vuelven ritos recurrentes cada `traditionDays` días
(`tradition_rite` con el nombre de la tradición).

## 5. Creencias y espiritualidad

- **Sistemas de creencias** (`social.beliefs.systems`): `animism` (los espíritus de los animales y los lugares), `ancestors` (los espíritus de
  los antepasados) y `sky` (el cielo y las estaciones). Cada clan tiene una fuerza 0..1 de cada uno; empieza en `start` ± `spread` (hash de la
  semilla y el nombre del clan) y **deriva**: cada rito suma `drift` × su peso en ese sistema (`honours`). La creencia **dominante** es la más
  fuerte.
- **Tabúes** (`taboos`), activos cuando su sistema pasa de `holdAbove`: `sacred_hunt` (no se caza a menos de `radius` del lugar sagrado: el
  tablón no publica la cacería y la presa se descarta) y `share_kill` (la primera pieza de un cazador va entera al almacén y todo el clan la
  agradece: evento `food` con el cazador).
- **Augurios** (`omens`): sucesos del mundo leídos por el clan: `storm` (la tormenta de mitad de verano), `eclipse` (raro: una probabilidad al
  día por hash), `good_harvest` (una cosecha del campo), `big_kill` (una pieza grande), `two_deaths` (dos muertes en un día). Cada uno mueve el
  ánimo de todos, las creencias y puede pedir un rito (`rite`).
- **Lugar sagrado**: el clan adopta uno el primer día: el tótem si hay uno; si no, un punto alcanzable a `sacred.distance` del campamento
  (ángulo por hash). Se guarda; los ritos con `place: sacred` van allí.
- **Chamán** (papel espiritual): derivado cada día como un oficio: el adulto vivo con más `curiosity` y `sociability` ponderadas
  (`shaman.traits`) más la edad (`shaman.age` por año) y los ritos que ha presidido. Preside los ritos (efectos × `shaman.bonus`) y el de
  curación solo se hace con él. No es un rol de trabajo: no le quita su oficio.
- **Entre clanes** (H13): cada saludo entre miembros de dos clanes acerca sus creencias (`spread` × apertura) y la **similitud** de las
  creencias (1 − la media de las diferencias) suma confianza y actitud (`trust` × (2 × similitud − 1)): creencias compartidas, más confianza;
  opuestas, recelo.

## 6. Comida variada

Objetos nuevos en datos (`social.json`), con su caducidad (H18) y su nodo:

| Comida | Grupo | De dónde | Vida (s) |
|---|---|---|---:|
| `roots` Raíces y tubérculos | `root` | nodo `root_patch` (cavar a mano; prado y bosque) | 7200 |
| `nuts` Frutos secos | `nut` | nodo `nut_tree` (bosque; rebrota despacio) | 14400 |
| `mushrooms` Setas | `fungus` | nodo `mushroom_ring` (bosque) | 1200 |
| `greens` Hierbas silvestres | `green` | nodo `wild_greens` (prado) | 900 |
| `eggs` Huevos | `egg` | nodo `bird_nest` (bosque y orilla) | 2400 |
| `honey` Miel | `sweet` | nodo `bee_tree` (bosque, pocos) | 0 (no caduca) |
| `fish` Pescado | `fish` | nodo `fishing_spot` (orilla, junto al agua), con nasa `fish_trap` (`Item.Tool.Fishing`) | 900 |
| `stew` Guiso | `dish` | `stew`: 1 carne + 2 raíces + 1 hierbas silvestres en el hogar (`cuisine`) → 3 | 3600 |
| `roast` Asado | `dish` | `roast`: 2 carne + 1 grasa en el hogar (`cuisine`) → 2 | 2400 |

La torta de grano (`flatbread`) queda pendiente: el grano de `era1_ampliado` está diferido (el validador no lee los cultivos) y una receta que lo
pida haría fallar la carga estricta. Pescar pide la nasa (`Item.Tool.Fishing`): la vida social la pide al plan de producción (`SetDemand`)
cuando el clan conoce un `fishing_spot` y nadie lleva una, y la opción **`gear`** hace que un miembro la coja del almacén; con ella en la mochila
el cerebro de siempre considera el nodo (y el hambre también).

**Nutrición** (`social.diet.foods`): lo que sacia cada comida (sustituye al valor fijo de las necesidades para esa comida). **Variedad**: cada
miembro recuerda los grupos de sus últimas `window` comidas; con `varied` grupos distintos o más, cada comida da `bonus` de ánimo y `health` de
salud; con uno solo (ventana llena), `monotony` (negativo).

## 7. Caza de más animales

- **Especies nuevas** (`species` en `social.json`): ciervo (`deer`, cuadrúpedo grande, rápido, pace en manada), jabalí (`boar`, se defiende con
  colmillos), urogallo (`grouse`, ave pequeña). Pescado: un nodo, no un animal. **Fauna inicial**: `social.hunting.wildlife` dice cuántos de
  cada especie suelta el editor al hacer `tribe build` alrededor del campamento.
- **Cacerías** (`combat.hunts`, suman por id): cada una con su **despiece** (`yields`: carne, piel, hueso, tendón, grasa, plumas) y, las
  grandes, **en grupo** (`group`: la persecución no empieza hasta que haya tantos cazadores con el trabajo; el más audaz **ojea** (`driver`) y
  los demás esperan con la lanza (`spear`), en el registro). El jabalí se revuelve (`fightsBack`) y hiere: combate de H14.
- **Trampas**: la opción `snare` pone un lazo (cuesta `cost`, tecnología `trapping`) cerca de donde se vio una presa pequeña; a los `arm`
  segundos atrapa al animal de su etiqueta que pase a menos de `radius` (la ecología lo retira y lo cuenta como cazado) y el que lo puso, u otro,
  lo recoge (`check`). Trampas de foso (`pit`) para jabalí y ciervo con la misma mecánica (más coste, más tiempo).
- **Sobrecaza**: la ecología cría con su máximo y su enfriamiento; cazar más deprisa de lo que crían vacía la especie cerca del campamento y el
  tablón deja de publicar esa cacería (no hay presa a la vista); sin caza, vuelve a crecer.
- **Oficios**: el tendón (`sinew`) da cordel (`sinew_cordage`, `butchery`), la grasa (`fat`) el asado y arde (combustible 1), el hueso
  (`bone`) es el de `era1_ampliado` (el pico de hueso) y las plumas (`feather`) quedan para las flechas.

## 8. Árbol: la rama social y espiritual

Encima de `ritual` (tótem) y `cooking` (hogar) de [PROGRESION.md](PROGRESION.md), en un bloque aparte en `social.json`:

| Tecnología | Requiere | Da |
|---|---|---|
| `storytelling` (idea) | — | la conversación `story` (las historias enseñan un poco de lo que sabe el que cuenta) |
| `shamanism` (idea) | `ritual`, `herb_lore` | el rito de curación (`healing`, con chamán) |
| `trapping` | `cordage` | `snare` (lazo); las trampas de lazo y de foso |
| `fishing` | `basketry` | `fish_trap` (nasa) |
| `butchery` | `flint_knapping` | `sinew_cordage` (2 tendones → 2 cordel) |
| `cuisine` | `cooking` | `stew` (guiso) y `roast` (asado) en el hogar |

## 9. Visuales

Actividades del manifiesto (`assets/animation/mixamo.manifest.json`, clips del pack completo): `talk.chat` (asentir, gesto alegre), `talk.story`
(gesto pensativo, asentir largo), `talk.console` (suspiro, asentir), `talk.settle` (negar, asentir), `talk.greet` (saludo con la mano), `talk.play`
(gesto alegre, salto), `rite.*` (de rodillas en el funeral, brazos al cielo en el solsticio, alegría en la fiesta), `snare` (agacharse). El
círculo del rito se ve porque cada uno está en su sitio del círculo mirando al centro. Animales nuevos con cuerpo procedimental (`quadruped`
con color y tamaño: ciervo, jabalí; `rabbit` pequeño para el urogallo); el zorro con `Fox.glb` como hasta ahora. Props de comida en
`visuals.json` (montones de raíces, frutos secos, pescado).

## 10. Guardado, editor y QA

- La sección `ai` de la tribu lleva `social` (opcional): ánimo, duelo, dietas, agenda y rito en curso, creencias, lugar sagrado, chamán,
  trampas, primeras cazas y contadores. Una partida anterior carga sin nada de eso.
- Editor: `social status [nombre]`, `rite list | start <rito> [sujeto]`, `belief status`, `hunt status`; sección **Social life** del panel Tribe.
- QA (`SimInvariants`, `social`): ánimo y duelo en 0..1; los asistentes de un rito están vivos; ningún rito dura más de su máximo; el almacén
  nunca negativo; las trampas apuntan a animales de su etiqueta.

## 11. Tests

`tests/social_test.cpp` (test `social`): la sección dice lo mismo que el código, va y vuelve y un error la deja intacta; el paquete `era1`
carga estricto con `social.json`; las conversaciones cambian vínculos y ánimo de forma determinista; tras una muerte hay funeral y baja el
duelo; el festival de invierno reúne al clan; el tabú cambia la caza; la variedad de la dieta sube el ánimo; una cacería en grupo mata un
ciervo y el despiece da sus objetos; la sobrecaza vacía la especie; dos clanes con creencias compartidas ganan confianza; guardado a mitad de
un rito; y los invariantes de `sim_sweep` pasan.
