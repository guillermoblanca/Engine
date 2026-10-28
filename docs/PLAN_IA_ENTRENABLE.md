# Plan: simulación medible y IA entrenable

Del estado actual (cerebro de utilidad, un clan, mundo que exige GL) a una IA que se pueda medir, comparar por perfiles y, si compensa, sustituir en parte por una red neuronal.
Redactado el 28-10-2026 a partir de la sesión de revisión de `mundo/integracion` (71/71 tests) y de una simulación sin ventana de ~12.800 s con 4 fundadores.

## 1. Estado actual (hechos medidos)

- **IA:** cerebro de utilidad. Puntúa opciones (`gather`, `explore`, `experiment`, `socialize`, `rest`, `sleep`, `tool`…) con pesos y rasgos que están en datos (`era1.json`, sección `society`). No es aprendizaje.
- **Un solo clan:** el editor guarda una tribu (`m_Tribe`). Sociedad, exploración, conocimiento y trabajos son por tribu.
- **Mundo:** `Terrain::CreateHills` construye malla y texturas, así que no hay `GeneratedWorld` sin contexto GL (HANDOFF, punto 4).
- **Observabilidad:** el log de la tribu es un anillo de 800 líneas de texto libre (`Tribe.cpp:578`). No hay series temporales ni puntuaciones de las decisiones.
- **Velocidad:** ~8 min reales por 6.400 s simulados con 7 miembros, `timescale 20`, ventana oculta.
- **Tests:** 71 de 71 pasan con `-j 2`. `ai` tarda ~104 s y el HANDOFF dice que falla con carga.
- **Comportamientos anómalos observados** (base para los primeros escenarios, sin causa verificada):
  1. Bucle de expedición: 25 de 29 expediciones van a Central Meadows por `copper_ore` y cada una "encuentra" la misma veta; el almacén sigue con `copper_ore x4` y quien extrae ha fabricado 1 objeto.
  2. Búsqueda de juncos a puntos aleatorios con 5 juncales conocidos y 16 juncos en el almacén.
  3. Casi nadie fabrica (`made 0` en 5 de 7) con piedra, pedernal, ramas, arcilla y fibra acumuladas; hoguera apagada todo el tiempo.
  4. Un miembro murió de hambre (comió 12 veces frente a 80–145 del resto) sin reacción del clan.
  5. Aparece un miembro sin evento de nacimiento o llegada en el log.
  6. Sueño sincronizado de todo el clan.

## 2. Objetivo y criterios de éxito

Objetivo final: poder cambiar la IA (pesos, perfiles o una red) y saber, con datos y sin mirar logs, si mejora o empeora.

| # | Criterio | Cómo se mide |
|---|---|---|
| O1 | Mundo y simulación corren sin GL | un test de CPU genera el mundo y simula 1 h de juego |
| O2 | Velocidad suficiente para barridos | ≥ 200× tiempo real con 50 miembros en un núcleo |
| O3 | Todo lo que pasa queda registrado | 0 eventos de decisión perdidos en una corrida de 24 h simuladas |
| O4 | Comportamiento verificable | batería de escenarios con aserciones en el CI |
| O5 | Perfiles comparables | informe automático de N semillas × M perfiles |
| O6 | Varios clanes | ≥ 3 clanes de ≥ 30 miembros en el mismo mundo |
| O7 | Red neuronal opcional | red que iguala al cerebro actual en los escenarios y se carga desde datos |

## 3. Principios

- Todo lo nuevo se ejecuta sin ventana (CLAUDE.md) y es determinista dada la semilla.
- Cada cambio de generación o de IA va con versión: no se rompen guardados ni repeticiones (#65).
- La telemetría y el puente de entrenamiento están desactivados por defecto y no alteran el resultado de la simulación.
- Lo explicable no se pierde: los límites duros (comer con hambre crítica) siguen en el motor, no en la red.
- El que llega segundo integra: PR pequeños y en orden.

## 4. Hitos

### H0 · Integración y base (en curso)
- Fusionar `mundo/integracion` en `master`. 
- **Hecho cuando:** `master` compila con `-DGB_WARNINGS_AS_ERRORS=ON` y pasa `ctest -j 2`.
- Tests: los 71 existentes.

### H1 · Corregir los comportamientos anómalos
- Investigar y corregir los seis puntos de la sección 1 (empezar por el bucle del cobre en `TribeExplore.cpp` y `JobBoard.cpp`).
- **Hecho cuando:** cada punto tiene un test que fallaba antes del arreglo.
- Tests nuevos (`tests/behavior_*`): 
  - tras `copper_ore` encontrado, la expedición se cierra y se abre un trabajo de extracción; 
  - con una especie de nodo conocida, el forrajeo no hace expediciones aleatorias; 
  - en 3 h simuladas la hoguera se enciende y nadie muere de hambre con comida disponible; 
  - un oficio vacante por muerte lo ocupa otro miembro.
- Necesita: nada nuevo. Es el mejor banco de pruebas del arnés de H4.

### H2 · Mundo y simulación sin GL
- Separar alturas, biomas y colocación (CPU) de la malla y texturas (GL) en `Terrain` y `WorldGen`.
- Tabla de biomas horneada (rejilla 1–2 m) y distancia real al agua.
- `WorldDesc::GeneratorVersion` (ya hay `WorldGenVersion.h`) con migración en `GB/Save`; mezclar `desc.Seed` en los azares fijos.
- Programa `GBSim` (sin editor): genera mundo, crea clanes y simula N segundos escribiendo métricas.
- **Hecho cuando:** O1 y una medida de O2.
- Tests: 
  - el mundo de CPU es idéntico al de GL en alturas, biomas y nodos (comparación bit a bit en 28 mundos del `sweep`); 
  - un guardado antiguo carga el mismo mundo con su versión; 
  - 1 h simulada es reproducible: mismo hash de estado con la misma semilla en Windows y Linux (cuidar `acos` frente a `cos(MaxSlope)` precalculado, ya anotado en el HANDOFF);
  - `golden_images` no cambia.
- Riesgo: cambia referencias de imagen y datos. Mitigación: versionar el generador y no tocar la versión 1.

### H3 · Telemetría en SQLite
- Dependencia: SQLite (amalgamación con `FetchContent` y versión fija).
- `TelemetrySink` con hilo de escritura, cola acotada y transacciones de ~1 s (WAL, `synchronous=NORMAL`).
- Tablas: `runs` (semilla, versión del generador, commit, perfil), `events`, `samples`, `clan_samples`, `decisions` (opciones consideradas y puntuaciones).
- Siempre con `clan_id` y `member_id`, aunque haya un clan.
- Argumento `--telemetry=archivo.db`; comando `telemetry start|stop`.
- **Hecho cuando:** O3. Muestras diezmables, eventos nunca.
- Tests: 
  - con la telemetría activa el hash de estado de la simulación es idéntico al de sin ella; 
  - los eventos de una corrida cuadran con el log de la tribu; 
  - cola llena: se pierden muestras, no eventos; 
  - cierre limpio sin corromper el `.db`.

### H4 · Arnés de escenarios y métricas
- Escenarios en datos (JSON): mundo, semilla, clan, duración, perfil, aserciones.
- Métricas: tiempo por actividad, muertes y causa, producción, fabricación, cobertura de exploración, repeticiones (misma acción al mismo objetivo más de K veces), tiempo sin hoguera.
- Informe automático (HTML o Markdown) y `tools/analyze_run.py`.
- **Hecho cuando:** O4, con ≥ 10 escenarios en el CI.
- Tests: los escenarios son los tests. Además: un escenario que debe fallar (bucle forzado) para comprobar que el arnés detecta el fallo.

### H5 · Perfiles y barridos
- Perfiles de personalidad en datos (curioso, cauto, sociable, solitario…) con rasgos y pesos; tarda poco porque ya están en `society`.
- Barrido N semillas × M perfiles en paralelo con `GBSim`; DuckDB lee el `.db` para el análisis.
- **Hecho cuando:** O5 y O2 al 100 %.
- Tests: reproducibilidad del barrido (mismo informe con la misma semilla); un perfil extremo produce las métricas esperadas.

### H6 · Varios clanes
- Un contenedor de clanes con almacén, atlas, tablón de trabajos y hogar propios.
- Decisiones de diseño previas: cómo comparten recursos, si se ven, relaciones (ignorarse, comerciar, competir).
- Migrar guardado, editor (`tribe` con id), repetición y telemetría.
- **Hecho cuando:** O6 con presupuesto de ms por tick dentro del límite.
- Tests: aislamiento (un clan no lee el almacén de otro), guardado y carga de varios clanes, escala (3 × 30 miembros), `ai` con presupuesto holgado.
- Riesgo: es el cambio más invasivo; toca `Tribe`, `Society`, `Exploration`, `JobBoard` y el guardado.

### H7 · Entorno de aprendizaje (API estado–acción–recompensa)
- Interfaz de entorno: `reset(semilla, escenario)`, `observe(miembro)`, `act(miembro, opción)`, `step(dt)`, `reward`.
- Puente a Python: proceso aparte con tuberías o `pybind11`; envoltorio estilo Gymnasium en `tools/gym/`.
- Observación: necesidades, rasgos, entorno cercano, memoria, estado del clan. Acciones: las opciones del cerebro.
- **Hecho cuando:** un agente aleatorio y el cerebro de utilidad corren por la API con los mismos resultados que sin ella.
- Tests: determinismo por la API, `reset` repetido idéntico, la acción forzada se ejecuta o devuelve un motivo.

### H8 · Clonación de comportamiento
- Dataset (estado, opción elegida, puntuaciones) desde `decisions`.
- Entrenar una MLP pequeña en PyTorch que imite al cerebro de utilidad (opción 2 de la propuesta).
- Exportar a ONNX; inferencia en C++ con ONNX Runtime o MLP propia; modelo versionado y guardado con la partida.
- **Hecho cuando:** O7 en su primera mitad: la red iguala al cerebro en los escenarios de H4 (tolerancia por métrica) y la inferencia cabe en el presupuesto.
- Tests: la inferencia en C++ da la misma salida que PyTorch (tolerancia 1e-5); simulación con red es reproducible; presupuesto de ms con 100 miembros agrupados en lotes; el límite duro (hambre crítica) sigue mandando.

### H9 · Refinado por refuerzo y comparación
- Diseñar la recompensa (supervivencia, producción, diversidad) y auditarla con agentes adversarios.
- Entrenar con la API de H7 y evaluar con el arnés de H4/H5.
- **Hecho cuando:** la red supera al cerebro de utilidad en ≥ 2 métricas clave sin empeorar ninguna aserción.
- Tests: batería de regresión de H4 sobre cada modelo candidato; prueba de "trampas" (la red no aprende a quedarse quieta ni a explotar un fallo del motor).

## 5. Dependencias entre hitos

```
H0 → H1 ┐
H0 → H2 → H3 → H4 → H5
            ↘     ↘
             H7 → H8 → H9
H0 → H6 (necesita H3 para su telemetría; H7 puede prescindir de H6)
```
H1, H2 y H3 se pueden llevar en paralelo. H4 necesita H3. H8 necesita H7 y datos de H3.

## 6. Necesidades

- **Dependencias nuevas:** SQLite (H3); ONNX Runtime o MLP propia (H8); Python con pandas, DuckDB, PyTorch y Gymnasium (solo herramientas, no entran en el build del motor).
- **Máquina:** un equipo con varios núcleos para barridos (H5, H9); GPU opcional para el entrenamiento (H9).
- **Decisiones del usuario:** reglas de relación entre clanes (H6); qué se premia en la recompensa (H9); si se acepta cambiar referencias de imagen y datos al versionar el generador (H2).
- **Datos:** semillas y escenarios de referencia congelados; el X Bot y los packs de animación ya están en el repo (`packs/full`).
- **Coordinación:** el HANDOFF avisa de tres PR que se pisan en `Interactives.*`, `Actor.*` y `ActorWorld.*`; integrar en orden antes de abrir H6.

## 7. Estrategia de pruebas (resumen)

| Nivel | Qué cubre | Dónde |
|---|---|---|
| Unitario | reglas, parseo de datos, tabla de biomas, cola de telemetría | `tests/*_test.cpp` |
| Determinismo | mismo hash de estado con la misma semilla, con y sin telemetría, Windows y Linux | `tests/determinism_test.cpp` |
| Escenario | aserciones de comportamiento por perfil | `tests/scenarios/*.json` + arnés |
| Rendimiento | ms por tick con 30/100/300 miembros; velocidad de `GBSim` | `scale_test`, `bench` |
| Regresión de IA | cada modelo o cambio de pesos frente a la referencia | CI nocturno |
| Golden | imágenes, datos y guardados no cambian salvo versión | `golden_*` |
| Fuzz de entorno | acciones aleatorias por la API sin cuelgues | H7 |

El test `ai` (104 s) se debe aligerar en H2 o separar en rápido y largo, para que el CI no dependa de la carga de la máquina.

## 8. Riesgos principales

1. **Romper guardados y repeticiones** al tocar `WorldGen`. Mitigación: `GeneratorVersion` y migración (H2).
2. **Rendimiento** con muchos clanes: la percepción es O(miembros × cosas) (`docs/IA.md`). Medir en H2 y H6 antes de seguir.
3. **Recompensa mal diseñada** en H9. Mitigación: agentes adversarios y empezar solo por clonación (H8).
4. **Alcance:** H6 y H9 son grandes. Cada uno tiene su punto de decisión: seguir o parar con lo conseguido.
5. **Determinismo entre plataformas** (coma flotante). Mitigación: el hash de estado se prueba en el CI de Windows y Linux.

## 9. Orden recomendado

1. H0 ya.
2. H1 y H2 en paralelo, en ramas separadas.
3. H3, después H4.
4. Punto de control: ¿el arnés detecta las anomalías de la sección 1? Si sí, seguir con H5 y H7.
5. H6 solo cuando haya decisiones de diseño de relación entre clanes.
6. H8 y H9 al final, con la red como candidata contra una referencia ya medida.
