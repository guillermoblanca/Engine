# Estaciones y ecología básica (H3)

`GB/Gameplay/Ecology.h`.

## Estaciones

`SeasonClock` saca la estación del reloj de simulación (240 s por estación, cuatro por año), así que una partida guardada ya tiene la estación correcta sin guardar nada más.

| Estación | Crecimiento de plantas y cría | Hambre de los animales |
|---|---|---|
| Primavera | x1,2 | x1,0 |
| Verano | x1,0 | x1,0 |
| Otoño | x0,6 | x1,1 |
| Invierno | x0 (nada crece, no se cría) | x1,5 |

El huerto (`GardenSystem::GrowthRate`) usa ese factor: en invierno las plantas no avanzan. La regeneración de los nodos de recurso (árboles, bayas, fibra) usa el mismo factor: en invierno no rebrota nada. La lluvia (`weather rain|lightrain|storm`) riega las parcelas cada 5 s de simulación.

## Conejos

Un conejo es una entidad con sistema de habilidades cuyos atributos son todo su estado (`Food`, `Age`, `Cooldown`, `Starving`): se guardan y se cargan como todo lo demás. El sistema
solo los mueve y decide, sin azar (el paseo sale de un hash del nombre y del tiempo).

- Pasean en torno a donde nacieron.
- Con hambre (comida < 55) buscan la planta más cercana (arbustos de bayas y matas de fibra: **las mismas que recoge la tribu**) y se la comen: compiten con ella.
- Con comida > 80, adultos (120 s) y fuera de invierno, crían (enfriamiento de 150 s) hasta un máximo de 8.
- Sin comida mueren a los 40 s.

## En el editor

```
ecology spawn [n] [x z]                       suelta conejos
ecology status                                estación y población
ecology season [spring|summer|autumn|winter]  muestra o cambia la estación (mueve el reloj)
```

El panel **World state** muestra la estación, los animales con su comida, los interactivos, los nodos de recurso y las parcelas del huerto.

**Qué se guarda.** Los atributos (comida, edad, enfriamiento, hambre) y la posición, sí. **Hacia dónde iba** el conejo (su centro de paseo, la planta elegida y lo que lleva comiéndola) no: tras
cargar, cada conejo toma como centro el sitio donde está y elige de nuevo, así que la partida cargada no sigue paso a paso igual que la original aunque tenga los mismos números.

## Coste

Cada conejo cuesta unos 5 a 25 µs por fotograma con la escena de un mundo generado (3.000 entidades): 8 conejos, 0,03 ms; 40, 0,06 ms; 120, 0,12 ms (`tests/scale_test.cpp`, `GB_SCALE_BENCH=1`).
Antes de guardar el id de la entidad y listar las plantas una vez por fotograma era 0,35 / 1,8 / 5,7 ms: la búsqueda de una entidad por nombre recorre toda la escena.

## Test

`tests/ecology_test.cpp`: las estaciones siguen el reloj (y no se rompen con una estación de duración 0), con comida los conejos comen, crían y no pasan del máximo, sin plantas los cuatro mueren, dos ejecuciones dejan a
cada conejo en el mismo sitio y una pradera guardada y cargada a mitad conserva a los mismos conejos y plantas con los mismos números y sigue funcionando.

## Límites conocidos

- Solo hay un animal (conejo) y ningún depredador; la comida de los conejos y la de la tribu es la misma, y con pocos arbustos se la quitan.
- Las estaciones no cambian aún el aspecto del terreno ni de los árboles, ni el clima de forma automática.
