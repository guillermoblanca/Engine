# Wiki del proyecto

`index.html` es una página única, sin servidor ni red: se abre con el navegador. Muestra las **reglas reales del juego** (habilidades con sus requisitos, costes y animaciones; efectos; árbol de etiquetas;
atributos por tipo de entidad; necesidades; sentidos y decisión; roles; estaciones, ecología e interactivos; objetos, recetas y nodos; tecnologías; paquetes y el resto de datos de `assets/data`)
y los documentos de `docs/`. Tiene buscador.

## Cómo se actualiza

```
tools\make_wiki.cmd <directorio-de-build>     (por defecto build_final; por ejemplo C:\gb-build\wt-mundo-c)
```

Hace dos cosas:

1. El editor sin ventana construye una escena con todas las demos y ejecuta `docs export docs/wiki/data/rules.json`, que vuelca lo que dicen las reglas en el motor (`GB/Gameplay/DocsExport.h`).
2. `python tools/make_wiki.py` (necesita `pip install markdown`) lee ese JSON, `assets/data/*.json` y `docs/*.md` y escribe `index.html`.

Lo que se ve es lo que el motor hace: si añades una habilidad o un atributo, aparece al regenerar. Los textos de `NOTES_ES` en `tools/make_wiki.py` traducen las notas de las habilidades (en el código están en inglés).
Un documento nuevo de `docs/` aparece en la wiki cuando se añade a `DOC_LIST` en `tools/make_wiki.py`.

## Qué hay dónde

| Sección | Sale de |
|---|---|
| Habilidades, efectos, atributos, necesidades, sentidos, roles, estaciones | `data/rules.json` (exportado del motor) |
| Objetos, recetas, nodos y tecnologías | `assets/data/era1.json` y `era1_ampliado.json` |
| Paquetes | `assets/data/*.pack.json` |
| Más datos del juego (tribu, trabajo, conocimiento, exploración, interactivos, especies, cámaras) | `assets/data/era1.json`, `world.json` y `cameras.json`, tal como están escritos |
| Estado de los hitos (inicio) | la tabla de estado del apartado 4 de `docs/PLAN_SOCIEDAD_VIVA.md` |
| Documentos | `docs/*.md` |
