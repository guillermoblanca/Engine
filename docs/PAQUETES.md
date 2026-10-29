# Eras como paquetes de datos (H17)

Un **paquete** es un manifiesto `assets/data/<id>.pack.json` junto a los ficheros de contenido que lista. Los ficheros de contenido son los mismos documentos de siempre
(secciones `items`, `recipes`, `nodes`, `roles`...): no saben a qué paquete pertenecen, así que `content load` sigue sirviendo para un fichero suelto.

```json
{
  "id": "era1_ampliado",
  "name": "Era 1 · Catálogo ampliado (E0)",
  "era": 1,
  "version": "1.0",
  "depends": [ "era1" ],
  "files": [ "era1_ampliado.json" ],
  "given": { "wood": "de dónde sale sin nodo ni receta" },
  "stations": [ "workbench" ],
  "deferred": { "grain": "por qué todavía no se puede conseguir" }
}
```

- `depends`: ids de paquetes, que se buscan en la misma carpeta. `files`: rutas relativas al manifiesto, en orden.
- `given`: objetos que llegan por otra vía (la reserva inicial de `tribe build`, el pozo).
- `stations`: estaciones en las que se puede fabricar una vez cargado el paquete sin construir nada. `era1` declara `workbench`: el campamento cuenta como banco de trabajo
  improvisado (`TribeConfig::CampStations`). Las demás estaciones salen de los **edificios** (H8, sección `buildings`, ver [ASENTAMIENTOS.md](ASENTAMIENTOS.md)): el secadero da `drying_rack`.
- `deferred`: huecos conocidos (objeto, receta o nodo → motivo). El validador los lista como notas, no como problemas. Si un hueco deja de serlo, avisa para quitarlo.

## Carga

`ContentPacks::Load(id, carpeta)` (`GB/Gameplay/ContentPack.h`) resuelve las dependencias en profundidad (cada una una vez, las ya cargadas no se repiten, un círculo es
un error), lee todos los ficheros en orden y los pasa por **todas las secciones registradas**, sobre copias. Solo si todo ha ido bien se aplica: un error en cualquier fichero de
cualquier paquete, o una dependencia que falta, no cambia nada. El paquete pedido se vuelve a cargar aunque ya lo estuviera (para recoger ediciones en caliente).

**Secciones.** Cada sistema registra la suya con `ContentPacks::AddSection`; Items no conoce a los demás. `StagedSection(nombre, claves, cargar, comprobar, objetos...)` crea una
sección sobre objetos vivos de cualquier tipo: hace la copia, carga cada documento en ella y la confirma al final. Hoy hay seis (`GB/Game/GameContent.h`): `items` (objetos,
recetas y nodos, con `LoadContent` y el validador), `roles` (`LoadRoles`), `tribe` (`LoadTribeRules`: agenda, comida y existencias de la tribu), `work` (`LoadWorkRules`: nombres del clan, aptitudes, tablón y revisión de oficios), `interactives` (`LoadInteractives`) y `species` (`LoadSpecies`; las dos últimas con
`AddWorldContentSections`, que el paquete `era1` lee de `world.json`). Los **edificios** (`buildings`, H8) los lee la propia sección `items` cuando se le dan
(`AddGameContentSections(..., &buildings)`, `ItemContentSection(items, recipes, nodes, buildings)`), porque el validador los comprueba con los objetos. Para añadir tecnologías basta con otra sección:

```cpp
packs.AddSection(StagedSection("interactives", { "interactives" },
    [](const std::string& json, std::map<std::string, InteractiveDef>& defs) { return LoadInteractives(json, defs); }, nullptr, m_Interactives.Defs));
```

Una clave de primer nivel que ninguna sección lee se anota (nota «has no loader here») y se ignora: los datos pueden llegar antes que su código.

## Validador de alcanzabilidad

`CheckReachability` parte de lo dado y repite hasta que nada cambia: un nodo da su objeto si su herramienta (`requires`) se puede tener (por jerarquía de etiquetas: un hacha
de sílex vale como `Item.Tool.Axe`), y una receta da su producto si sus entradas, su herramienta y su estación se pueden tener. Problemas: objetos que nada alcanzable da,
recetas imposibles (con el primer motivo), nodos cuya herramienta no se puede fabricar, `given` que no son objetos y ciclos de recetas que nada de fuera alimenta. Un ciclo
alimentado desde fuera (madera → tablón → madera) es solo una nota.

Se ejecuta en cada carga: avisa (en el editor, por el log) o, con `strict`, rechaza la carga. El test `packs` lo pasa en modo estricto sobre `era1` y `era1_ampliado`.

## Editor

```
pack list                         paquetes de assets/data (* = cargado), con su era y lo que necesitan
pack load <id|manifiesto> [strict] carga con dependencias; muestra problemas y notas
pack check                        vuelve a comprobar lo cargado ahora
```

Al arrancar el editor carga `era1` (antes leía `era1.json` a mano: el resultado es el mismo).

**Tecnologías (H11).** Con `AddGameContentSections(..., &techs)` la sección `items` lee también `techs` y `knowledge` (las comprueba con los objetos de la misma carga) y el
validador las tiene en cuenta: una receta o un nodo bloqueado por una tecnología solo es alcanzable si alguna de ellas se puede descubrir (conocida desde el principio,
o por experimentación con ingredientes alcanzables y sus requisitos descubribles). Una tecnología que nunca se descubre es un problema, salvo que el paquete la difiera
(`era1` difiere `smelting` hasta la forja). Ver [CONOCIMIENTO.md](CONOCIMIENTO.md).

## Lo que ya encontró

- La progresión de la Edad de Piedra ([PROGRESION.md](PROGRESION.md)) completa la cadena del cobre (alfarería → horno → ladrillo cocido → forja → fundición), así que
  `era1` ya no difiere nada; da `hide` (la piel de la caza) igual que da `meat`. Una tecnología se puede probar con una receta cuya estación da un edificio que ella misma
  desbloquea (la cocina y su hogar, la fundición y su forja): en el juego el intento solo necesita los materiales.
- La tribu no puede conseguir su primera madera ni su primera piedra sin herramientas: talar pide hacha y picar pico, y ambos piden madera y piedra. Funciona porque
  `tribe build` deja una reserva inicial; `era1` lo declara en `given`.
- `era1_ampliado` tenía troncos (`log`) que nada daba, y por tanto tablones imposibles: se añade el nodo `fallen_trunk` (con hacha). Grano, bayas secas, adobe y hacha
  de cobre quedan diferidos hasta los campos (E7), el secadero (E9) y la forja (E10).

## Pendiente

- Versión de paquete en el guardado (qué paquetes y versiones tenía la partida) y comprobación de versión en `depends`.
- Validar entre secciones (la herramienta de un rol existe), habilidades, modelos y animaciones que nombra el contenido.
- Hecho (H11): las tecnologías (`techs`, `knowledge`) viajan con la sección `items` y el validador sabe de ellas ([CONOCIMIENTO.md](CONOCIMIENTO.md)). Falta: «alcanzable desde la era anterior» como regla de era.
