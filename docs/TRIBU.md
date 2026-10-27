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

Un trabajador sin su herramienta va al campamento a hacerla si el almacén tiene los materiales; si no, ayuda recogiendo lo que falta. Lleva lo que recoge al
almacén cuando su mochila llega a 4 unidades, y deja de recoger un recurso cuando el almacén tiene 40. El leñador enciende la hoguera cuando todos tienen su
herramienta y el almacén puede pagarla (la hoguera no se come la piedra que aún necesita el pico).

## En el editor

```
tribe build [modelo.fbx|fox] [x z] [trabajadores 2-6]   crea el parche de recursos, el campamento y los trabajadores
tribe status                                            almacén y qué hace cada uno
tribe log [n]                                           las últimas n líneas de lo que ha pasado
timescale <1-20>                                        acelera la simulación (el día de la tribu en un minuto)
```

Los trabajadores son actores normales (`actor list`, `actor bag <nombre>`): se guardan con la partida. El estado de la tribu (almacén, trabajos en curso) aún no.

## Test

`tests/tribe_test.cpp` ejecuta cuatro trabajadores con cuerpos de prueba que interpretan el mismo texto de tareas que un actor real durante 10 minutos de
simulación: se hacen las dos herramientas y la hoguera, ningún contador baja de cero y dos ejecuciones acaban idénticas.

## Límites conocidos

- Con los recursos agotados (las rocas no se regeneran) la tribu se queda sin nada que hacer y descansa. Faltan comida, hambre y necesidades (H5).
- No hay decisiones colectivas ni roles que cambien: el reparto es fijo (H9 a H12).
- El estado de la tribu no se guarda todavía.
