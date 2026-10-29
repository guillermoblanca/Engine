# Datos por individuo: captura para análisis masivo

Propuesta del 29-10-2026. Amplía el `Chronicle` (muestreo del clan cada 60 s con texto libre de eventos) hasta una captura que pueda correr en segundo plano días simulados y dejar una tabla por individuo, lista para analizar con SQL o pandas. Enlaza con H2 (simulación sin GL) y H3 (telemetría en SQLite) de `PLAN_IA_ENTRENABLE.md`; no los sustituye.

## 1. Qué se captura hoy y qué falta

Hoy: `Chronicle` guarda por muestra y miembro nombre, oficio, trabajo, detalle y las cinco necesidades; por clan, nivel de la pirámide, días de comida, fuego, camas, edificios y almacén. Los eventos son las líneas de `Tribe::Say` en texto libre.

| Área | Ya existe en el motor | Se captura | Falta |
|---|---|---|---|
| Estado físico | necesidades, posición, mochila, salud | 5 necesidades | posición (x, z, región), mochila, distancia al campamento, velocidad real |
| Decisión | `AgentMind.Options` con puntuaciones y factores, `Chosen` | nada | opciones consideradas, puntuación y factores de cada una, cambio de decisión, tiempo en cada compromiso |
| Trabajo | `Job`, `Detail`, `Made`, `Deposited`, `Meals`, `Failures` | trabajo actual | duración de cada tarea (inicio, fin, resultado, motivo de fallo), unidades recogidas y depositadas |
| Habilidades | `SkillSet` por familia, `WorkKind` | nada | nivel por familia en cada muestra; ganancia por tarea |
| Persona | `Person`: edad, sexo, padres, pareja, genes, rasgos, generación, hijos | nada | ficha fija por individuo (nacimiento, genes, rasgos) y su evolución (edad, etapa) |
| Social | afinidad entre miembros, normas, tradiciones, quejas | nada | matriz de afinidad cada N muestras; interacciones con quién y qué resultado |
| Conocimiento | `AgentKnowledge`, tecnologías, recetas | nada | tecnologías sabidas por individuo; enseñanza y aprendizaje con fecha |
| Memoria espacial | `SpatialMemory` por individuo | nada | tamaño de la memoria, nodos descubiertos, cobertura de exploración |
| Economía | almacén, stockpiles, tablón de trabajos, comercio | almacén | producción y consumo por ítem y por quién; trabajos publicados y reclamados; precio y trueque |
| Clanes | relaciones, encuentros, hurtos, pactos, territorio | nada | evento por cada encuentro, hurto, pacto, emisario; reparto de territorio |
| Salud y muerte | causa de muerte | marcador de muerto | causa, instante, necesidades en las 10 muestras previas |
| Coste | `m_Cost` (ms por parte, decisiones por fotograma) | nada | ms de IA por tick, búsquedas A* y su coste, para detectar bucles |
| Entorno | hora, estación, tiempo atmosférico | hora | clima, temperatura, estación |

Comportamientos que hoy no se pueden ver sin leer el log: bucles de tareas fallidas (se encontró uno de ~6000 eventos en 40 fotogramas), miembros que no hacen nada, hambre sin reacción, repeticiones al mismo objetivo.

## 2. Principios

1. **Eventos nunca se pierden; las muestras se pueden diezmar.** Si la cola se llena, se descartan muestras, se cuenta cuántas, y los eventos esperan.
2. **Capturar no cambia el resultado.** Con la captura activa el hash del estado de la simulación es el mismo que sin ella. Se comprueba con un test.
3. **Todo lleva `run_id`, `clan_id` y `member_id`**, aunque haya un solo clan.
4. **Esquema estable y versionado**: `schema_version` en la tabla `runs`; un cambio de columnas sube la versión.
5. **Filas planas**: sin texto libre en lo que se analiza (el texto se queda como campo `text` del evento, pero con `kind` y campos estructurados al lado).

## 3. Tablas

- `runs`: `run_id`, semilla, versión del generador, commit, perfil, fecha, duración simulada, parámetros del barrido.
- `individuals`: ficha fija. `run_id`, `member_id`, nombre, nacimiento, sexo, padres, genes (JSON), rasgos iniciales, oficio inicial.
- `samples`: una fila por individuo cada `every` s simulados (60 s por defecto, configurable). Posición, región, necesidades, trabajo, detalle, mochila (JSON), habilidades (JSON), edad, etapa.
- `clan_samples`: una fila por clan y muestra. Nivel de la pirámide, días de comida, almacén (JSON), fuego, camas, edificios, vivos, muertos, tablón (publicados, reclamados).
- `decisions`: una fila por decisión tomada (no por tick). Instante, individuo, opción elegida, opciones consideradas con puntuación y factores (JSON), motivo del cambio si sustituyó a un compromiso. Es la tabla grande: se limita con `decisions_every` o solo cuando cambia la elección.
- `tasks`: una fila por tarea terminada. Inicio, fin, tipo, objetivo, resultado (`ok`, `empty`, `unreachable`, `interrupted`), unidades.
- `events`: `kind`, `who`, `other`, `time`, campos estructurados y `text`. Incluye nacimientos, muertes, parejas, enseñanza, hurtos, pactos, comercio, construcción terminada, fuego, hambre crítica.
- `relations`: afinidad entre miembros cada `relations_every` (por defecto 1 día simulado).
- `cost`: ms de IA por tick, búsquedas de ruta y su coste.

## 4. Formato y escritura

Recomendado: **SQLite** con hilo de escritura, tal como fija H3 (amalgamación por `FetchContent` con versión fija, WAL, transacciones de ~1 s, cola acotada). Las razones: un solo fichero por corrida, abre directamente en DuckDB (que lee SQLite) y en pandas, y no añade un formato que haya que mantener.

Alternativa por si el volumen lo pide: exportar a **Parquet** desde SQLite con una herramienta de `tools/` (DuckDB `COPY ... TO`), sin meter Parquet en el motor. Así el motor no depende de una librería de columnas.

Orden de magnitud: 50 miembros, una muestra por minuto simulado y 30 días son unas 2 millones de filas en `samples`; cabe de sobra en un fichero. `decisions` es la que puede crecer: de ahí su límite.

## 5. Segundo plano

Hoy una corrida larga necesita el editor (`--hidden --script=...`), que exige GL. Para dejar una simulación trabajando sola hace falta H2:

- `GBSim`: ejecutable sin ventana (`GBSim --seed=N --days=30 --clans=3 --telemetry=run.db --profile=...`), con progreso por consola, reanudación desde un guardado y parada limpia (Ctrl+C cierra la base sin corromperla).
- Mientras llega H2: el mismo `TelemetrySink` sirve al editor oculto, y los tests con cuerpos simulados (`sim_sweep`) ya corren sin GL, así que se puede empezar a medir con ellos.
- Barridos: N semillas × M perfiles, uno por proceso; cada corrida escribe su propio `.db` y una herramienta los une para el análisis.

## 6. Entrega en pasos

1. **`TelemetrySink` y esquema** con `runs`, `individuals`, `samples`, `clan_samples` y `events` estructurados. Test del hash con y sin captura; test de cola llena (se pierden muestras, no eventos). El `Chronicle` pasa a ser una vista de lo que escribe el sink.
2. **Tareas y decisiones**: `tasks` y `decisions`, con límite configurable. Es lo que permite medir bucles y repeticiones.
3. **Social y conocimiento**: `relations`, enseñanza, normas, clanes (encuentros, hurtos, pactos, comercio).
4. **`GBSim`** sin GL y reanudación (H2), y el comando `telemetry start|stop` y el argumento `--telemetry` en el editor.
5. **Análisis**: `tools/analyze_run.py` con DuckDB; el dashboard lee de ahí en lugar del CSV suelto. Informe automático por corrida y comparación entre corridas.
6. **Barridos** de semillas y perfiles.

## 7. Decisiones abiertas

- Frecuencia de muestreo por defecto (60 s simulados propuestos) y tope de `decisions`.
- Si el análisis se hace solo con SQLite y DuckDB o también hace falta Parquet desde el principio.
- Si los clanes de un mismo mundo comparten fichero (propuesto) o cada uno escribe el suyo.
