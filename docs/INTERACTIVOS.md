# Interactivos con estado (H3)

`GB/Gameplay/Interactives.h`: cosas del mundo con un estado propio, hechas como las parcelas del huerto: una entidad con sistema de habilidades cuyo estado son
**etiquetas** y **atributos**, así que se guardan y se cargan con el resto del estado de juego (`docs/GUARDADO.md`) y los personajes los usan con las mismas
habilidades que el jugador.

| Interactivo | Estado | Habilidad | Qué pasa |
|---|---|---|---|
| Fogata (`Fire Pit`) | `unlit`, `lit`, `out`; atributo `Fuel` (0..10) | `Stoke` (necesita madera en la mochila) | Cada trozo de madera da 2 de combustible; arde 1 por cada 6 s; llama y luz solo mientras arde |
| Pozo (`Well`) | atributo `Water` (0..6) | `DrawWater` | Da 2 de agua a la mochila por uso; recupera 1 cada 20 s |
| Puerta (`Gate`) | `open`, `closed` | `OpenDoor`, `CloseDoor` | La hoja gira sobre su bisagra; cerrada bloquea el paso (`DoorObstacles`) |

## En datos

Qué es cada interactivo está en la sección `interactives` de [`assets/data/world.json`](../assets/data/world.json) (un fichero del paquete `era1`, ver [PAQUETES.md](PAQUETES.md)) (`InteractiveDef`): nombre de la entidad, posición en
`interactives build`, punto de trabajo, etiquetas y atributos iniciales (las capacidades: `Fuel` 0 de 10, `Water` 6 de 6), temporizadores (`burnSeconds`, `refillSeconds`,
`refillAmount`, `swingDegrees`, `swingSpeed`), cantidades (`fuelPerWood`, `waterPerDraw`) y las habilidades que lo usan (nombre, montaje, notify, evento, requisitos sobre quien
la usa y sobre el objetivo, `targetMin`). El código solo conserva los tres **comportamientos** (`kind`): `campfire` (quema `Fuel`, escucha `Event.Fuel.Add`), `well` (rellena
`Water`, escucha `Event.Water.Draw`) y `door` (abre y cierra, escucha `Event.Door.Open` y `Event.Door.Close`). Un hogar más grande, una cisterna u otra puerta son unas líneas
de JSON sin compilar.

El editor lo lee al arrancar (sección `interactives` del registro de paquetes) y `content load <archivo>` o `pack load` lo vuelven a leer (añade o sustituye por id; si algo está mal no cambia nada y dice por qué). Una instancia
sigue la definición de su nombre o, si no hay, la primera de su tipo; un atributo `BurnSeconds` propio de la entidad manda sobre el `burnSeconds` de la definición
(la hoguera de la tribu arde a su ritmo). El sistema adopta en su `Update` los interactivos creados después de `Bind`.

Editor: `interactives build [x z]` los coloca y `interactives status` muestra su estado. Ejemplo:

```
actor bag worker add wood 4
actor tasks worker ability Stoke Fire_Pit | wait 1 | ability DrawWater Well | wait 1 | ability OpenDoor Gate
```

Todos los actores tienen estas habilidades, y también `Chop`, `Mine` y `Gather` de los nodos de recurso ([OBJETOS.md](OBJETOS.md)). Las etiquetas de los objetos de la
mochila (`Item.Resource.Wood`, `Item.Tool.Axe`) pasan a ser etiquetas del actor, y por eso una habilidad puede exigir "llevar madera".

Test `interactives`: un trabajador enciende la fogata, la deja apagarse, saca agua hasta vaciar el pozo y esperar a que se llene, y abre y cierra la puerta; los valores
no salen de sus límites y dos ejecuciones acaban igual. El test `resources` añade un bosque talado que se regenera en diez minutos de simulación acelerada.

Test `interactives` también: una partida guardada a medias (fogata encendida, puerta abierta) vuelve igual, y la fogata cargada se apaga a su hora; el archivo
`world.json` dice lo mismo que `DefaultInteractives`, un documento roto no cambia nada, un hogar nuevo definido solo en datos funciona con su propia habilidad y su
ritmo, y una fogata creada después de `Bind` se aviva.

## Qué se guarda y qué no

El estado (etiquetas `State.*`) y las cantidades (`Fuel`, `Water`) van en el componente de la entidad; los temporizadores del sistema (la fracción de segundo que lleva ardiendo la fogata, el tiempo hasta la próxima gota del pozo) van en la sección `interactives`. Una fogata cargada se apaga en el mismo tic que la original (test `interactives`).

## Hecho y pendiente de H3

Hecho (en otros documentos): estaciones y crecimiento, conejos y zorros ([ECOLOGIA.md](ECOLOGIA.md)) y el panel **World state** del editor, que lista fogatas, pozos y
puertas con su estado.

**La hoguera de la tribu es este interactivo**: al encenderla la tribu construye un `fire_pit` llamado `Campfire` en el campamento, encendido con el calor del kit, y un
miembro la aviva con `Stoke` (la misma regla de combustible que el jugador) cuando le quedan 2 o menos. Ver [TRIBU.md](TRIBU.md).

Pendiente:
- Colocar interactivos desde el panel (hoy `interactives build`, que coloca todos los de los datos).
