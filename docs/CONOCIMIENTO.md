# Conocimiento y tecnología (H11, E4 + E5)

Qué sabe cada agente y cada clan, cómo se descubre una tecnología y cómo se difunde. Lógica pura de CPU, determinista (hash sembrado, sin generador
global de azar). Tests: `tests/knowledge_test.cpp` (datos, puertas, descubrimiento, difusión, guardado y validador) y el bloque «Knowledge» de
`tests/tribe_test.cpp` (la tribu descubre el tallado de sílex por sí sola).

| Pieza | Dónde | Qué es |
|---|---|---|
| `TechDef`, `TechTree`, `LoadTechs`, `DefaultTechs` | `GB/Gameplay/Techs.h` | Las tecnologías como datos (sección `techs` y reglas `knowledge`) |
| `ClanKnowledge` | `GB/Game/Knowledge.h` | Qué sabe cada agente y el clan; experimentar, observar, enseñar, olvidar; guardado |
| `RecipeBook::WhyNot/Craft(..., known)` | `GB/Gameplay/Items.h` | Las recetas con puerta: `known` es el conjunto de recetas que sabe hacer quien fabrica; `nullptr` = todas (las llamadas de antes) |
| `CheckReachability(..., techs)` | `GB/Gameplay/ContentPack.h` | El validador sabe de tecnologías |
| Ganchos en `Tribe` | `GB/Game/Tribe.cpp` | Puertas al fabricar y recoger, opción `experiment`, testigos, lecciones, el registro |

## Datos

```json
"techs": [
  { "id": "flint_knapping", "name": "Flint knapping", "era": 1, "requires": [ "cordage", "stone_tools" ],
    "recipes": [ "flint_axe", "flint_pickaxe" ], "nodes": [], "buildings": [],
    "discovery": { "experiment": 0.15, "observe": 0.34, "teach": 0.25, "seconds": 20, "waste": 0.3 } },
  { "id": "stone_tools", "name": "Stone tools", "era": 1, "known": true, "requires": [], "recipes": [ "stone_axe", "stone_pickaxe" ] }
],
"knowledge": { "lessonSeconds": 20, "observeRadius": 12, "growth": 0.5, "seed": 1 }
```

- `known`: se sabe desde el principio de la era (todo agente que se une al clan la sabe).
- `requires`: tecnologías que hay que saber antes de poder descubrir o aprender esta.
- `recipes` / `nodes` / `buildings`: lo que desbloquea. **Una receta o un nodo que ninguna tecnología nombra es saber común**: todos lo hacen, como antes de H11. Si varias nombran la misma receta, basta con saber una. Los edificios son nombres hasta E9.
- `discovery`: `experiment` (probabilidad de un intento, 0 = no se descubre probando), `observe` (comprensión que da ver a alguien usarla), `teach` (comprensión por lección), `seconds` (lo que dura un intento) y `waste` (probabilidad de que un intento fallido estropee una unidad del primer ingrediente).
- Reglas del clan: cada cuánto hay lección en el campamento, a qué distancia se ve trabajar a otro, cuánto crece la probabilidad con cada fallo y la semilla.

`era1.json` trae seis: piedra, fuego y cordel (conocidas: la tribu hace lo mismo que antes), tallado de sílex y cestería (se descubren) y fundición (era 2,
diferida hasta la forja). `era1_ampliado.json` añade `bone_working` y `tillage`. `DefaultTechs()` dice lo mismo que `era1.json` (lo comprueba el test).

La sección se carga **dentro de la sección `items`** del registro de paquetes (`ItemContentSection(items, recipes, nodes, techs)`, o `AddGameContentSections(...,
&techs)`): el validador necesita a la vez los objetos, las recetas, los nodos y las tecnologías de la misma carga. Es atómica: una tecnología mal escrita
rechaza el documento entero, objetos incluidos.

## Conocimiento por agente y por clan

- Cada agente: `Known` (lo que sabe), `Progress` (comprensión de lo que está aprendiendo, 0..1; al llegar a 1 lo sabe), `Tries` (intentos fallidos por tecnología) y `NextTry`.
- El clan: lo que sus miembros han **mostrado en el campamento** (`Share`, al volver como la memoria espacial del clan). Una tecnología que ya no sabe nadie vivo
  se **olvida** (salvo las del principio). Un agente que la descubre y muere antes de volver al campamento se la lleva.
- Sin tecnologías cargadas (`SetTechs(nullptr)`, por defecto) todo se sabe y nada tiene puerta: los tests y el código que no sabe de H11 siguen igual.

## Descubrimiento (E5)

1. **Experimentar**: un miembro que tiene en el almacén o en la mochila los ingredientes de una receta de una tecnología que no sabe (y sabe lo que esta
   requiere) añade la opción `experiment`: va al campamento y trabaja `seconds`. Cada intento acierta con `experiment × (1 + growth × fallos)`; la tirada es un hash
   de la semilla, el nombre, la tecnología y el número de intento. Vale 0,2 (curiosidad, poco más que descansar) o 1,1 si la tecnología le daría la
   herramienta de su oficio que no tiene. Tras un intento espera `seconds` antes del siguiente. Cada intento cuenta como práctica de `crafting` (aptitudes de H7),
   y un miembro solo cambia a un oficio cuya herramienta no lleva si sabe fabricarla (`TribeWork`).
2. **Observar**: quien está a menos de `observeRadius` de alguien que fabrica una receta o trabaja un nodo de una tecnología que no sabe gana `observe`.
3. **Enseñar**: cada `lessonSeconds` hay lección: cada miembro desocupado en el campamento (descansa, se calienta, charla, come) gana `teach` de la primera
   tecnología (por id) que sabe otro que está en el campamento.

Todo lo que pasa va al registro de la tribu: «sits down at the camp to work out Flint knapping», «discovers Flint knapping by experimenting with the materials of the
flint_axe (try 3)», «understands 34% of Flint knapping by watching Biel», «learns Basketry from Ana at the camp», «shows the clan how Basketry is done», «the clan
forgets Basketry: nobody alive knows it any more».

## Validador

`CheckReachability` con tecnologías: una receta o un nodo bloqueado solo es alcanzable si alguna de sus tecnologías se puede descubrir. Una tecnología se puede
descubrir si es conocida, o si todo lo que requiere se puede y se puede probar (`experiment` > 0 y alguna de sus recetas tiene ingredientes, herramienta y estación
alcanzables, o no desbloquea recetas). Observar y enseñar necesitan a alguien que ya la sepa, así que por sí solos no descubren nada. Problemas nuevos: tecnologías
que requieren o desbloquean ids que no existen y tecnologías que nunca se descubren (por ejemplo, el sílex detrás del tallado de sílex probado con sílex). Lo
diferido del paquete (`deferred`) sigue siendo nota: `era1` difiere `smelting`.

## Guardado

Usa los huecos que reservó H4 ([GUARDADO.md](GUARDADO.md)), sin subir `kFormatVersion`:

- `actors[].knowledge` (`ActorState::Knowledge`): lo que sabe cada miembro como nombres `tech.<id>`; otros nombres («place.camp») se conservan tal cual.
- Sección `knowledge-clan` (versión 1): `{"known": ["tech.cordage", ...], "lesson": 7.25, "agents": {"Biel": {"progress": {...}, "tries": {...}, "nextTry": 312.5}}}`.
- Una partida anterior (sin nombres ni sección) carga con las tecnologías del principio. El editor solo escribe la sección cuando hay tribu.

## Editor

```
knowledge status [nombre|clan]       qué sabe cada uno, qué aprende (con %), qué ha probado y qué podría aprender
knowledge grant <nombre|clan> <tech> enseña una tecnología al momento (clan: a todos)
knowledge techs                      las tecnologías cargadas, qué requieren y qué desbloquean
```

El panel Tribe tiene la sección **Knowledge** (cada tecnología: conocida por el clan y por cuántos, sabida pero sin mostrar, aprendiéndose con barra, o desconocida) y
el inspector del miembro, lo suyo. `knowledge` se graba en las repeticiones.

## Pendiente

- Accidente (un golpe contra pedernal da lascas) y estudiar una pieza (`Study`); que el jugador cuente como testigo.
- Difusión entre clanes (comercio, alianza) y pérdida por catástrofe (H13).
- Edificios y nodos con tecnología en los datos de verdad (hoy los nodos con puerta solo están en los tests: un nodo cuyo material es el ingrediente de su propia
  tecnología nunca se descubriría).
- Hecho (H18): la tribu fabrica cordel y cestas por su cuenta (cadenas de producción, [ECONOMIA.md](ECONOMIA.md)): quiere las entradas de las recetas que no sabe para poder probarlas, así que el tallado de sílex se descubre jugando.
- Barrido de semillas para el ritmo (que la era 2 no llegue ni en 10 días ni en 10 000) y que dos clanes con semillas distintas sigan caminos distintos.
