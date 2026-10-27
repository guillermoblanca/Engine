# Partidas guardadas y repeticiones (H4)

Módulo `GB/Save` (`Engine/src/GB/Save/`), puro CPU: se ejecuta sin ventana y sin contexto GL. Los tests están en `tests/save_test.cpp` y las
partidas de versiones antiguas en `tests/golden/saves/`. El editor lo usa con los comandos de más abajo.

## Qué guarda una partida

Un archivo `<nombre>.gbsave` (JSON) con:

| Sección | Contenido |
|---|---|
| `format`, `engine` | versión del formato (entero) y del motor |
| `world` | semilla y parámetros de generación (`type`, `seed`, `size`, `resolution`, `river`, `lake`, `forest`, `trees`, `rocks`); `generated: false` si no hay mundo generado |
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

## Versiones y migraciones

`kFormatVersion` es el formato que escribe esta versión del motor (hoy **1**). Política:

- Cada cambio de la disposición del archivo sube `kFormatVersion` y añade **un paso** a `BuiltinMigrations()` (`From = N`, convierte el texto
  JSON de la versión N en el de N+1 y pone `format`). Las migraciones se encadenan v0→v1→v2... y nunca se borran.
- Un archivo con versión mayor que la del motor se rechaza (“guardado por una versión más nueva”). Uno más viejo se migra al cargar (aviso en el
  registro) y al guardar se escribe en el formato actual.
- Añadir un campo opcional con valor por defecto no necesita subir la versión; cambiar el significado de uno, sí.
- **Cada versión deja su partida en `tests/golden/saves/`** (`vN_*.gbsave`) y el test `Migrations` la carga con el código actual. Cuando se
  suba la versión: no regenerar la anterior, añadir la nueva con `GB_UPDATE_GOLDEN=1` y retocar el test que compara byte a byte la actual.
- v0 es un archivo de escena de `SceneIO` (sin `format`): `scene.json` se importa como una partida sin mundo generado
  (`tests/golden/saves/v0_scene.json`).

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

## Límites conocidos de la v1

- Las habilidades **en curso** no se guardan (se avisa); se restauran canceladas. Guardar con el personaje quieto.
- `CharacterController` no expone su velocidad: un personaje que camina se restaura con su entrada pero sin inercia.
- La edición de terreno todavía no existe como dato: irá como sección cuando haya un registro de ediciones.
- Los cuerpos del ragdoll se guardan y se cargan en `ActorState`, pero lo que los lee y los aplica es el sistema de actores.
- El mundo generado debe ser reproducible: si cambia el generador, las partidas viejas se rechazan (o se cargan con aviso) hasta decidir.
