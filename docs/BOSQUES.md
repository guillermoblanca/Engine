# Bosques, rocas, flores y comida silvestre

Petición: más árboles, bosques de nodos que reutilicen los modelos procedurales (así talar es fácil), rocas de varios tamaños y apiladas, flores, hierba que
cambie suavemente de una zona a otra, más variedad de comida, tareas básicas sin herramienta y empezar con abundancia. Este documento es el diseño, lo que hay
hecho, cómo verlo y lo que falta.

[← README](../README.md) · relacionado: [RECURSOS_MUNDO.md](RECURSOS_MUNDO.md) (el reparto por bioma que esto amplía), [mundo.md](mundo.md), [JUGAR.md](JUGAR.md)

## Cómo estaba (punto de partida)

- `PlanWorldResources` (CPU, determinista por semilla) reparte cada tipo de nodo con `spawn` (biomas, `perHectare`, `spacing`, `clump`, `max`) y devuelve `ResourceSpot`s;
  `BuildWorldResources` los convierte en entidades `wild <tipo> <n>` con primitivas (cubo y esfera) y un `Gameplay` con `Amount` = capacidad.
- Los árboles y rocas "de verdad" (`GeneratedWorld::Trees/Rocks`, 420 y 70 por tile) son entidades con `PropComponent` (`Tree|Rock` + variante) que el editor dibuja con los
  modelos procedurales (`TreeFactory`, `RockFactory`) y **no se pueden talar**. Los nodos talables eran primitivas aparte, unos 300 en un mapa de 360 m.
- La hierba (`GrassField`) usa `GeneratedWorld::GrassDensityAt`, que era una tabla por bioma: corte duro en cada frontera.
- Los datos de nodos viven en `assets/data/era1.json` (y deben coincidir con `RegisterEraOneItems`).

## Diseño

1. **Un árbol talable es un árbol procedural.** El nodo (`wild tree 12`) tiene un hijo `Crown` con `PropComponent{Tree, variante}` (el mismo modelo que los árboles decorativos del
   mundo, con escala y giro propios por nodo) y un tocón `Trunk` de primitiva. Al talar el último golpe el `ResourceSystem` oculta la copa (`SyncVisuals` ya esconde las partes
   `Crown`/`Rock`) y queda el tocón; al rebrotar vuelve. Las rocas igual (`Rock`, `Rock 2`… para las apiladas). Sin recetas de mundo (tests, sim sin ventana) cae a las primitivas de antes.
   No hay malla nueva ni instancias nuevas: se reutiliza el dibujo existente (`TreeFactory` ya cachea una malla por variante y el `Prop` las coloca; límite de dibujo 340 m).
2. **Parches ("patches") en vez de puntos sueltos.** Un parche es un grupo de nodos: el mapa se corta en celdas de `cell` m; una celda cuyo centro cae en un bioma de la lista
   da un parche con esa probabilidad (por la abundancia). El parche tiene radio entre `radiusMin` y `radiusMax` (muchos pequeños, pocos grandes), borde irregular (ruido de valor),
   claros (`clearings`: zonas sin nodos), y dentro una rejilla con jitter de `spacing` m, más rala hacia el borde (`fill`). Cada punto elige un tipo de la mezcla (`mix`, por peso) y
   toma tamaño de los datos del tipo (`spawn.scaleMin/scaleMax`), variante de modelo y giro por hash. Se descarta el agua, la pendiente, los árboles/rocas del mundo y lo que no
   deja el hueco (`Radius`, que crece con la raíz de la escala: un árbol grande pide más sitio).
3. **Primero el reparto base y luego los parches**, con la misma ocupación: el cobre y el pedernal (raros) no se pierden, y los parches rellenan el sitio libre.
4. **Abundancia en datos** (`assets/data/bosques.json`, `abundance`): multiplica el `perHectare` y el `max` de todos los nodos, y la probabilidad y el tamaño de los parches.
   Por defecto 2,0 (el motor, sin fichero, sigue en 1,0). `maxNodes` limita lo que añaden los parches (presupuesto de entidades). El editor la usa si no se da densidad.
5. **Determinismo.** Solo hash entero de (celda, semilla, tipo de parche); los flotantes entran al leer el suelo. Misma semilla y datos = mismos nodos en el mismo orden.
6. **Rocas** de varios tamaños como tipos de datos (`rock_small` 0,35-0,6, `rock` 0,7-1,4, `boulder` 2-3) y `rock_pile`: una roca con hasta 3 más pequeñas encima (`spawn.stack`).
7. **Flores** = nodos `decor` (sin `Gameplay`, sin interacción): una entidad con unas cuantas esferas. Se planean y se guardan en la línea base como cualquier nodo.
8. **Hierba con transición suave.** `BiomeMap::Grass` guarda la cobertura de cada bioma desfocada ~14 m (dos pasadas de caja) al hornear el mapa de biomas; `GrassDensityAt` la lee
   con interpolación bilineal. El coste por brizna es el mismo (una lectura de rejilla). El agua sigue desnuda.
9. **Comida y tareas sin herramienta.** La comida silvestre (`mushroom_ring`, `root_patch`, `nut_tree`, `wild_greens`, `bird_nest`, `bee_tree`) ya la define `social.json`
   (vida social, con su dieta y sus modelos): los parches la reparten en bosque y pradera, no se duplica. Nuevos, a mano (`requires` vacío): `loose_stones` (piedras sueltas, 1 por golpe,
   capacidad 3, rebrote 600 s) y `fallen_branch` (ramas caídas, rebrote 300 s): rinden 1 por golpe frente a `1 + poder` con herramienta y no se agotan nunca del todo, pero tardan en volver.
   La caza sigue siendo la ecología (`fox`, `rabbit`, `deer`, `boar`, `grouse`).

## Datos

| Fichero | Qué es |
|---|---|
| `assets/data/bosques.json` | `abundance` (2,0), `maxNodes` (3200) y los `patches`: `forest`, `stony`, `meadow_forage`, `flower_beds` (biomas con probabilidad, `cell`, `radiusMin/Max`, `spacing`, `fill`, `clearings`, `maxSlope`, `max`, `mix`) |
| `assets/data/era1_bosque.json` + `.pack.json` | Pack sobre era1: `tree`, `old_tree`, `young_tree` y `rock` con `spawn.prop` y escala; `rock_small`, `boulder`, `rock_pile`, `loose_stones`, `fallen_branch`, `flowers_*` (`decor`) |
| `assets/data/player.json` | `controls.game.resources` = 2,0 (la densidad que usa `play`) |

El editor carga el pack y `bosques.json` al arrancar (`EditorApp`). Sin esos ficheros todo se comporta como antes.

## Cómo generarlo y verlo

```
Editor --play                       # play.txt: temperate 5; resources world 2.0 + parches
Editor --script=play.txt
Editor --hidden --frames=8 --exec="generate temperate 5; resources world; camera 30 38 210 0 0 0; wait 3; screenshot forest.png"
Editor --exec="generate temperate 5; resources world 0.5"     # un mundo pobre; sin número: la abundancia de bosques.json
GBSim --seed=5 --days=2 --members=4 --forest=assets/data     # la simulación sin ventana con los bosques
GBForestTests                                                 # ctest -R forest
```

## Medidas (temperate 5, mapa de 360 m, Release, RTX 3070)

- `resources world`: 1432 nodos (antes 305 a densidad 1): 302 árboles, 276 jóvenes, 101 viejos, 120 bayas, flores (260), setas, raíces, nueces, ramas caídas, etc. Planificar: ~14 ms en CPU.
- Editor headless oculto: 20 ms por fotograma sin nodos, 20 ms con densidad 0,5 (605 nodos) y 27,8 ms con 1432 nodos (2435 objetos dibujados). El coste es de dibujo: cada árbol procedural son dos mallas
  (corteza y follaje) más el tocón, y cada ramo de flores 4 primitivas; no hay instancias. Para un equipo flojo, `resources world 1` o bajar `abundance`.
- GBSim 2 días, 4 miembros: 8,7 s sin bosques, 33,8 s con `--forest` (los miembros buscan entre ~1100 nodos de recurso en vez de ~300).
- Entidades: ~3,7 por nodo con modelo procedural (nodo, `Crown`, `Trunk`), 4-5 las flores.
- Hierba: el cambio máximo entre muestras a 1 m es 0,044 (un corte duro entre pradera y roca daba 0,88).

## Límites y siguientes pasos

- **Con la capa visual (`visuals on`, también en `play`)** los nodos cuyo modelo existe en `visuals.json` (`tree`, `rock`, `flint_outcrop`…) los dibuja esa capa y no el `Prop`; la escala y la variante del nodo no se aplican ahí. Falta añadir la escala al `Look`.
- El tiempo "mayor" de las tareas sin herramienta se expresa por rendimiento y rebrote, no por duración del swing: `ResourceNodeDef` no tiene aún duración por nodo (haría falta tocar la activación de habilidades).
- Los árboles decorativos del mundo siguen aparte de los talables (los nodos evitan sus huecos). Que sean la misma cosa sigue pendiente.
- Rocas apiladas: la altura de cada piedra de la pila es aproximada (no conoce la altura real de la variante).
- La partida guardada solo guarda la abundancia; los parches se rehacen con `bosques.json` de ese momento (cambiarlo desplaza la línea base).
- **Mundo continuo por chunks (solo diseño, sin código).** El planificador ya es por celdas hash (`cell` de cada parche, sin estado global salvo la ocupación), así que se puede evaluar por chunk:
  chunk = bloque de N x N celdas; los parches de las celdas vecinas que lo tocan (radio máximo conocido) se planean con la misma semilla, y la ocupación se rellena con las celdas del borde antes que las
  interiores. Con `Tiles` (H16, un heightmap único) ya no hay costura de terreno; falta que `GeneratedWorld` genere/olvide chunks de heightmap y de entidades según la distancia del jugador y que el
  `GameplayWorld`/`ResourceSystem` solo itere los chunks activos (hoy recorren todos los nodos por fotograma: es lo que más cuesta con 1400 nodos). Primer paso útil: índice espacial de nodos por celda.
