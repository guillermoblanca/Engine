# La primera tribu (H2 + H3)

`GB/Game/Tribe.h`: un puñado de trabajadores que, sin que nadie los dirija, recogen recursos, los llevan a un almacén común, fabrican sus herramientas y
encienden una hoguera. Es la sociedad más sencilla posible: reglas sobre las mismas habilidades, objetos y nodos de recurso que usa el jugador
([OBJETOS.md](OBJETOS.md)), así que lo que hace un trabajador es lo que podría hacer el jugador. Es determinista (los empates se rompen por nombre, sin azar).

## Roles

| Rol | Qué hace |
|---|---|
| `forager` (recolector) | Fibra y bayas con las manos |
| `woodcutter` (leñador) | Talar árboles; necesita un hacha |
| `miner` (minero) | Picar rocas; necesita un pico |

Los trabajadores ya no siguen un if/else por rol: cada uno es un **agente** ([IA.md](IA.md)) que ve y oye, recuerda lo que ha visto (y lo comparte con el clan en el campamento), tiene
necesidades (hambre, sed, descanso, abrigo, seguridad, compañía) y, cuando queda libre, puntúa lo que puede hacer (comer, beber, dormir, calentarse, fabricar su herramienta, recoger,
llevar al almacén, encender la hoguera, explorar, descansar) y elige. **El rol es un conjunto de pesos** sobre esas opciones: el recolector prefiere bayas y fibra, el leñador árboles y la
hoguera, el minero rocas; todos ayudan con lo que falta. El hambre sustituye al antiguo temporizador de 40 s: un trabajador come una baya (primero la que lleva, luego la del almacén)
cuando la necesidad aprieta más que su trabajo, y la hoguera quema un trozo de madera cada 30 s (si no hay madera se apaga y vuelve a arder cuando la haya): así hay siempre algo que
reponer y nadie se queda sin trabajo para siempre. Cada uno espera y duerme en su propio sitio alrededor del campamento.

Un trabajador sin su herramienta va al campamento a hacerla si el almacén tiene los materiales; si no, ayuda recogiendo lo que falta. Lleva lo que recoge al
almacén cuando su mochila llega a 4 unidades, y deja de recoger un recurso cuando el almacén tiene 40. El leñador enciende la hoguera cuando todos tienen su
herramienta y el almacén puede pagarla (la hoguera no se come la piedra que aún necesita el pico). Solo recogen lo que **recuerdan**: al principio miran a su alrededor (parados giran
la cabeza) y, si no conocen lo que su oficio pide, salen a explorar.

## En el editor

```
tribe build [modelo.fbx|fox] [x z] [trabajadores 2-6]   crea el parche de recursos, el campamento y los trabajadores
tribe status                                            almacén y qué hace cada uno
tribe log [n]                                           las últimas n líneas de lo que ha pasado
ai debug|status|memory|scream                           el cerebro de los miembros (ver IA.md)
timescale <1-20>                                        acelera la simulación (el día de la tribu en un minuto)
```

Los trabajadores son actores normales (`actor list`, `actor bag <nombre>`): se guardan con la partida, y el estado de la tribu (almacén, hoguera, memoria y decisión de cada uno, sus
necesidades) va en la sección `ai` del guardado: al cargar siguen donde estaban. `tribe build` pone también un pozo cerca del campamento. Una partida guardada con un trabajador a medias de una habilidad vuelve con la habilidad cancelada y sin `State.Busy` (antes la tribu entera quedaba bloqueada tras cargar).

## Panel Tribe y selección

- Panel **Tribe** (se pone al frente al hacer `tribe build`; View > Tribe): tiempo, estado de la hoguera, almacén con barras, tabla de miembros (qué hacen y qué llevan), el
  miembro seleccionado con su inventario, sus órdenes, sus necesidades, **qué decidió y por qué** (las opciones con su puntuación) y lo que sabe, y el registro de lo ocurrido. Clic en un nombre lo selecciona; doble clic va hasta él.
- **Clic en un personaje en el viewport** lo selecciona: el panel *Abilities* muestra el nombre del personaje, su rol y su inventario. Clic en una parcela, un árbol o un
  arbusto muestra sus habilidades y atributos.
- Por qué se paran: cuando el almacén llega a 40 de un recurso nadie recoge más de él (`StockCap`), y si no queda ningún nodo de lo que toca (las rocas no se regeneran)
  descansan. El panel y `tribe status` dicen cuál es el motivo.

## Test

`tests/tribe_test.cpp` ejecuta cuatro trabajadores con cuerpos de prueba que interpretan el mismo texto de tareas que un actor real durante 10 minutos de
simulación: se hacen las dos herramientas y la hoguera, ningún contador baja de cero y dos ejecuciones acaban idénticas. El cerebro tiene los suyos (`perception`, `ai`, ver [IA.md](IA.md)).

## Límites conocidos

- Con los recursos agotados (las rocas no se regeneran) el minero espera en el campamento y, si no conoce ninguna roca, sale a explorar; la tribu no hace nada más con ello.
- No hay decisiones colectivas ni roles que cambien: los pesos de cada rol son fijos (H9 a H12), y la herramienta de cada oficio sigue ligada al rol.
- El almacén es un único inventario del campamento; no hay otros almacenes ni caminos (la navegación es otra parte de H5).
- Más límites de percepción, necesidades y decisión en [IA.md](IA.md).
