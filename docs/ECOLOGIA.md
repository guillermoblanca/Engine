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

## Cómo se ven

Con `tribe build`, `ecology spawn` o `ecology season` la estación pasa a mandar sobre el aspecto del mundo (`ecology visuals on|off` lo activa o lo apaga; no cambia nada si no hay animales ni tribu):

| Estación | Clima | Nieve en el suelo | Copas y arbustos de los nodos |
|---|---|---|---|
| Primavera | lluvia ligera el primer 20 % | 95 % que se derrite en el primer cuarto | verde claro |
| Verano | despejado (tormenta a mitad de estación) | 0 | verde |
| Otoño | lluvia el último tercio | un primer copo al final | naranja |
| Invierno | nevada hasta el 90 % | cae durante el primer 15 % y se queda | escarchados |

Todo sale de la estación y de cuánto lleva (`SeasonClock::Weather`, `SnowCover`, `Foliage`), sin guardar nada. Los colores se mezclan con los de la estación siguiente en el último 30 %, y el
editor solo escribe las propiedades (`weather.preset`, `ground.snow`) cuando cambian.

## Especies en datos

Las especies están en la sección `species` de [`assets/data/world.json`](../assets/data/world.json) (`SpeciesDef`): nombre, plural, etiqueta (`Animal.*`), aspecto
(`body`: `rabbit` o `quadruped`, color y tamaño), qué come (`eats`: etiquetas de plantas, como `Resource.Plant`, **o** de animales, como `Animal.Rabbit`), alcance de la
vista y los números de su vida (máximo, velocidad, hambre, cría, cuánto aguanta sin comer...). El editor la lee al arrancar y con `content load`; un documento roto no
cambia nada. Los valores por defecto del conejo son los de siempre, así que sin zorros los conejos hacen exactamente lo mismo (test `ecology`).

| Especie | Come | Notas |
|---|---|---|
| Conejo (`rabbit`) | plantas (`Resource.Plant`): arbustos de bayas y matas de fibra | hasta 8, ver abajo |
| Zorro (`fox`) | conejos (`Animal.Rabbit`) | hasta 2; 2,4 m/s (el conejo va a 1,3); con hambre (< 50) persigue al conejo más cercano a menos de 18 m, lo sujeta 4 s y se lo come (+55); cría lento (adulto a 200 s, enfriamiento 300 s); sin comer muere a los 120 s |

Un ciervo que pace es unas líneas de JSON (`"tag": "Animal.Deer", "body": "quadruped"`). Los contadores van por especie (nacidos, muertos de hambre, comidas, cazados) y se
guardan en la sección `ecology`; una partida antigua (solo conejos) carga sus contadores en los conejos.

## Conejos

Un conejo es una entidad con sistema de habilidades cuyos atributos son todo su estado (`Food`, `Age`, `Cooldown`, `Starving`): se guardan y se cargan como todo lo demás. El sistema
solo los mueve y decide, sin azar (el paseo sale de un hash del nombre y del tiempo).

- Pasean en torno a donde nacieron.
- Con hambre (comida < 55) buscan la planta más cercana (arbustos de bayas y matas de fibra: **las mismas que recoge la tribu**) y se la comen: compiten con ella.
- Con comida > 80, adultos (120 s) y fuera de invierno, crían (enfriamiento de 150 s) hasta un máximo de 8.
- Sin comida mueren a los 40 s.

## En el editor

```
ecology spawn [n] [x z] [especie]             suelta conejos (o zorros: ecology spawn 1 fox)
ecology status                                estación y población
ecology season [spring|summer|autumn|winter]  muestra o cambia la estación (mueve el reloj)
```

El panel **World state** muestra la estación, los animales con su comida, los interactivos, los nodos de recurso y las parcelas del huerto.

**Qué se guarda.** Los atributos (comida, edad, enfriamiento, hambre) y la posición van con la entidad; **hacia dónde iba** el conejo (su centro de paseo, la planta elegida, lo que lleva comiéndola) y los contadores van en la sección `ecology` ([GUARDADO.md](GUARDADO.md)): una partida cargada sigue paso a paso igual que la original (test `ecology`).

## Coste

Cada conejo cuesta unos 5 a 25 µs por fotograma con la escena de un mundo generado (3.000 entidades): 8 conejos, 0,03 ms; 40, 0,06 ms; 120, 0,12 ms (`tests/scale_test.cpp`, `GB_SCALE_BENCH=1`).
Antes de guardar el id de la entidad y listar las plantas una vez por fotograma era 0,35 / 1,8 / 5,7 ms: la búsqueda de una entidad por nombre recorre toda la escena.

## Test

`tests/ecology_test.cpp`: las estaciones siguen el reloj (y no se rompen con una estación de duración 0), con comida los conejos comen, crían y no pasan del máximo, sin plantas los cuatro mueren, dos ejecuciones dejan a
cada conejo en el mismo sitio y una pradera guardada y cargada a mitad conserva a los mismos conejos y plantas con los mismos números y sigue funcionando. Además: `world.json` dice
lo mismo que `DefaultSpecies`, los documentos rotos no cambian nada, dos zorros cazan conejos y viven de ellos sin dejar entidades a medias, sus contadores se guardan, y sin la
especie zorro la pradera acaba igual.

## Límites conocidos

- Los conejos no huyen del zorro ni la tribu caza (ni el zorro ataca a la tribu); la comida de los conejos y la de la tribu es la misma, y con pocos arbustos se la quitan.
- El color de estación solo llega a los nodos de recurso (los árboles del mundo generado conservan el suyo); el terreno y la hierba no cambian de color, solo la nieve.
