# GB Engine – guía para agentes

- C++20, CMake. Objetivo: Windows y Linux. Namespace `GB`, macros `GB_*`.
- Build y tests: ver README. Antes de commitear: configurar con `-DGB_WARNINGS_AS_ERRORS=ON`, compilar y pasar `ctest`.
- Usar `#include "GB/..."` con `/` (nunca `\`).
- No versionar binarios ni salidas de build (ver `.gitignore`). Dependencias vía FetchContent con versión fija.
- Todo código nuevo del motor debe poder ejecutarse headless (sin ventana) para tests y QA visual por capturas.
- El motor es una biblioteca estática `GB`; no usar `__declspec(dllexport)`.
- **La documentación vive fuera del motor**, en `../documentation` (repo git propio, desde el 2026-11-08): GDD en `00_gdd/`, diseño en `01_diseno/`,
  docs técnicos en `02_tecnico/`, arte en `03_arte/` (enlaza `../art-book`), producción, QA y estado de los subsistemas en `04_produccion/`. Un
  comentario del código que cite `docs/X.md` se refiere a ese fichero allí (búscalo por nombre). `docs/` solo tiene un README que apunta allí.

## Edificios y tecnologías (contenido por datos)

- Un edificio nuevo se escribe en **tres sitios que los tests obligan a coincidir**: `assets/data/era1.json` (`buildings`) = `DefaultBuildings()` en
  `Engine/src/GB/Gameplay/Buildings.cpp` (packs_test, settlement_test); la tecnología que lo abre en `era1.json` (`techs[].buildings`) = `builds(...)` en
  `Engine/src/GB/Gameplay/Techs.cpp` (knowledge_test); y su modelo por fases en `assets/data/visuals.json` (`buildings.<id>`; el test de visuales pide uno
  por cada id de `DefaultBuildings()`, presupuesto 20 000 triángulos).
- Varias tecnologías que nombran un edificio son un **O**, no un Y (`Tribe::ClanKnowsBuilding`: basta con saber una). Para exigir dos sin tecnología nueva,
  que una lo abra y que sus fases pidan un intermedio de la otra (la atalaya pide estacas de `palisade_building`): el planificador solo cuenta como pagable
  lo que da una receta conocida. Una tecnología nueva también hay que duplicarla en `DefaultTechs()`.
- Los efectos son solo provisiones genéricas (`shelter`, `rest`, `warmth`, `station`, `store`, `storeCap`, `keep`, `defense`, `healing`, `culture`, `aura`,
  `supersedes`); nada de código por edificio. `defense` se suma entre edificios; `keep` cuenta el menor de los que están en pie (> 0).
- Objetos y recetas de `era1.json` no tienen copia en C++, pero uno nuevo arrastra reachability, montones y props: evitarlos si un intermedio existente sirve.
- Al añadir o cambiar edificios, actualizar `../documentation/01_diseno/PROGRESION.md` (§3 tabla y árbol, §4 tabla y recuento) y el recuento de `../documentation/02_tecnico/gameplay/ASENTAMIENTOS.md`.

## Piezas visuales (`visuals.json`)

- Los visuales se cargan en tiempo de ejecución: se pueden iterar con el Editor Release ya compilado, sin recompilar.
- `cylinder` crece desde su base (`size` = diámetro abajo, alto, diámetro arriba; arriba 0 = cono); `box` y `blob` van centrados.
- Rotación en grados, orden Ry·Rx·Rz (yaw, pitch, roll). Con roll 90 un cilindro se tumba hacia −x desde su `pos`.
- Copias: `row` va a lo largo de X centrado; `stack` sube; `ring` empieza en el ángulo 0 y con `face` la X local de cada copia mira hacia fuera y la Z es la
  tangente (así `pos`/`rot` se dan en ese marco: un anillo de `radius` 0 sirve para repetir una pieza girada). El `rot` de la pieza **no** gira la fila.
- La textura `clay` es naranja de por sí: para barro neutro, compensar con un color azulado (material `daub`). La textura `stone` hace que el barro parezca mampostería.

## Renders de validación

- Desde `Engine/`: `build/Editor/Release/Editor.exe --hidden --quality=medium "--exec=generate temperate 5 0 0; time 10; building <id> phases -12 10; camera 0 22 40 <x> ground+0.5 10; wait 4; screenshot ../documentation/img/edificios/<id>_phases.png; quit"`.
- `generate ... 0 0` no quita los árboles (vienen del paquete de bosques). En la semilla 5, la fila en z=10 desde x=-12 está despejada; el lago está en +z (≈ z 60).
- `camera <yaw> <pitch> <distancia> x y z`: yaw 0 = cámara en +z mirando a −z. Subir el pitch (≈22) saca de cuadro el arbusto cercano a la cámara.
- Varias obras del mismo edificio en una escena necesitan nombre propio (`building <id> 99 1 x z <Nombre>`); sin él, la segunda sustituye a la primera.
- `screenshot` acepta rutas con `..` pero no crea carpetas: la de destino (en `../documentation/img/...`) tiene que existir.
- Abrir cada PNG y mirarlo antes de darlo por bueno.

## Interfaz del juego

- Skill del proyecto `gb-game-ui` (`D:/__game_engine__/.claude/skills/gb-game-ui/`): diseño y auditoría de la UI del modo `play` (HUD, paneles,
  barra de acciones, noticias, selección, bocadillos) con el estilo de era 1 (piel, pergamino, ocre, sacado de `assets/ui/icons.png`). Captura
  todos los estados sin ventana con `scripts/capturas_hud.txt` + `shot_ui`, mide el contraste de los tokens con `scripts/ui_colors.py`, y su
  auditoría base del 2026-11-06 está en `references/auditoria_2026-11-06.md`.
- La UI de juego se pinta con su tema solo mientras dibuja (push/pop); el editor conserva el suyo. Los botones solo encolan `game <acción>`, y el
  texto de `game status` lo leen los tests (`Editor/tests/play.txt`): cambiar cómo se dibuja, no ese texto.
- El modo clan (`play`) dibuja solo el juego a pantalla completa (`DrawGameScreen`; el dock del editor queda vivo y oculto, consola en `game console`).
  En el modo libre, `observer` (botón «Clans») muestra cada clan con el mismo estilo (`ObserverView.cpp`). Tema en `Editor/src/GameTheme.h`.
- La UI del juego y del observador está en español: todo texto visible pasa por `Lang` (`Editor/src/Lang.h`, `assets/ui/lang/es.json`, solo Latin-1;
  `lang en|es`, `lang missing`, `lang check`). Las órdenes, sus respuestas, los registros y `game status` siguen en inglés, porque los leen los tests.

## Revisión de arquitectura

- **Prioridad: los arreglos de arquitectura van antes que las funcionalidades nuevas.** El plan está en `../documentation/02_tecnico/arquitectura/ARQUITECTURA_REVISION.md` §7 (mapa
  interactivo en `../documentation/02_tecnico/arquitectura/mapa.html`). Se hace en su orden, del paso 1 (`GB/Scene/`) en adelante, un corte por cambio, sin mezclar cambios
  de comportamiento. Cada corte verifica: la arista desaparece en `gb_deps.py`, compila con `-DGB_WARNINGS_AS_ERRORS=ON` y `ctest -C Release` queda
  igual que la línea base.
- Mientras el plan no esté terminado, el código nuevo **no empeora el grafo**: no añade aristas que suban de capa ni puertas de CPU a tipos de GPU
  (`Mesh`, `Texture`, `Renderer`...), ni entra en un ciclo. Comprobarlo con `gb_deps.py` antes de dar el trabajo por bueno, y si es inevitable, decirlo.
- Una funcionalidad que choca con un paso pendiente (por ejemplo, otro backend de render antes del paso 5, o tocar `Terrain`/`Model` con GPU dentro)
  espera a ese paso o lo hace primero.
- Skill del proyecto `gb-architecture-review` (`D:/__game_engine__/.claude/skills/gb-architecture-review/`): mide el grafo de includes por
  módulo con `scripts/gb_deps.py` (ciclos y qué corte libera cada módulo, puertas de CPU a tipos de GPU, cabeceras centrales, pins de CMake),
  verifica cada hallazgo en el código y propone cortes con plan. `--cut FROM:TO` simula un corte antes de hacerlo. La medición de referencia
  del 2026-11-03 está en su `references/cortes.md`.

## Pendiente

- **Atalaya (`watchtower`) y despensa de tinajas (`jar_store`)** (2026-11-03, ../documentation/01_diseno/PROGRESION.md §4.1): datos, C++ y visuales escritos y renders en
  `../documentation/img/edificios/`. Compila en Release y `ctest -C Release` pasa salvo los fallos ya conocidos (`editor_*`, `golden_images hills_grass_trees`).
  Falta: compilar con `-DGB_WARNINGS_AS_ERRORS=ON` y la Debug, y ver en una partida larga (o QA de simulación) que el planificador los levanta.
- `settlement_test` comprueba el número exacto de edificios (`file.size() == 18`): subirlo al añadir uno.
- Vasijas como objeto (contenedor del almacén) hechas en el horno: hoy las tinajas de la despensa son ladrillo cocido y arcilla.
- Huecos del árbol sin edificio: `fishing`, `trapping`, `butchery`, `shamanism`, `storytelling` (../documentation/01_diseno/PROGRESION.md §6.8). Corral (sin animales de cría) y pozo de piedra.
- Los edificios no son obstáculos de navegación; la empalizada con puerta espera a que la navegación sepa de puertas.
- **Arquitectura (prioritario, ver «Revisión de arquitectura»)**: revisión del 2026-11-03 en `../documentation/02_tecnico/arquitectura/ARQUITECTURA_REVISION.md` (dos ciclos de módulos,
  datos con recursos de GPU, `GB` enlaza glad/glfw PUBLIC; plan por pasos y la vía para backends Vulkan/D3D). Sin código aún: empezar por el paso 1
  (`GB/Scene/`) y responder las preguntas de §8.
- **IA de las tribus** (2026-11-04): evaluación medida en `../documentation/04_produccion/qa/REVISION_IA_TRIBUS.md`. Primero F1 (los pozos de los clanes extra de `SimQA.cpp`
  no tienen sistema de gameplay: invalida los barridos con varios clanes) y F3 (no hay forma de buscar agua; la sed no entra en Maslow). Luego F2
  (la exploración busca tipos de nodo que no existen), F4 (las obras esperan materiales durante días) y F5 (casi no se experimenta). El `GBSim`
  Release del 2026-11-04 se cae al arrancar: las medidas se hicieron con el Debug del 11-02.
- **Murallas en spline con puertas por clan y área reclamada**: diseño en `../documentation/01_diseno/MURALLAS.md` (decidido: trazado por IA y a mano; el área da defensa
  solo dentro, robo agravado, suelo de territorio y subáreas por uso; las puertas abren al clan y a los aliados y cierran en incursión). Sin código:
  empezar por la fase M1 (§12).
