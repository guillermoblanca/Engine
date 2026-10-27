# Interactivos con estado (H3)

`GB/Gameplay/Interactives.h`: cosas del mundo con un estado propio, hechas como las parcelas del huerto: una entidad con sistema de habilidades cuyo estado son
**etiquetas** y **atributos**, así que se guardan y se cargan con el resto del estado de juego (`docs/GUARDADO.md`) y los personajes los usan con las mismas
habilidades que el jugador.

| Interactivo | Estado | Habilidad | Qué pasa |
|---|---|---|---|
| Fogata (`Fire Pit`) | `unlit`, `lit`, `out`; atributo `Fuel` (0..10) | `Stoke` (necesita madera en la mochila) | Cada trozo de madera da 2 de combustible; arde 1 por cada 6 s; llama y luz solo mientras arde |
| Pozo (`Well`) | atributo `Water` (0..6) | `DrawWater` | Da 2 de agua a la mochila por uso; recupera 1 cada 20 s |
| Puerta (`Gate`) | `open`, `closed` | `OpenDoor`, `CloseDoor` | La hoja gira sobre su bisagra (visual; aún no abre ni cierra la colisión) |

Editor: `interactives build [x z]` los coloca y `interactives status` muestra su estado. Ejemplo:

```
actor bag worker add wood 4
actor tasks worker ability Stoke Fire_Pit | wait 1 | ability DrawWater Well | wait 1 | ability OpenDoor Gate
```

Todos los actores tienen estas habilidades, y también `Chop`, `Mine` y `Gather` de los nodos de recurso ([OBJETOS.md](OBJETOS.md)). Las etiquetas de los objetos de la
mochila (`Item.Resource.Wood`, `Item.Tool.Axe`) pasan a ser etiquetas del actor, y por eso una habilidad puede exigir "llevar madera".

Test `interactives`: un trabajador enciende la fogata, la deja apagarse, saca agua hasta vaciar el pozo y esperar a que se llene, y abre y cierra la puerta; los valores
no salen de sus límites y dos ejecuciones acaban igual. El test `resources` añade un bosque talado que se regenera en diez minutos de simulación acelerada.

Test `interactives` también: una partida guardada a medias (fogata encendida, puerta abierta) vuelve igual, y la fogata cargada se apaga a su hora.

## Qué se guarda y qué no

El estado (etiquetas `State.*`) y las cantidades (`Fuel`, `Water`) van en el componente de la entidad; los temporizadores del sistema (la fracción de segundo que lleva ardiendo la fogata, el tiempo hasta la próxima gota del pozo) van en la sección `interactives`. Una fogata cargada se apaga en el mismo tic que la original (test `interactives`).

## Hecho y pendiente de H3

Hecho (en otros documentos): estaciones y crecimiento ([ECOLOGIA.md](ECOLOGIA.md)), animales con ecología básica (conejos) y el panel **World state** del editor, que lista fogatas, pozos y puertas con su estado.

Pendiente:
- La colisión de la puerta.
- Que la hoguera de la tribu sea un interactivo de este tipo: hoy son dos cosas distintas (la hoguera que construye la tribu se llama `Campfire` y es solo visual; el interactivo `Fire Pit` no está ligado a la tribu ni se puede avivar desde ella).
- Colocar interactivos desde el panel (hoy `interactives build`, que coloca siempre los tres).
