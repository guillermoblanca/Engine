# Jugar (H15)

Eres **un miembro más de un clan**. Los demás viven solos (recogen, comen, duermen, aprenden, se emparejan, luchan, comercian) y tú haces lo
mismo con las mismas habilidades, pero decides tú. El clan te cuenta: comes de su almacén, duermes en una cama de sus refugios, coges trabajos de
su tablón, y lo que aportas sube tu **posición** en el clan; con posición alta el clan **te sigue** cuando propones algo.

[← Volver al README](../README.md) · Plan: [PLAN_SOCIEDAD_VIVA.md](PLAN_SOCIEDAD_VIVA.md) (H15)

## Arrancar

```bash
Editor --play                                # el mundo y el clan por defecto (sección controls.game de player.json)
Editor "--play=alpine 7 5 3"                 # mundo, semilla, miembros por clan, clanes
Editor --script=play.txt                     # el mismo arranque desde un guion (play.txt, en la raíz del repositorio)
```

Dentro del editor: `play [temperate|alpine|desert] [semilla] [miembros] [clanes]` o **View > Play the game**. `play off` vuelve al editor
(el mundo y los clanes se quedan).

`play` genera el mundo, reparte los recursos salvajes, funda tu clan donde dice el elector de sitio (H8), pone los demás clanes lejos (H13), te
da un cuerpo (el **X Bot** de `packs/full`; sin el paquete, el zorro) junto al campamento como miembro del clan 0, pone la **cámara de juego** que te
sigue (H19), activa la **capa visual** (props en las manos, sets y montajes de trabajo, igual que a los NPC) y esconde los paneles del editor
(la consola se queda). La calidad es `auto`: empieza en `low` y baja a `minimum` en cuanto un fotograma tarda más de `autoMs` (40 ms), para GPU lentas.

## Controles

Están en datos: la sección **`controls`** de `assets/data/player.json` (teclas con los nombres de Dear ImGui; una tecla vacía la desactiva).

| Tecla | Acción | Comando |
|---|---|---|
| WASD, Mayús, ratón | Andar, correr, girar la cámara | – |
| E | Lo que indica el aviso de abajo: recoger del nodo cercano, avivar la hoguera, sacar agua del pozo, trabajar en una obra, hablar con alguien, dejar lo que llevas en el almacén, comerciar en el campamento de otro clan, devolver un golpe | `game interact` |
| F | Comer (lo que llevas o, en el campamento, del almacén) | `game eat` |
| R | Beber el agua que llevas | `game drink` |
| G | Dejar en el almacén todo lo que no es herramienta | `game deposit` |
| Z | Dormir (en tu cama de un refugio, si hay) / levantarse (andar también despierta) | `game sleep` |
| C | Coger el trabajo más urgente del tablón que puedes hacer | `game claim` |
| I | **Mochila**: lo que llevas (con «eat» en la comida), las recetas que puedes fabricar (Mayús muestra las que aún no) y el almacén del clan (botón «take 1») | `game inventory` |
| T | **Gente**: los miembros por distancia con qué hacen y qué sienten por ti; botones hablar, regalar, enseñar, aprender y casarse (hay que estar al lado); los otros clanes, con comerciar y visitar | `game people` |
| B | **Edificios**: los que el clan sabe levantar (y los que aún no), lo que dan, lo que cuestan, y «Propose here» para proponerlo donde estás; debajo, las obras del clan | `game build` |
| X / Q | Atacar al más cercano (o al que te ataca) / bloquear | `game attack` / `game block` |
| Tab / J | Panel del clan: miembros (qué hacen y qué piensan de ti), tablón (con botón «take»), almacén, conocimiento, propuestas, otros clanes | `game clan` / `game jobs` |
| P | Pausa | `game pause` |
| = / - | Más rápido / más lento (×1, 2, 4, 8, 16, 20) | `game faster` / `game slower` |
| F5 / F9 | Guardar / cargar (ranura `play`) | `game save` / `game load` |
| V | Cambia entre la vista tras el jugador y la vista cenital (WASD mueve, Q/E giran, Z/X acercan; E e interactuar no actúan desde arriba) | `game view [top\|follow]` |
| Esc | Cierra el panel o la ayuda abiertos; si no hay ninguno, sale al editor (el cuerpo del jugador se queda en el mundo) | `game quit` / `play off` |
| H | Ayuda | `game help` |

En el juego **Q/E no giran la cámara** (E es interactuar): giran las flechas ←/→ o el ratón. El overlay del clan (campamento con su almacén, edificios y obras con lo que les falta, campos, y «YOU» sobre el jugador) se ve también en el juego; se apaga en el panel Tribe (`overlay off`).

Abajo a la izquierda hay una **barra de acciones** con un botón por cada acción (con su tecla entre corchetes): hacen lo mismo que las teclas.

Lo que no tiene tecla se hace desde la consola con `game`:

```
game take <objeto> [n]                 coger del almacén (comida libre; lo demás baja un poco la posición)
game give <miembro> <objeto> [n]       regalar (sube el vínculo; comida y herramientas valen el doble)
game talk [miembro]                    qué hace, por qué (su mente), su trabajo, su aptitud y qué siente por ti
game teach|learn <miembro> [técnica]   enseñarle una tecnología que sabes, o que te enseñe una suya (H11)
game marry <miembro>                   pareja, si te quiere lo bastante (H9: los hijos vienen solos)
game propose gather|make <obj> [n]     proponer trabajo al clan; propose build <edificio> (donde estás)
game craft <receta>                    fabricar con tu mochila (en el campamento, con el almacén)
game attack [enemigo] | block | flee   pelear (H14): el más cercano o el que te ataca; bloquear; huir hacia el campamento
game trade|visit <clan>                en el campamento de otro clan: trueque de lo que os falta y sobra, o visita
game claim <trabajo> | release         un trabajo concreto del tablón (gather wood, hunt Animal.Rabbit...) / dejarlo
game status                            el estado del HUD como texto (los tests lo leen)
```

## La interfaz

- Arriba a la izquierda: **necesidades** en barras (hambre, sed, descanso, calor, vida: la lista es `hud.needs`), y debajo tu posición en el clan,
  tu **estado** (vida, aguante y heridas de H14, con quién peleas), lo que **llevas** (y tus herramientas), tu **trabajo** del tablón con lo hecho,
  el **reloj** (día, hora, estación, velocidad).
- Abajo: el **aviso** de lo que hace E ahora.
- Abajo a la derecha: las **noticias** del clan (lo que te concierne y lo grande: muertes, nacimientos, incursiones, tradiciones, propuestas cumplidas).

## Qué hacer

1. **Ayudar al clan**: recoge lo que pide el tablón (C para coger un trabajo; E en un nodo; G en el campamento). Cada unidad entregada, y más si es
   de tu trabajo, sube tu posición.
2. **Vivir**: come (F) antes de que el hambre te deje sin fuerzas, bebe, duerme de noche junto a la hoguera o en un refugio.
3. **Ganarse al clan**: regala, habla, enseña. Con vínculos y posición, tus **propuestas** salen adelante: que recojan madera para una obra, que
   hagan cuerda, que levanten un refugio donde estás.
4. **Liderar**: a partir de 60 de posición el clan te sigue con más facilidad (`leader` en los datos).
5. **Defender y cazar**: con un arma en la mochila (un hacha sirve) ataca a los depredadores y a los que asaltan el almacén; caza (coge un trabajo
   `hunt`) para traer carne.
6. **Salir**: ve al campamento de otro clan y comercia (`game trade Blue`): mejora la relación entre clanes y tu posición.

**Objetivos** (no hay un final escrito): un clan que no pasa hambre, una hoguera que no se apaga, un refugio con cama para todos, liderar el clan y
estar en paz (o en alianza) con los vecinos.

## Morir

Decisión de diseño: con `player.mortal: false` (por defecto) **no se pierde la partida**. El hambre, la sed y el frío te debilitan, pero tu vida no
baja de `floorHealth`; un golpe mortal (H14) te **derriba**: despiertas en el campamento, con las heridas curadas y un tercio de vida, los que te
atacaban te dejan, y el clan te tiene en algo menos (crédito `fall`). Con `mortal: true` mueres como cualquiera: el HUD dice **GAME OVER** y solo
quedan `game load` o `play`.

## Las reglas (datos)

`assets/data/player.json`, sección **`player`** (registrada en los paquetes; `PlayerRules` en `GB/Game/PlayerMember.h`):

- `standing`: posición inicial, máximo, umbral de líder, lo que se pierde al día y `credit` (puntos por unidad de cada aportación: `deposit`,
  `job`, `gift`, `teach`, `craft`, `stoke`, `build`, `trade`, `proposal`, `hunt`, `defend`; negativos: `take`, `fall`).
- `proposals`: cada adulto vota `base + affinity × vínculo + standing × posición/máx + leader (si lideras) + Σ culture[norma] × (2·norma − 1) − coste`;
  sale adelante con más de `majority` de votos a favor. El coste es `unitCost` por unidad pedida o `buildCost` por edificio. Determinista.
  Una propuesta aceptada de recoger o fabricar es una **demanda** de la economía (el tablón publica sus `gather` y `make`); un edificio, una **obra**.
- `giftBond`, `talkBond` / `talkSeconds`, `teachAmount`, `marryBond`, `reach`, `campReach`, `visitRadius`, `mortal`, `floorHealth`, `role`.

Sección **`controls`**: `keys`, `hud` (`notifications`, `notifySeconds`, `needs`, `clock`, `prompt`) y `game` (`world`, `seed`, `resources`,
`members`, `clans`, `model`, `fallback`, `name`, `quality`, `autoMs`, `timeScale`).

## Cómo está hecho

- El jugador es un `TribeMember` con `Player` (`GB/Game/Tribe.h`): tiene necesidades, aptitudes, persona en la sociedad, conocimiento, mochila y
  oficio; el clan lo cuenta en sus existencias, camas, nacimientos y en el tablón. **El cerebro nunca lo mueve**: `Tribe::Update` le da su turno
  (mira y escucha, cuenta al clan lo que vio en el campamento) pero llama a `StepPlayer` en lugar de decidir; nunca es abstracto; la revisión de
  oficios, los enviados, las bodas entre clanes y las partidas de incursión lo dejan fuera.
- Lo que hace llega al clan por `Tribe::Player*` (`TribePlayer.cpp`): almacén, comidas, agua, cama, regalos, tablón, trabajo de sus habilidades
  (`PlayerWorked`: aptitud, testigos que aprenden, el trabajo avanza), fabricar, propuestas, hablar, lecciones, pareja, peleas. El comercio con otro
  clan es `ClanWorld::PlayerVisit` (`ClansPlayer.cpp`).
- Se guarda en la sección `ai` de la tribu: la marca `player` del miembro y el estado `player` (posición, lo hecho, propuestas).
- El editor (`Editor/src/GameMode.cpp`) solo lee teclas, la escena y la mochila, y dibuja la interfaz.

## Tests

- `tests/player_test.cpp` (test `player`): datos (`player.json` dice lo que dice el código, ida y vuelta, errores), un jugador guiado por órdenes que
  el clan cuenta (comidas del almacén, un trabajo del tablón con sus seis unidades, aptitud al recoger, ninguna orden del cerebro en dos minutos,
  nunca abstracto), regalos que suben el vínculo, propuestas rechazadas y aceptadas de forma determinista por posición, vínculos y cultura (la
  aceptada aparece en el tablón y se cumple), lecciones, pareja, inmortal por las reglas, comercio en el campamento de otro clan, guardar y cargar
  con su pertenencia, y pelea (H14): ataque, golpe mortal que lo derriba y lo devuelve al campamento, y jugador mortal que muere.
- `Editor/tests/play.txt` (test `editor_play`): `play` sin ventana, unos minutos de juego con órdenes `game`, guardar y cargar, el HUD por texto,
  `qa check` sin violaciones y una captura `play_hud.png` en el directorio de compilación.
  En el juego los personajes siguen el tiempo del fotograma (un jugador maneja uno), así que el test se lanza con `--step=60`: cada fotograma
  son 1/60 s de juego por lento que vaya la máquina, cada `wait` es el mismo tramo de partida y la calidad automática (que mira lo que tarda el
  fotograma) no salta. Antes, en una máquina lenta, el almacén aún no tenía bayas al cogerlas y la línea esperada nunca podía salir.

## Límites conocidos

- Sin entrega directa a una obra (los materiales van al almacén y el clan los acarrea) ni órdenes directas a otros miembros aunque lideres
  (el liderazgo pesa en las propuestas).
- Un jugador inmortal no envejece hasta morir (la sociedad lo trata como a cualquiera: a los ~70 años de juego marcaría su muerte).
- El aviso de E elige lo más cercano; no hay menú radial ni selección con el ratón.
- Una sola persona: el modo de juego controla al primer jugador del clan 0.

## Ajustar las animaciones

Si una animación o una transición falla (el golpe que no encaja, un bucle que no debería serlo, un fundido brusco), no hace falta tocar código: la ventana **View > Animation Bindings**
del Editor cambia el clip de cada acción, su recorte, velocidad y mezcla, lo previsualiza y lo guarda en `assets/data/animation_bindings.json`. Ver [EDITOR_ANIMACIONES.md](EDITOR_ANIMACIONES.md).
