# Capa visual por datos: nodos, props, edificios, texturas y criaturas

Lo que la simulación sabe y antes no se veía, dibujado a partir de datos: nodos de recurso que cambian con su `Amount`, herramientas en la mano y en
la cadera, cargas en brazos con los sets de transporte, manos que llegan al objeto, edificios por fases, campos, montones del almacén, el zorro
animado y un conejo procedural. **Nada de esto cambia la simulación**: una tribu sin ventana decide exactamente lo mismo; la capa visual solo lee
estado (etiquetas, atributos, mochilas, trabajos de la tribu, campos y almacenes).

El mapa acción → clip (qué animación hace cada habilidad y cada actividad) está en [ANIMACIONES_USO.md](ANIMACIONES_USO.md).

## Piezas

| Pieza | Fichero | Qué es |
|---|---|---|
| `ProcMesh` | `GB/Visual/ProcMesh.h` | Geometría en CPU: caja, cilindro (con conicidad y curva), *blob* (icosaedro subdividido deformado con ruido) y hoja (hierba, junco). Determinista (hash de los vértices), UV en metros |
| `ProcTexture` | `GB/Visual/ProcTexture.h` | Texturas en CPU al cargar: corteza, tablones, piedra, sílex, mineral de cobre, paja, tierra labrada, piel, hojas, arcilla, cuerda, mimbre, hierba y pelo. Periódicas (se repiten sin costura), deterministas, 256 px o 64 px con `quality minimum` |
| `VisualDefs` | `GB/Visual/VisualDefs.h` | Los datos (sección `visuals`): materiales, modelos por piezas con su ventana de estado, edificios por fases, criaturas, montones, reglas de props y de alcance. `LoadVisuals` atómico, `VisualsToJson` |
| `PropVisuals` | `GB/Visual/PropVisuals.h` | `ChooseProps`: qué se ve en las manos, caderas, espalda y brazos según la mochila y el trabajo (puro) |
| `BuildingVisuals` | `GB/Visual/BuildingVisuals.h` | Adaptador para H8: `Build(mundo, id, fase, progreso)`, `Set`, `Read`, `Overall` |
| `SceneVisuals`, `VisualCache` | `GB/Visual/SceneVisuals.h` | Lado GL: mallas en caché por modelo, variante y estado; texturas; el gancho del `EntityRenderer` que dibuja nodos, edificios y criaturas |
| `ReachLayer` | `GB/Animation/ReachLayer.h` | Capa de animación del juego: IK de dos huesos de una o dos manos a un punto, con fundido |
| `TribeVisuals` | `GB/Game/TribeVisuals.h` | El director de la tribu: props, sets, montajes de actividad, alcance, heridas, campos, montones y banco de trabajo. Uno para todos los clanes |

## Datos: `assets/data/visuals.json`

Fichero del paquete `era1` (`era1.pack.json`), leído por la sección `visuals` (`AddVisualContentSection`). Un paquete posterior puede
redibujar un modelo por id sin tocar los demás. Un error (forma, material, textura, look o socket desconocidos) rechaza el documento entero.

```json
"tree": { "variants": 3, "parts": [
  { "name": "Trunk", "shape": "cylinder", "size": [0.42, 3.2, 0.22], "material": "bark", "sides": 9, "bend": 0.18, "show": [0, 1] },
  { "name": "Crown", "shape": "blob", "pos": [0.15, 3.25, 0], "size": [1.15, 0.95, 1.15], "material": "leaves", "count": 4, "radius": 0.75,
    "detail": 2, "rough": 0.38, "jitter": 0.5, "show": [0, 1], "progressive": true },
  { "name": "Stump", "shape": "cylinder", "size": [0.44, 0.45, 0.38], "material": "bark", "show": [-1, 0] } ] }
```

- **Piezas**: `shape` (`box`, `cylinder` = diámetro abajo, alto, diámetro arriba; `blob` = radios; `blade` = ancho, alto, inclinación), `pos`,
  `rot` (grados), `material`, repeticiones (`count` con `layout` `ring`/`row`/`stack`/`scatter`, `radius`, `spacing`, `columns`, `jitter`).
- **Estado**: la fracción `f` = `Amount / capacidad` del nodo. Una pieza se ve con `f` en `(from, to]` (`show`); `from < 0` incluye el nodo agotado,
  así que `[-1, 0]` es lo que solo tiene el nodo agotado (el tocón). Con `progressive` las copias desaparecen de una en una al bajar `f`: el
  arbusto pierde un racimo por cada baya cogida, la mata de fibra adelgaza, el árbol pierde copa y al talarlo queda el tocón con astillas.
- **Variantes**: cada instancia elige una por el hash de su nombre (no todos los árboles iguales).
- **Materiales**: color, textura procedural, `uvScale`, rugosidad, metal, `grain` (relieve de ruido del renderer), `wind` (balanceo), `seasonal`
  (multiplica el color de hoja de la estación: las copas procedurales cambian con la estación como las de antes), `doubleSided`.

## Nodos de recurso

El gancho `EntityRenderer::SetEntityVisual` dibuja cada entidad con etiqueta `Resource.Node.<id>` con el modelo de su nodo (`model` del nodo o
su id) y la cantidad viva del mundo de gameplay; **la entidad y sus hijos de primitivas no se dibujan**, pero siguen en la escena: el guardado,
la línea base del mundo, la selección en el visor y una compilación sin la capa visual no cambian. Modelos de era 1 y del catálogo ampliado:
árbol, árbol viejo, árbol joven, roca, granito, afloramiento de sílex, veta de cobre (vetas verdes que se agotan, la roca queda), orilla de
arcilla, juncal, mata de fibra, arbusto de bayas, tronco caído (en dos mitades), roble con bellotas, pino con resina, grano silvestre,
salina y huesos.

## Props en las manos

`ChooseProps` (puro, con test) decide a partir de la mochila y la actividad:

| Situación | Qué se ve |
|---|---|
| Talando (`Chop` activo) | el mejor hacha de la mochila en la mano derecha (sílex mejor que piedra); su prop se llama `tool`, el mismo que equipa la habilidad, así que el *look* sustituye a la caja |
| Andando con herramientas | el hacha colgada de la cadera derecha, el pico de la izquierda (modelos `_rest`: cabeza arriba) |
| Con cesta | la cesta a la espalda |
| Camino del almacén (`deposit`, `haul`) con 3+ unidades | la carga en brazos entre las palmas (agarres con IK), con el set de transporte: troncos con `holding`, piedra/arcilla en caja con `box`, comida en cesta con `box`, 14+ unidades en carretilla con `wheelbarrow` |
| Avivando o encendiendo el fuego | leña en la mano izquierda |
| Comiendo | un puñado de bayas |
| Labrando (`farm.till`) / segando | la azada / la hoz en la mano |

Nunca dos props en el mismo socket y como mucho `maxShown`. Los modelos de props son procedurales (hacha = mango + cabeza + atadura, pico,
azada, hoz, cesta, haz de troncos, caja de piedras, cesta de bayas, haz de fibra, carretilla, leña).

## Manos al objeto

`visuals.reach` dice, por habilidad o actividad, qué mano (o las dos) va al punto de contacto del objetivo (altura sobre su origen, distancia
hacia quien actúa) y en qué ventana del montaje: tronco al talar, roca al picar, arbusto, mata, arcilla, brasas al avivar, cuerda del pozo, tirador
de la puerta y tablero del banco de trabajo. El objetivo sale del trabajo de la tribu (`Detail`: el nodo, `Campfire <clan>`, el pozo) o del banco
de trabajo del campamento.

## Edificios de H8

Las obras y edificios de H8 (`GB/Gameplay/Buildings.h`: entidad con `Building.Kind.<id>` y los atributos `Phase` y `Work`) se dibujan con el
modelo procedural de su mismo id: el avance de la obra es su fase más el trabajo hecho sobre el que pide (`BuildingDef::Phases[i].Work`), y se
reparte en las fases visuales a la misma fracción de la obra entera (las tres fases del refugio de H8 sobre las cuatro visuales: estacas, armazón,
paredes, techo). La cantidad viva sale del mundo de gameplay; las piezas de primitivas de `BuildSite` siguen en la escena como respaldo (no se
dibujan bajo el gancho). La habilidad `Build` tiene su animación en el manifiesto (`build_hammer`, `build_place`), las manos van a la obra
(`reach.Build`) y quien lleva materiales a la obra (`deliver`) los lleva en brazos como al almacén.

## Edificios por fases sin H8 (adaptador)

Seis edificios con sus fases: refugio (estacas, armazón, paredes de zarzo, techo de paja), almacén elevado (postes, plataforma, paredes de tablón,
tejado), banco de trabajo (patas, tablero con yunque y lascas), secadero (postes, travesaños, tiras colgadas), horno (piedras con brasas, cúpula
de arcilla, chimenea) y empalizada (estacas aguzadas, travesaños). Las piezas de las fases anteriores están todas; las de la fase actual aparecen
en orden con el progreso (estaca a estaca). H8 solo tiene que llamar:

```cpp
EntityId site = BuildingVisuals::Build(world, "shelter", /*phase*/ 0, /*progress*/ 0.3f, "Hut 1", parent, position, yaw);
BuildingVisuals::Set(world, site, phase, progress);           // o escribir los atributos Phase y Progress en su sistema de habilidades
```

Editor: `building` (lista), `building <id> [fase] [progreso] [x z] [nombre]`, `building <id> phases [x z]` (todas las fases en fila).

## Estado que solo tiene la simulación

- **Campos** del clan (H18): cada parcela por su estado (`plot_dug`, `plot_sown`, `plot_sprout` que crece con `Growth`, `plot_grown` o
  `plot_grown_<cultivo>`), y escarcha encima en invierno.
- **Almacén y montones**: el almacén principal (delante del campamento) y los secundarios muestran un montón por tipo (`piles`: madera →
  troncos apilados, piedra, sílex, arcilla, fibra, comida en cestas, el resto en cajas), lleno según la cantidad.
- **Banco de trabajo** del campamento (la estación `workbench`).
- **Heridas (H14)**: un miembro con una herida seria o grave (`Combat.Wounded.Serious`/`.Grave`, las que frenan `Vigor`) o con `Health` por
  debajo de `InjuredBelow` (35 %) anda con el set `injured` (cojea). `TribeVisuals::Injured` se puede sustituir.
- **Combate (H14)**: en `fight`, `raid` y `hunt` la herramienta es el arma y va en la mano (hacha, si no pico; `Item.Weapon` primero cuando haya
  armas con modelo). Los montajes de los golpes son los de las reglas de combate de H14 (no se duplican aquí). La carne de una caza va a casa
  envuelta en su piel en brazos (`holding`) y en el almacén es un montón sobre una piel que mengua al estropearse. Un miembro muerto
  (`State.Dead`) o en ragdoll suelta todos sus props y no reproduce actividades ni alcances. `recover` y `care` tienen su actividad.

## Criaturas

`visuals.creatures` por etiqueta de especie: el zorro es `models/Fox.glb` con sus clips (`Survey`, `Walk`, `Run` según la velocidad medida),
escalado a su largo; el conejo es un cuerpo procedural (cuerpo, cuartos traseros, cabeza, orejas, ojos, patas y cola blanca). La entidad de
primitivas de la ecología sigue igual debajo. La vida social ([VIDA_SOCIAL.md](VIDA_SOCIAL.md)) añade en `social.json` (sección `visuals`, suma
por id) el ciervo (cuerpo, cuello, cabeza, astas, patas), el jabalí (cuerpo bajo, hocico, colmillos) y el urogallo, los modelos de sus nodos
(raíces, avellano, corro de setas, hierbas silvestres, nido con huevos, colmena en un tronco, sitio de pesca) y los montones de raíces, frutos
secos, pescado y platos del almacén. Los ritos se ven como un círculo: cada asistente en su sitio alrededor del lugar, con el gesto del rito.

## Editor

```
visuals [status]                       qué hay cargado, la caché y, por miembro, actividad, props, set, montaje y alcance
visuals on|off                         la capa entera
visuals props|activities|reach on|off  partes del director
visuals nodes [x z]                    cada tipo de nodo lleno, a medias y agotado, en filas
building ...                           ver arriba
```

`quality minimum` hace las texturas a 64 px. Al recargar contenido (`content load`, `pack load`) se vacía la caché.

## Tests

- `visuals` (CPU): la sección carga, va y vuelve y es atómica; texturas deterministas, distintas, sin costura y de 64 px en mínimo; mallas
  deterministas, sanas y dentro de presupuesto (nodo 9000 triángulos, prop 1500, edificio 20 000); un modelo por nodo de era 1 que cambia con
  `Amount` (bayas racimo a racimo, tocón al talar, vetas de cobre); el gancho lee la cantidad viva; los props por mochila y trabajo; los edificios
  por fases y el adaptador.
- `anim_usage` (CPU): ver [ANIMACIONES_USO.md](ANIMACIONES_USO.md).

## Límites

- Sin oclusión ni LOD propio: cada nodo son 1-3 objetos de render; con cientos de nodos cuenta la distancia de dibujo (`quality minimum`).
- Los props siguen el socket de la mano según la pose del clip; un clip de espada no sujeta un hacha igual: algunas herramientas atraviesan la
  mano en ciertos fotogramas (pide clips propios: ver la lista para el artista).
- El banco de trabajo y los montones solo existen en pantalla (no son entidades): no se pueden seleccionar.
