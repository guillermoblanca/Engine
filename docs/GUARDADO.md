# Partidas guardadas y repeticiones (H4)

Módulo `GB/Save` (`Engine/src/GB/Save/`), puro CPU: se ejecuta sin ventana y sin contexto GL. Los tests están en `tests/save_test.cpp` y las
partidas de versiones antiguas en `tests/golden/saves/`. El editor lo usa con los comandos de más abajo.

## Qué guarda una partida

Un archivo `<nombre>.gbsave` (JSON) con:

| Sección | Contenido |
|---|---|
| `format`, `engine` | versión del formato (entero) y del motor |
| `world` | semilla y parámetros de generación (`type`, `seed`, `generator`, `size`, `resolution`, `river`, `lake`, `forest`, `trees`, `rocks` y, si hay nodos de recurso repartidos, `wild`); `generated: false` si no hay mundo generado |
| `baseline` | identidad del mundo que genera la semilla (nº de objetos y hash), para detectar que el generador ya no produce lo mismo |
| `clock` | tic de simulación (pasos fijos de `dt`), hora del día, duración del día, días, si el cielo sigue la hora |
| `weather` | preset del clima |
| `entities` | **solo la diferencia** con el mundo generado: `removed` (ids), `changed` y `added` (entidades como las escribe `SceneIO`), y `nextId` |
| `gameplay` | por sistema de habilidades: efectos con tiempo (enfriamientos, regeneración) con su reloj, usos de cada habilidad y habilidades completadas |
| `actors` | por actor: posición, giro, set de animación, props en la mano, inventario, entrada que mantiene y, si hay, los cuerpos del ragdoll (posición, rotación, velocidades) |
| `sections` | bloques con nombre y versión propia que registra cada sistema (inventarios, ediciones de terreno, clanes...) |
| `checksum` | FNV-1a de 64 bits del resto del archivo |

Los atributos, etiquetas y habilidades concedidas viajan en el `GameplayComponent` de la entidad (`SceneIO`); `gameplay` añade lo que ese
componente no tiene. **El archivo no lleva fecha**: la fecha es la del fichero. Por eso guardar, cargar y guardar produce los mismos bytes.

### Guardado diferencial

`CaptureGame` pide al anfitrión el mundo que genera la semilla (`BuildBaseline`) y guarda la diferencia por id. Cargar es lo contrario
(`ApplyGame`): se genera el mundo, se aplica la diferencia y se valida todo **antes** de tocar nada; si algo falla (JSON dañado, checksum,
un efecto o actor desconocido, entidades que no enlazan, un generador que cambió) la carga devuelve el error y el mundo queda como estaba.
Si el generador cambió a propósito, `LoadOptions::AllowBaselineDrift` aplica la diferencia con un aviso.

En el editor los árboles y rocas son entidades cuyos ids dependen de lo hecho antes, así que `EditorSaveHost` las renumera como las numera
la semilla (el resto de la escena va en la sección `user-entities`, numerada desde 1). Un árbol talado es un id en `removed`.

**Los nodos de recurso repartidos** (`resources world <densidad>`, ver [RECURSOS_MUNDO.md](RECURSOS_MUNDO.md)) también salen de la semilla: la densidad va
en `world.wild` y `BuildBaseline` los vuelve a planificar con `PlanWorldResources` sobre el mundo generado y los datos de nodos cargados. Así la partida
solo lleva los nodos que cambiaron (recogidos, agotados, movidos) y no unos cientos. Su raíz `World resources` lleva la etiqueta `world`, como los
grupos de árboles y rocas: `generate` la quita con el mundo viejo. Cargar otro contenido (`content load`) olvida la línea base en caché; si los datos
nuevos planifican otros sitios, la partida se ve como un generador que cambió (deriva de la línea base, ver arriba).

**La línea base se genera sin GL** (`WorldGenerator::GenerateData`: alturas, río, lago, mapa de biomas y colocación en CPU), así que una partida con
mundo generado se valida y se aplica también sin ventana.

## Versiones y migraciones

`kFormatVersion` es el formato que escribe esta versión del motor (hoy **2**). Política:

- Cada cambio de la disposición del archivo sube `kFormatVersion` y añade **un paso** a `BuiltinMigrations()` (`From = N`, convierte el texto
  JSON de la versión N en el de N+1 y pone `format`). Las migraciones se encadenan v0→v1→v2... y nunca se borran.
- Un archivo con versión mayor que la del motor se rechaza (“guardado por una versión más nueva”). Uno más viejo se migra al cargar (aviso en el
  registro) y al guardar se escribe en el formato actual.
- Añadir un campo opcional con valor por defecto no necesita subir la versión; cambiar el significado de uno, sí.
- **Cada versión deja su partida en `tests/golden/saves/`** (`vN_*.gbsave`) y el test `Migrations` la carga con el código actual. Cuando se
  suba la versión: no regenerar la anterior, añadir la nueva con `GB_UPDATE_GOLDEN=1` y retocar el test que compara byte a byte la actual.
- v0 es un archivo de escena de `SceneIO` (sin `format`): `scene.json` se importa como una partida sin mundo generado
  (`tests/golden/saves/v0_scene.json`).
- v1 → v2: el mundo guarda la **versión del generador** (`world.generator`, ver `GB/World/WorldGenVersion.h`). Toda partida v1 se hizo con el
  generador 1, así que la migración pone `generator: 1`; ese generador se conserva bit a bit y la semilla sigue dando los mismos árboles y rocas
  (`tests/golden/saves/v1_*.gbsave`). Un mundo nuevo usa el generador más reciente; uno cargado conserva el suyo. Un `generator` que el motor no
  conoce se rechaza. v2 añade también `world.wild` (opcional, solo si no es 0). Partida de referencia: `tests/golden/saves/v2_world.gbsave`.

## Escritura atómica, copia y recuperación

`WriteTextAtomic`: escribe `<archivo>.tmp`, comprueba el tamaño, copia el anterior a `<archivo>.bak` y renombra el temporal encima. El destino
es siempre el archivo viejo completo o el nuevo completo. `LoadSaveGameFromFile` cae al `.bak` (con aviso) si el principal falta o está dañado.
Un `.tmp` huérfano de un corte de luz se ignora y se sobrescribe en el siguiente guardado.

`AutoSaver` es el temporizador (autoguardado cada N s en el hueco `autosave`; guardado rápido en `quicksave`).

## Repetición determinista

`ReplayRecorder` toma el estado inicial como una partida guardada y apunta cada orden (`tic`, `comando`). `PlayReplay` carga ese estado, ejecuta las
órdenes en su tic, avanza los tics y compara la **huella** final (`Digest`: una huella por parte —entidades, habilidades, actores, reloj, clima,
secciones— con los números cuantizados como `Qa::Fingerprint`, así que las diferencias minúsculas entre plataformas no cuentan). Si difiere,
dice qué partes. Archivo `.gbreplay`: JSON con las órdenes, el estado inicial (una partida completa) y la huella final.

Cualquier simulación que implemente `ISimulation` (`Step()` + `Command()`) se puede repetir: `GameSession` (headless, con huerto, habilidades,
personajes que caminan y reloj; sus comandos están en `GameSession.h`) y el editor.

## Editor

| Comando | |
|---|---|
| `save <nombre>` / `load <nombre>` | guarda/carga `saves/<nombre>.gbsave` (la carpeta se cambia con `GB_SAVE_DIR`) |
| `quicksave` / `quickload` | `F5` / `F9` |
| `saves` | lista con fecha, versión del formato, tic, mundo y tamaño; marca los dañados |
| `autosave on\|off [segundos]` | sin argumentos, el estado (por defecto cada 300 s) |
| `replay record` / `replay stop [archivo]` / `replay play <archivo>` | `replays/<archivo>.gbreplay`; `play` termina en error si la huella difiere |

`load` sigue cargando modelos glTF (`load x.glb`, también `load_model`). Menú File > Save game (quicksave), Save game as… y Load game. Todo funciona con `Editor --hidden --script=...` / `--exec`. Mientras se graba
no se puede cargar. Se graban las órdenes que cambian la simulación (`garden`, `ability`, `add`, `entity`, `set`, `time`, `weather`...).
La simulación del editor avanza en tics fijos de 1/60 s (`StepSimulation`); el reloj se guarda en tics.

## Cómo engancharse

- **Actores** (el `Actor` que sale de `PlayerMode`, los NPC): implementar `IActorStateSource` (`CaptureActors` / `RestoreActors(actors, check)`;
  con `check = true` solo valida) y darlo con `EditorSaveHost::SetActorSource` / `GameSession::SetActorSource`. Sus atributos y cooldowns ya
  viajan con su sistema de habilidades (mismo nombre que la entidad).
- **Un sistema nuevo** (inventarios, terreno editado, clanes): un `SectionBlob` con nombre y versión desde `ISaveHost::Sections()`; recibirlo en
  `LoadedGame::Sections` en `Commit` y validarlo en `Validate`.

## Secciones del editor

El editor registra estas secciones (cada una con su versión propia, hoy 1; no cambian `kFormatVersion`: un archivo sin ellas se carga con los valores de arranque):

| Sección | Contenido | Quién |
|---|---|---|
| `user-entities` | lo que no genera la semilla: huerto, actores, nodos, interactivos, conejos, hoguera | `EditorSaveHost` |
| `ai` | la tribu: almacén, hoguera, reloj, memoria y decisión de cada miembro (las necesidades son atributos) | `Tribe::ToJson` |
| `ecology` | hacia dónde iba cada conejo (centro de paseo, planta elegida, tiempo comiendo) y los contadores de nacidos, muertos y comida | `EcologySystem::ToJson` |
| `interactives` | los temporizadores de las fogatas y los pozos (la fracción de segundo que lleva ardiendo) | `InteractiveSystem::ToJson` |
| `season-look` | el clima, la nieve y el color de las hojas que escribió la estación, y si las estaciones visibles están activas | `SeasonLook` |

La estación sale del reloj (el tic se guarda), así que no necesita sección. El temporizador `Regrow` de cada nodo es un atributo y viaja con la entidad.

**Los actores** guardan además de lo anterior de qué estaban hechos (`kind`: `fox`, `cesium` o el archivo del personaje): al cargar en un mundo que no los tiene (un mundo nuevo, o una repetición que viene de otra cosa) el editor los vuelve a crear, y los que
existen y no están en la partida se quitan (la partida es toda la verdad sobre quién está). Los actores se mueven en el tic fijo (`EditorSaveHost::Step`), no con el tiempo del fotograma, cuando no hay un jugador manejando uno; por eso una repetición, una partida cargada y una ejecución a otra velocidad de fotogramas van igual.

## Repetición del editor (aceptación de H4)

Probado de extremo a extremo con el editor real sin ventana (`ctest`: `editor_replay`, `editor_replay_society`, `editor_replay_midrun`, `editor_replay_wild`; los guiones están en `Editor/tests/`, `--strict=1` hace que una línea que falla dé código de salida 1):
un mundo generado con huerto y actores con habilidades; la pequeña sociedad (tribu de cinco, interactivos y conejos); y una grabación a mitad de la marcha (conejos paseando, una fogata ardiendo, otra estación) que se reproduce **en otro mundo** y termina con la misma huella.
`editor_replay_wild` graba sobre un mundo con nodos de recurso repartidos (`resources world`) y la reproduce en otro mundo: los nodos vuelven desde la línea base de la semilla.

## Huecos reservados (sin activar)

Para las decisiones aún sin responder de `PROPUESTA_HABILIDADES_Y_ECONOMIA.md` (3: caducidad; 5: conocimiento por agente y por clan), el formato deja sitio sin cambiar de versión; todos son opcionales, se escriben solo cuando tienen valor, nada los rellena hoy y los guardados de ahora son los mismos bytes que antes:

| Hueco | Dónde | Para qué |
|---|---|---|
| `inventory[].age` (`ItemStack::Age`) | cada montón de la mochila de un actor | edad del montón de lo que caduca; **en uso desde H18** (E6, [ECONOMIA.md](ECONOMIA.md)): `ActorWorld` lo rellena desde `ItemSlot::Age` |
| `actors[].knowledge` (`ActorState::Knowledge`) | cada actor | lo que sabe ese agente como nombres; **en uso desde H11**: `tech.<id>` por tecnología ([CONOCIMIENTO.md](CONOCIMIENTO.md)) |
| sección `knowledge-clan` | `sections` | lo que sabe el clan, lo medio aprendido, los intentos y el reloj de lecciones; **en uso desde H11** (el editor la escribe si hay tribu) |

H11 activó los dos de conocimiento: el editor rellena `knowledge` de los miembros de la tribu en `EditorSaveHost::Actors` y lee ambos en `Commit` tras `RestoreTribe`; una partida sin ellos carga con las tecnologías del principio. H18 activó el de la caducidad: `ItemSlot::Age` se guarda en las mochilas (este hueco) y en los inventarios de la tribu (tercer número del montón, dentro de la sección `ai`), y la sección `ai` lleva además `economy` (campos, almacenes secundarios, contadores); todo opcional. Si una decisión cambia la disposición (por ejemplo, caducidad por casilla en vez de por montón), sube `kFormatVersion` con su migración como siempre.
`tests/golden/saves/v1_slots.gbsave` es un guardado con los tres huecos usados: debe seguir cargándose y reescribiéndose idéntico.

## Números en los archivos

Un `float` se escribe como el texto más corto que vuelve a leerse como el mismo número (`0.1`, `134.02432`): una escena guardada y cargada vuelve **idéntica bit a bit** (con 7 cifras, una coordenada cerca de un límite de redondeo cambiaba de cubeta en la huella de una repetición y la repetición fallaba). La identidad del mundo generado (`baseline`)
sigue calculándose con el formato de 7 cifras de siempre (`SceneFloats::Short7`) para que las partidas viejas sigan coincidiendo.

## Límites conocidos de la v1

- Las habilidades **en curso** no se guardan (se avisa); se restauran canceladas, sin la etiqueta `State.Busy` que ponían. Una partida guardada a mitad de un trabajo sigue **sin ser idéntica paso a paso**: quien estaba haciendo algo lo vuelve a decidir. Guardar (y grabar una repetición) con los personajes quietos da resultados idénticos.
- `CharacterController` no expone su velocidad: un personaje que camina se restaura con su entrada pero sin inercia.
- La edición de terreno todavía no existe como dato: irá como sección cuando haya un registro de ediciones.
- Los cuerpos del ragdoll se guardan y se cargan en `ActorState`, pero lo que los lee y los aplica es el sistema de actores.
- El mundo generado debe ser reproducible: **cada cambio de lo que sale de una semilla va tras una versión nueva del generador** (`kWorldGeneratorLatest`),
  y las anteriores conservan su camino de código. Un cambio sin versión nueva lo detecta la identidad de la línea base (la carga se rechaza o, con
  `AllowBaselineDrift`, se aplica con aviso).
