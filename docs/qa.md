# QA automatizado

Imágenes y datos de referencia, barridos de semillas y flujo de actualización.

[← Volver al README](../README.md)

## QA automatizado

Inspirado en el **Graphics Test Framework de Unity** (imágenes de referencia que se aceptan a mano) y en **geodiff / los wedges de PDG de Houdini** (comparar datos con tolerancia y barrer parámetros).

```cpp
GB::Qa::CheckGoldenImage(GB_GOLDEN_DIR "/images", "water_reflections", image, { /*Rmse*/ 4.0, /*BadFraction*/ 0.02, /*BadDelta*/ 32 });
GB::Qa::GoldenData d;  d.Set("trees", 150, /*abs*/ 4, /*rel*/ 0.03);  d.Set("river.endLevel", level, 0.4);
d.Check(GB_GOLDEN_DIR "/data", "world_temperate_5");
```

- **Imágenes de referencia** (`golden_images`): siete escenas canónicas (PBR, noche con farolas, agua con SSR, colinas con hierba y árboles, lluvia y charcos, rocas con capas, pila de postproceso) a 192×108 y 1 muestra de MSAA. Métricas: RMSE, error máximo y porcentaje de píxeles que difieren; si falla deja `nombre.actual.png` y `nombre.compare.png` (esperado | actual | diferencia ×8) en `qa_artifacts/`. Una referencia en `golden/<plataforma>/` tiene prioridad sobre la genérica, y una referencia que falta falla con instrucciones, como en Unity.
- **Datos de referencia** (`golden_data`): estadísticas de mundos generados (alturas, árboles, rocas, biomas, lago, río), de la erosión, de un muro de ladrillos golpeado por una bola y de una explosión sobre cajas. Las **tolerancias viven en el código** (no en el archivo), así que actualizar una referencia nunca relaja la comprobación.
- **Barridos** (`sweep`): 28 mundos (temperado, alpino, desierto, y variaciones de bosque, río y lago) comprobados con invariantes: alturas finitas, presupuestos, colocaciones dentro del mapa y en suelo seco, biomas que suman 1, el río siempre baja, **llega al lago y termina a su nivel**, la cuenca está bajo el agua, el punto de aparición no está en el agua. Informa de la semilla que falla.
- **Barrido de la simulación** (`sim_sweep`): tribus que viven días sobre mundos generados, comprobadas paso a paso contra la tabla de invariantes de `SimQA`, con una línea de métricas por partida; ver [QA_SIMULACION.md](QA_SIMULACION.md).
- **Huellas** (`Fingerprint`): hash FNV de datos float cuantizados, insensible a diferencias de redondeo entre plataformas.
- Flujo: `ctest` normal compara; tras un cambio visual **intencionado**: `GB_UPDATE_GOLDEN=1 ctest -R golden --test-dir build`, mira las imágenes y haz commit de `tests/golden`. `tools/qa_report.py build/tests/qa_artifacts` genera una página HTML con las diferencias; el CI sube `qa_artifacts` como artefacto si algo falla.
- Primer fruto: los datos de referencia delataron que el río del mundo templado acababa 3,7 m **por debajo** del lago (el trazado chocaba con el borde del mapa); ahora el camino se calcula con Dijkstra hasta el lago y el nivel del agua baja hasta el del lago, y los barridos lo vigilan.
- Test `qa` (CPU): métricas, tolerancias, referencias de imagen y de datos, sobrescritura por plataforma, huellas.
