# La sociedad del clan (H9 + H10)

Demografía y herencia (H9) y relaciones, personalidad y cultura (H10). Quién nace de quién, cuántos años tiene cada uno, qué ha heredado, qué clase de
persona es, a quién aprecia y qué valora el clan entero. Lógica pura de CPU, determinista (cada «dado» es un hash de la semilla, un nombre y un contador;
sin generador global de azar) y casi todo en datos. Tests: `tests/society_test.cpp` (test `society`).

| Pieza | Dónde | Qué es |
|---|---|---|
| `SocietyRules`, `LoadSocietyRules`, `DefaultSocietyRules` | `GB/Game/Society.h`, `SocietyRules.cpp` | Las reglas como datos (sección `society`) |
| `Society`, `Person` | `GB/Game/Society.h`, `Society.cpp` | El estado: personas, vínculos, normas, tradiciones; los días; guardado |
| Ganchos en `Tribe` | `GB/Game/TribeSociety.cpp` (y pocas líneas en `Tribe.cpp`, `TribeWork.cpp`) | Nacimientos con cuerpo nuevo, muertes, factores sobre las opciones, eventos entre miembros |
| Enseñanza preferente | `KnowledgeAgent::Learn/Teach/Mentors` en `GB/Game/Knowledge.h` | Los padres enseñan primero; un niño aprende más rápido, un mayor enseña mejor |
| `Actor::SetHeight`, `ITribeBody::SetGrowth` | `GB/Game/Actor.h`, `GB/Game/Tribe.h` | La estatura de los niños |

## Datos: la sección `society`

Va en `assets/data/era1.json` y se registra en los paquetes como sección propia (`SocietyContentSection`, `AddSocietyContentSections`): atómica, una
parte (`life`, `births`, `genes`, `traits`, `relations`, `culture`) reemplaza esa parte entera y lo que no dice es el valor por defecto. `DefaultSocietyRules()`
dice lo mismo que el archivo (lo comprueba el test).

```json
"society": {
  "seed": 1, "mutation": 0.06,
  "life": { "daySeconds": 0, "daysPerYear": 4, "childUntil": 14, "elderFrom": 52, "maxAge": 72, "longevity": 8, "oldAge": 0.35,
            "founderMin": 16, "founderMax": 40, "heavy": [ "woodcutting", "mining", "firekeeping", "tool", "fire", "stoke" ],
            "childWork": 0.15, "elderWork": 0.6, "childLearn": 1.6, "childTeach": 0.5, "elderTeach": 1.5, "childPace": 0.7, "elderPace": 0.75,
            "childHeight": 0.55, "childRole": "forager" },
  "births": { "pairAge": 16, "ageGap": 15, "pairMin": -0.1, "minAge": 16, "maxAge": 42, "spacing": 2, "chance": 0.12, "foodPerMember": 1.5,
              "needsFire": true, "maxMembers": 200 },
  "genes": { "strength": { "mean": 0.5, "spread": 0.2, "effects": { "woodcutting": 0.25, "mining": 0.25 }, "pace": { "woodcutting": 0.2, "mining": 0.2 } }, ... },
  "traits": { "curiosity": { "mean": 0.5, "spread": 0.25, "heritability": 0.5, "effects": { "experiment": 0.8, "explore": 0.5 } }, ... },
  "relations": { "events": { "work": 0.02, "food": 0.03, "camp": 0.015, "conflict": -0.12, "teach": 0.04, "pair": 0.3, "birth": 0.4, "help": 0.1,
                 "theft": -0.4, "rescue": 0.4 }, "decayPerDay": 0.03, "keep": 0.02, "maxPerMember": 12, "kin": 0.3, "campSeconds": 30, "campRadius": 6,
                 "campNearest": 2, "workRadius": 10 },
  "culture": { "drift": 0.05, "norms": { "industry": { "start": 0.5, "trait": "diligence", "effects": { "gather": 0.2, "rest": -0.3 } }, ... },
               "traditions": { "fire_vigil": { "name": "Fire vigil", "event": "stoke", "count": 30, "effects": { "warm": 0.15 } }, ... } }
}
```

**Efectos sobre las opciones** (`effects`): clave = tipo de opción del cerebro (`gather`, `explore`, `experiment`, `socialize`, `rest`, `sleep`, `tool`,
`fire`, `stoke`, `warm`, `regroup`, `deposit`...) o familia de aptitud (`woodcutting`, `mining`, `foraging`, `crafting`, `firekeeping`) para las opciones de esa
familia. Un valor centrado (gen, rasgo, norma) da el factor `1 + fuerza × (2v − 1)`: el miembro medio queda igual; una tradición (se tiene o no) da `1 + fuerza`.

## Ciclo de vida (H9)

- **Edad** en años: un año son `daysPerYear` días simulados (el día del reloj de las necesidades, 1200 s, o `daySeconds` si no es 0). Con los datos de
  era 1 un año son 80 minutos de simulación: 200 años, unas 270 horas (a `timescale 20`, unas 13).
- **Etapas**: niño (< `childUntil`), adulto, mayor (≥ `elderFrom`). Efectos:
  - Un niño deja el trabajo pesado (`heavy`: talar, picar, la hoguera, las herramientas; factor `childWork`), aprende `childLearn` veces más rápido
    (aptitudes y lecciones), enseña peor (`childTeach`), trabaja más despacio (`childPace`), tiene el oficio de los niños (`childRole`) y la revisión de
    oficios no lo toca; crece de `childHeight` a 1 de estatura (`ITribeBody::SetGrowth`; el actor del editor cambia de escala con `Actor::SetHeight`).
  - Un mayor hace menos trabajo pesado (`elderWork`), enseña mejor (`elderTeach`) y trabaja más despacio (`elderPace`).
- **Fundadores**: `tribe build` (o `AddMember`) da a cada miembro una edad entre `founderMin` y `founderMax`, sexo alterno (cuatro fundadores son dos
  parejas posibles) y genes y rasgos del hash de la semilla y su nombre.
- **Pareja**: cada día, de todas las parejas posibles (libres, de `pairAge` o más, de distinto sexo, sin parentesco, con menos de `ageGap` años de
  diferencia y afinidad ≥ `pairMin`), primero la de más afinidad. La viuda o el viudo pueden volver a emparejarse.
- **Nacimientos**: una pareja tiene un hijo con probabilidad `chance` al día si la madre tiene entre `minAge` y `maxAge`, su último hijo fue hace
  `spacing` años o más, el almacén tiene `foodPerMember` comida por miembro, hay hoguera (`needsFire`: el abrigo) y el clan no pasa de `maxMembers`.
  El hijo **es un cuerpo nuevo**: la tribu pide uno con `Tribe::SetSpawner` (el editor genera un actor del modelo de la tribu junto a la madre; los tests,
  un cuerpo de prueba) y toma el siguiente nombre libre de los datos (`work.names`). Sin `spawner` no hay hijos (el registro lo dice).
- **Muertes**: de vejez (cada día, a partir de `elderFrom`, probabilidad anual `oldAge × ((edad − elderFrom)/(límite − elderFrom))²`; nadie pasa del límite
  `maxAge ± longevity` según el gen `longevity`) y por las necesidades de siempre (hambre, sed, frío; [IA.md](IA.md)). La muerte libera a la pareja, borra
  los vínculos del muerto y cuenta para las tradiciones.

## Herencia

- **Genes** (0..1): `strength`, `endurance`, `perception`, `dexterity`, `longevity` y uno por rasgo de personalidad. Un fundador los saca alrededor de su `mean`
  (± `spread`, en campana de tres tiradas); un hijo, mezclando los de sus padres (una proporción al azar por gen: recombinación) más una mutación de hasta
  `mutation`. Qué hace cada gen es dato: `effects` (gustos: la fuerza, talar y picar), `learn` (sesgo de la velocidad de aprendizaje de una familia: la
  destreza, `crafting`) y `pace` (sesgo de la velocidad de trabajo).
- **Las aptitudes no se heredan** (empiezan en 0, H7), pero sí lo rápido que se aprenden (`learn`). **El conocimiento** pasa enseñando: en cada lección
  (H11) el aprendiz aprende de sus *mentores* si están en el campamento (sus padres vivos, luego los que más aprecia), con `teach × Learn × Teach`.

## Personalidad (H10)

Cuatro rasgos: `sociability` (charlar, reagruparse), `diligence` (recoger, llevar, hacer la herramienta; menos descanso), `curiosity` (experimentar,
explorar) y `boldness` (explorar; menos miedo). Cada uno = `heritability × gen + (1 − heritability) × crianza`, donde la crianza es la media de los
rasgos de los padres acercada a la norma del clan que sigue ese rasgo. El inspector muestra el factor `nature` (genes y rasgos), `life` (etapa) y `culture`
(normas y tradiciones) en cada opción.

## Relaciones

Grafo **disperso** de vínculos por pares (−1..1), simétrico: solo pares con algo que contar. Suben o bajan con eventos (`relations.events`):

| Evento | Cuándo |
|---|---|
| `work` | al acabar una recogida, con los (hasta 3) que recogen a menos de `workRadius` |
| `food` | al comer del almacén, con quien trajo la última comida (× 0,5 + la norma `sharing`) |
| `camp` | cada `campSeconds`, cada miembro en el campamento con sus `campNearest` más cercanos a menos de `campRadius` |
| `conflict` | encontrar vacío un nodo que otro vació («is cross with Biel: Biel emptied Bush 3») |
| `teach` | una lección, entre aprendiz y maestro |
| `pair`, `birth` | al emparejarse; entre el recién nacido y sus padres |
| `help`, `theft`, `rescue` | eventos controlados (editor: `tribe relate`; tests) |
| `care` | H14: un miembro termina de curar las heridas de otro ([CONFLICTO.md](CONFLICTO.md)) |


Cada día un vínculo pierde `decayPerDay` de sí mismo; por debajo de `keep` desaparece, y cada miembro guarda solo sus `maxPerMember` más fuertes. El
**parentesco** (pareja, padres, hijos, hermanos) suma `kin` a la afinidad. Se usa para elegir pareja, para el compañero que busca quien se siente solo
(la opción `socialize` va al que más aprecia de los vistos hace poco, con el factor `friend`) y para quién enseña a quién.

## Cultura

- **Normas** del clan (0..1): `sharing`, `industry`, `inquiry`, `daring`. Cada día se acercan `drift` a la media del rasgo que siguen entre los adultos vivos y
  escalan las opciones de todos (son la agenda del clan: un clan diligente valora la industria y descansa menos). El registro dice cuándo una norma pasa
  de 0,65 o baja de 0,35 («the clan values industry more and more (0.66)»).
- **Tradiciones**: la `count`-ésima vez que pasa un evento del clan (`stoke`, `fire`, `craft`, `learn`, `birth`, `death`, `pair`) nace una tradición con
  nombre, que se queda: «the clan keeps a new tradition: Fire vigil (30 times stoke)».

## Guardado

Todo va en la sección `ai` de la tribu, en campos opcionales: `society` (personas con edad, sexo, padres, pareja, genes y rasgos; vínculos; normas,
contadores y tradiciones; días, nacimientos, muertes y el reloj del día), `campTimer`, `foodBy` y `lastWorker`. Una partida anterior carga con los miembros
como fundadores (edad y genes de su nombre). Los hijos son actores normales: se guardan y se rehacen con la partida (su `Kind`); al cargar recuperan su
estatura.

## Editor

```
tribe family <nombre>          edad, etapa, sexo, padres, pareja, hijos, hermanos; genes y personalidad
tribe relations <nombre>       sus vínculos (con el parentesco)
tribe culture                  normas, tradiciones, días, vivos, nacidos, muertos, vínculos
tribe relate <a> <b> <evento>  un evento entre dos (rescue, theft, help...)
tribe day                      un día de la sociedad ahora (edades, muertes, parejas, nacimientos, normas)
```

Panel **Tribe**: sección **Society** (población por etapas, nacidos, muertos, vínculos, normas con barras y tradiciones) y, en el inspector del miembro,
**Life and family** (edad, etapa, familia, genes y personalidad con barras) y **Relations** (vínculos con color y parentesco).

## Test

`tests/society_test.cpp` (test `society`): la sección dice lo mismo que el código, va y vuelve y un error la deja intacta (también dentro de un paquete);
los fundadores son deterministas; una población de 30 crece con comida de sobra, se mantiene con comida para su tamaño y se extingue sin excedente en 200
años, sin fuego no tiene hijos, dos ejecuciones son idénticas y guardada a los 100 años y reanudada acaba igual; la herencia sigue al promedio de los
padres (pendiente ≈ 1 en cuatro semillas) y la media y la dispersión se mantienen; los vínculos suben, bajan, se apagan, se recortan y deciden pareja y
mentores; la lección prefiere a la madre y un niño aprende más rápido de un mayor; la curiosidad sube el experimentar, la etapa quita el trabajo pesado,
las normas derivan y las tradiciones nacen; una tribu con cuerpos de prueba pierde a un anciano de vejez, empareja, tiene un hijo con cuerpo nuevo y
pequeño, se guarda y se carga (también sin sociedad), y dos ejecuciones acaban igual; un clan crea vínculos trabajando y en el campamento; y un clan
diligente lleva más al almacén que uno ocioso.

## Vida social (ánimo, conversaciones, ritos, creencias)

Encima de las relaciones y la cultura, la **vida social** del clan ([VIDA_SOCIAL.md](VIDA_SOCIAL.md), sección `social` de `assets/data/social.json`):
cada miembro tiene **ánimo** y **duelo**; las **conversaciones** (charla, historia, chisme, enseñanza, consuelo, zanjar una disputa, saludo,
compartir comida, acicalarse, juego de niños) suben o bajan los vínculos de esta página con `Society::Adjust` y la necesidad `Company`; los
**ritos de paso** (nombre, mayoría de edad, pareja, funeral) nacen de los mismos eventos de la sociedad (`birth`, `adult`, `pair`, `death`) y
las **tradiciones** que nacen se vuelven ritos que vuelven cada pocos días. Un rito acerca las normas del clan (`Society::NudgeNorm`) y une a
quienes están en el círculo. Una tribu sin la sección `social` (la de los tests de esta página) no tiene nada de eso.

## Límites y pendiente

- **Modelos de niños y mayores**: el niño es el mismo modelo escalado (`Actor::SetHeight`); faltan proporciones, animaciones y retargeting propios (plan,
  apartado 7). El modelo de los hijos es el de `tribe build`; tras cargar una partida es `fox` hasta el siguiente `tribe build`.
- Sin embarazo, enfermedad ni muerte violenta (H14); sin liderazgo ni sucesión, estado de ánimo, deudas ni conversación con gestos (H10 completo).
- Los muertos siguen en la lista de la tribu (con su actor); en 200 años de partida real la lista crece: haría falta retirarlos y archivar la genealogía.
- La aceptación de 200 años se prueba con la demografía sola (`Society::Day` sin cuerpos); con la tribu completa, solo días cortos. El barrido de semillas
  del clan entero (H20) diría si las tasas de nacimientos y la deriva de normas están bien equilibradas; los clanes con líder agresivo o pacífico son H13/H14.
