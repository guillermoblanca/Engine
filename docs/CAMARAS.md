# Cámaras de juego y calidad mínima (H19)

Las cámaras del editor (órbita, vuelo) sirven para construir; las de **juego** son las que usa quien juega con el clan. Están en
`GB/Game/GameCamera.{h,cpp}`: matemática pura sobre una función de altura del terreno, sin GL, sin ventana y sin leer la entrada (salvo
`ReadGameCameraInput`, que es lo único que toca `GB::Input`). Así el test las conduce fotograma a fotograma y las mismas entradas dan siempre las
mismas poses, bit a bit.

| Cámara | Qué hace | Clase |
|---|---|---|
| `follow` | Tercera persona detrás de un actor. Se recoloca detrás de hacia donde mira si el usuario no la ha girado en `recenterDelay` segundos. Si una colina se interpone, se acerca (la prueba de suelo de `ThirdPersonCamera`) y nunca baja de `groundMargin` sobre el terreno. Siguiendo al jugador, los ángulos los pone la cámara del jugador (WASD siguen siendo relativos a ella); los datos ponen la distancia, la altura, el objetivo y la lente | `FollowCamera` |
| `overview` | Vista del clan, al estilo de un RTS: un punto de enfoque en el suelo, un rumbo y una distancia. La inclinación sale de la distancia (de `pitchNear` de cerca a `pitchFar` de lejos, en escala logarítmica). Panear, girar y acercar mueven el estado deseado, que se recorta al mapa (menos `edgeMargin`); lo que se ve lo sigue suavemente | `OverviewCamera` |
| `observe` | Cinemática u observador: encuadra a un actor o a un grupo (centro y radio, más `padding`) con su lente (35° por defecto) y la distancia que lo hace caber por `framing`, girando despacio (`orbitSpeed`, grados por segundo) | `ObserveCamera` |

`GameCameraRig` tiene las tres y la **transición**: al cambiar, parte de lo que había en pantalla y mezcla posición, dirección y lente
(`Camera::Blend`) durante `transition.seconds` con una curva suave (smoothstep). La cámara nueva empieza en un sitio que encaja con la vieja: la
panorámica, sobre el punto que miraba y con el mismo rumbo. La recta entre dos poses sobre el suelo puede atravesar una colina: durante la mezcla la
posición también se mantiene sobre el terreno.

## Datos

Los parámetros son la sección `cameras` de un paquete (H17), registrada con `AddCameraContentSections` (`GB/Game/GameContent.h`, como las de objetos, roles y mundo).
`assets/data/cameras.json` va en los ficheros de `era1` (tras `era1.json` y `world.json`) y contiene **exactamente** los valores por defecto del código (el test lo comprueba). Los
ángulos van en grados.

```json
{ "cameras": {
    "follow":   { "distance": 5.5, "pitch": 17, "fov": 50, "targetHeight": 1.3, "recenterDelay": 2, "groundMargin": 0.4, ... },
    "overview": { "distance": 90, "minDistance": 25, "maxDistance": 260, "pitchNear": 38, "pitchFar": 68, "panSpeed": 0.9, "edgeMargin": 8, ... },
    "observe":  { "fov": 35, "framing": 1.25, "padding": 1.5, "orbitSpeed": 6, "sharpness": 2.5, ... },
    "transition": { "seconds": 1.2 } } }
```

- Un documento puede cambiar solo parte (las claves que trae). Sin sección `cameras`, no cambia nada.
- Es atómico: una clave desconocida (una errata), un valor que no es número, fuera de rango o un mínimo por encima del máximo rechazan el documento
  entero y no cambia nada. Dentro de `pack load` rige además la atomicidad de todo el paquete.
- `GameCameraParamsToJson` escribe el juego completo (redondeado a milésimas) y vuelve a cargar igual.

## Editor

```
camera game follow [actor]                 detrás del actor (sin nombre: el que controla el jugador)
camera game overview [x z [distancia]]     vista del clan (sin punto: sobre lo que se miraba)
camera game observe [actor... | all]       cinemática del grupo (sin nombres: todos los actores)
camera game off                            vuelve la cámara del editor
camera game                                qué cámara hay y hacia dónde mira
```

- Controles: en `overview`, WASD o flechas (o el stick izquierdo) panean, Q/E o arrastrar con el botón derecho giran, la rueda, Z/X o los gatillos
  acercan. En `follow` y `observe`, Q/E y el botón derecho giran y la rueda acerca (WASD caminan).
- Fuera de `follow`, el personaje del jugador no recibe las teclas (en `overview` panean).
- Mientras una cámara de juego está activa, las sombras y el tiempo se centran en lo que mira. `camera <yaw> <pitch> <distancia>` (la de siempre)
  devuelve la vista al editor.
- Si el actor seguido desaparece, la cámara pasa sola a `overview`.
- Los comandos `camera ...` no son de simulación: no se graban en las repeticiones.

## Calidad mínima

`quality low` solo quita el posproceso (SSAO, grano, etc.); el coste en una GPU lenta está en el MSAA 4x en HDR a resolución completa, la hierba y el
reflejo del agua. `quality minimum` (también `--quality=minimum` y el menú Quality) es `low` más:

- render a 60 % de la resolución del viewport, escalado al mostrarlo (`stats` dice `viewport WxH (rendered wxh)`);
- sin MSAA y mapa de sombras de 1024 (el `Renderer` se vuelve a crear);
- sin reflejos de pantalla en el agua (`render.ssr`);
- árboles, rocas y objetos con límites a 110 m como mucho (`Scene::DrawDistance`) y la hierba a la mitad de distancia (`Scene::GrassDistanceScale`).

Cualquier otro preset devuelve esos valores; los demás presets no cambian respecto a antes (las imágenes de referencia no se mueven).

**Medidas** (28-10-2026, esta máquina, editor oculto, `generate temperate 5`, la misma vista, viewport 790×617, `stats` tras 100 a 150 fotogramas por
preset). Las medidas son ruidosas porque la máquina compilaba otros carriles a la vez; se alternan los presets para comparar en las mismas condiciones:

| Pasada | `low` | `minimum` | `medium` |
|---|---|---|---|
| 1 (carga moderada) | 304 y 293 ms | 225 ms (474×370; 37 objetos dibujados frente a 188) | 512 ms |
| 2 (carga alta) | 1260 y 882 ms | 701 y 953 ms | – |

`minimum` ahorra entre un 25 y un 45 % del fotograma respecto a `low`. Con tanta variación no es una meta de fotogramas por segundo (eso sigue pendiente,
en escenas de referencia y sin otros procesos); lo que sí se ve es que el coste no está solo en la GPU: el fotograma de 300 ms con 188 objetos apunta
también a la simulación y al editor (ImGui, la tribu, la hierba de 734 770 briznas generada en CPU).

## Pendiente

- Escenas de diplomacia (encuadre de dos grupos enfrentados) y cámaras con raíl para cinemáticas guionizadas.
- Colisión de la cámara con árboles y edificios (hoy solo con el terreno y el lago).
- Los valores de `quality minimum` en datos y una calidad automática que baje el preset si el tiempo de fotograma pasa de un umbral.
- Instanciado y LOD de personajes y vegetación, el resto de H19.
