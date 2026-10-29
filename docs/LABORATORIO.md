# Laboratorio: lanzar simulaciones y leer lo que ocurre

Una web local para lanzar simulaciones sin ventana (`GBSim`) y analizar su telemetría. No sustituye al juego: sirve para tomar decisiones sobre cómo debe evolucionar la IA, los clanes y el mundo.

## Poner en marcha

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target GBSim
python tools/lab/server.py            # o tools\lab\Laboratorio.bat en Windows
```

Abre <http://127.0.0.1:8765>. El servidor busca `GBSim` en las carpetas de build vecinas; si no lo encuentra, `--sim ruta/GBSim`. Las partidas se guardan en `lab_runs/` (un `.db` SQLite por partida; `--runs carpeta` para cambiarlo). Solo usa la biblioteca estándar de Python y escucha únicamente en esta máquina.

## Qué se puede hacer

- **Lanzar simulación**: años (un año de edad son 4 días simulados, un día son 1200 s), semilla, clanes, miembros por clan, bioma, muestreo, combate e incursiones. Condiciones de parada: muere un clan, mueren todos, o los clanes quedan **estancados** (días sin superar su mejor población).
- **Partidas**: las partidas guardadas, con su estado; abrir, borrar.
- **Análisis** de una partida: lectura automática de qué ha ocurrido; indicadores por clan; evolución comparada de población, comida, edificios, pirámide y camas; **necesidades cubiertas** (hambre, sed, descanso, calor, salud); tecnologías con su día; edificios; religión, creencias, ritos, tradiciones y normas; relaciones entre clanes (actitud, hostilidad, rencor, eventos); genealogía y vínculos; flora y recursos del mundo (al inicio y restantes); decisiones, resultado de las tareas y bucles; muertes con causa.
- **Comparar**: superpone la misma serie de varias partidas.

`GBSim` también se usa solo: `GBSim --seed=7 --years=2 --clans=3 --members=4 --combat --stop=clan_dead --stop=stagnant:6 --telemetry=run.db`. `tools/analyze_run.py` y `tools/sweep.py` hacen informes y barridos por consola.

## Qué datos hay y qué falta

Esquema 4 de la telemetría (`docs/DATOS_INDIVIDUO.md`): muestras de miembros y clanes, eventos, tareas, decisiones (con las 6 mejores opciones y sus factores), vínculos, relaciones entre clanes, días de cada clan (tecnologías, creencias, ritos, ánimo) y nodos del mundo.

Faltan: la **fauna** (GBSim no simula la ecología; el editor sí), y que GBSim cargue el contenido de la era (edificios y progresión) como el editor.

## Lanzar el juego

El juego es el editor con el modo de juego:

```
cmake --build build --config Release --target Editor
build/Editor/Release/Editor.exe --play                       # un mundo, dos clanes y tú en el clan 0
build/Editor/Release/Editor.exe "--play=alpine 7 5 3"        # bioma, semilla, miembros por clan, clanes
build/Editor/Release/Editor.exe --script=play.txt            # lo mismo desde el guion de la raíz
build/Editor/Release/Editor.exe --script=mi_guion.txt        # un guion propio (generate, tribe build, clan new, camera follow...)
```

Dentro, la consola (`help`) acepta todos los comandos. Con el comando `telemetry start fichero.db` el editor escribe la misma telemetría que GBSim, y el laboratorio puede abrirla.
