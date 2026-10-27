# Navegación (H5, primera mitad)

`GB/Navigation/NavGrid.h` + `GB/Game/AIDriver.h`: los personajes que no maneja una persona encuentran solos el camino por el terreno. Es la mitad de
navegación de H5 ([PLAN_SOCIEDAD_VIVA.md](PLAN_SOCIEDAD_VIVA.md)); la percepción (vista, oído, memoria) es la otra mitad y la hace otro carril.
Todo es CPU, sin ventana, y determinista (los empates se rompen por índice de celda y los actores se actualizan por nombre).

## Qué hay

- **Cuadrícula de navegación** (`NavGrid`): celdas de 0,5 m sobre los límites del mundo. Se construye desde un `CharacterWorld` (suelo, agua, límites y obstáculos
  estáticos), así que lo que bloquea al personaje bloquea el camino. Cada celda guarda la pendiente, la profundidad del agua, la vegetación (opcional) y si un
  obstáculo la bloquea (los obstáculos se engordan con el radio del personaje más un margen de 10 cm).
- **Costes por perfil** (`NavProfile`): lo que cuesta una celda se decide al consultar, no al construir, así que un mismo mapa sirve a todos. Un valor de 1 es
  caminar por suelo llano y seco. Pendiente (`SlopeWeight`), vadeo (`WaterWeight`), maleza (`VegetationWeight`) y **carga** (`Load` de 0 a 1, multiplica el coste de
  pendientes y agua por `1 + Load * LoadFactor`). Son muro la pendiente por encima de `MaxSlope` y el agua por encima de `MaxWadeDepth`; el `AIDriver` los toma del
  `CharacterController`, así que lo que el personaje no puede pisar tampoco se planifica. El rol y la carga se ponen en `Actor::Navigation` (o en
  `TaskRunner::Profile` / `InteractionRunner::Profile`).
- **Camino** (`NavGrid::FindPath`): A\* de 8 vecinos sin cortar esquinas, con heurística octil. Si la línea recta es libre y casi llana no busca. Para distancias
  largas (más de 48 celdas) busca primero en un **grafo grueso de regiones** de 16x16 celdas y la búsqueda fina se queda en ese pasillo (si falla, se repite sin
  pasillo). El resultado se **suaviza** uniendo cada punto con el más lejano que ve, siempre que la recta no cueste más que el trozo que sustituye (así no se
  atraviesa el agua que el A\* rodeó). Una meta dentro de un muro (el punto de interacción de un tronco) se mueve a la celda libre más cercana (hasta 2 m) y se
  termina andando; una meta inalcanzable da el camino hasta lo más cerca que se puede (`Partial`).
- **Seguimiento con evitación local** (`AIDriver`): `MoveTo` planifica al primer `Update` y sigue los puntos. Mira a los demás personajes (los obstáculos de
  `CharacterWorld` desde `DynamicFrom`, que `Actor::SetOthers` ya rellena): cada uno en el camino empuja hacia el lado contrario (siempre el derecho si va de
  frente, así dos que se cruzan se esquivan), uno demasiado cerca empuja directo hacia fuera y se frena si hay alguien delante. Atasco: si en un segundo avanza
  menos de 30 cm, o está bloqueado y casi quieto 1,5 s, **replanifica** con los vecinos cercanos como muros; tras 3 intentos sin avanzar se rinde (`Stuck`, y la
  tarea falla como antes). Si lo frena gente (no el terreno) espera hasta 20 s (`CrowdPatience`). Además replanifica cuando el mundo cambió bajo su camino.
- **Misma interfaz**: sigue devolviendo `CharacterInput` (lo mismo que el jugador). `goto`, `interact`, `ability ...` con `MoveToTarget`, `Actor::MoveTo` y la
  `Tribe` no cambian: `TaskRunner`, `InteractionRunner` y `Actor` le pasan el `CharacterWorld`, que lleva el puntero `Nav`. Sin cuadrícula (o con `SetPath`) el
  `AIDriver` camina en línea recta como antes.
- **El mundo cambia** (`SyncObstacles`, `UpdateRegion`): al talar un árbol o poner un edificio solo se rehacen las celdas alrededor del círculo que cambió (y
  su región del grafo grueso). Cada cambio sube `Revision()` y queda en un registro de 64 entradas; un caminante solo replanifica si el cambio cae dentro de la
  caja de su camino (`ChangedSince`). El editor mira cada segundo si los árboles cambiaron y sincroniza.
- **Reparto del trabajo**: `NavGrid::NewFrame` + `AllowPlan` limitan las búsquedas a 4 por fotograma (`MaxPlansPerFrame`); el resto espera al siguiente. `ActorWorld::Update`
  lo llama. Una tribu entera que sale a la vez no cuesta 50 búsquedas en un fotograma.

## En el editor

```
nav status                       cuadrícula (celdas bloqueadas, costosas, mojadas), búsquedas hechas, replanificaciones, tiempo y celdas rehechas
nav debug on|off                 sobre el viewport: rojo = muro, amarillo = costoso, azul = agua, líneas verdes = los últimos 64 caminos encontrados
nav rebuild | nav sync           construye de nuevo / mira ahora si cambiaron los obstáculos
nav path x1 z1 x2 z2 [carga]     calcula un camino y dice su longitud, coste y celdas abiertas
```

La cuadrícula se crea al crear el primer actor (`actor spawn`, `tribe build`) o con `nav debug on` / `nav rebuild`, y la comparten todos los actores del `ActorWorld`.

## Presupuesto de CPU

Medido por `tests/navigation_test.cpp` (Release, un hilo, mundo de 160 x 160 m con 260 árboles, 40 rocas, un lago con núcleo profundo, un estanque somero,
un arroyo y una colina inaccesible; 50 caminantes salen de un mismo campamento hacia 50 puntos de recurso a 40-130 m). El test usa 4 veces el presupuesto como
umbral para no fallar en máquinas lentas; las cifras son de una ejecución en el PC de desarrollo.

| Concepto | Presupuesto | Medido |
|---|---|---|
| Construir la cuadrícula (316 x 316 celdas) | 250 ms | 8 ms |
| Una búsqueda de camino, media (distancias largas) | 2 ms | 1,4 ms (5.500 celdas abiertas por búsqueda) |
| Navegación de 50 caminantes, media por fotograma | 0,5 ms | 0,036 ms (0,7 µs por caminante) |
| Fotograma más caro (la tribu sale a la vez, 4 búsquedas) | 10 ms | 7 ms |
| Rehacer la cuadrícula al cambiar un obstáculo | 5 ms | 0,02 ms (288 celdas, el 0,3 % de la cuadrícula) |
| Memoria | — | 316 x 316 x 4 bytes de datos + 16 bytes por celda de búsqueda (≈ 2 MB) |

Resultados del mismo test: los 50 llegan en 108 s simulados sin atascarse (0 `Stuck`, 19 replanificaciones en total), están empujando contra algo en el 0,02 %
de los pasos, ninguno vadea por encima de 0,77 m ni se mete en un tronco o una roca, y el más cercano a otro pasa a 0,94 m entre centros. El grafo grueso abre
387.000 celdas en vez de 570.000 (un 32 % menos) con un coste de camino un 0,3 % mayor. Dos ejecuciones terminan con los mismos fotogramas de llegada y las mismas
posiciones. En el mismo mundo, un estanque de 16 m de ancho se rodea (34,8 m de camino frente a 28 m en recta) y quien no teme al agua lo vadea (28,6 m).

Para el mundo de 512 m del editor (1.024 x 1.024 celdas) la construcción se mide en `nav rebuild`; el coste por búsqueda sube con la distancia, no con el tamaño del mapa.
Con 500 caminantes la media por fotograma sería de unos 0,4 ms sin contar las búsquedas, que se reparten a 4 por fotograma (6 ms como máximo en un fotograma).

## Límites conocidos y siguiente

- Los obstáculos son círculos, como los del `CharacterController`: un edificio rectangular se aproxima con varios (H8 traerá su propia huella).
- El `ActorWorld` toma los obstáculos de `ActorHost` al construirlo; las puertas (`gate`) y la vegetación como coste aún no alimentan la cuadrícula (la vegetación
  es una función opcional de `NavConfig`, sin conectar en el editor).
- Las habilidades con `MoveToTarget` usan el perfil por defecto del `InteractionRunner` de la tarea, no `Actor::Navigation`.
- La evitación mira posiciones, no velocidades: dos grupos que se cruzan de frente se esquivan, pero un pasillo muy estrecho con tráfico en los dos sentidos
  se resuelve esperando (`CrowdPatience`).
- Falta la otra mitad de H5 (percepción) y que la memoria espacial de cada agente y del clan alimente las metas (otro carril).
