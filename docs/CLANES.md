# Clanes y diplomacia (H13)

Varios clanes en un mismo mundo y lo que hay entre ellos: quién se ha encontrado con quién, qué piensan unos de otros, qué tierra reclama cada uno y
qué hacen sus enviados cuando se reúnen (comercio, regalos, visitas en las que se enseñan técnicas, parlamentos y pactos), además de bodas entre
miembros de clanes distintos. Todo es CPU, determinista y sin ventana; las reglas son datos (sección `diplomacy`).

[← Volver al README](../README.md) · relacionado: [TRIBU.md](TRIBU.md) (cada clan es una tribu), [SOCIEDAD.md](SOCIEDAD.md) (personalidad, normas,
parejas), [CONOCIMIENTO.md](CONOCIMIENTO.md) (tecnologías), [EXPLORACION.md](EXPLORACION.md) (regiones), [ECONOMIA.md](ECONOMIA.md) (existencias objetivo y almacenes)

## Qué hay

| Pieza | Fichero | Qué es |
|---|---|---|
| `Clan`, `ClanWorld` | `GB/Game/Clans.h`, `Clans.cpp` | Los clanes del mundo (cada uno con su `Tribe`), encuentros, territorio, comercio y reuniones, bodas, pactos, guardado |
| `DiplomacyRules`, `ClanRelation`, `Meeting`, `PlanBarter` | `GB/Game/Diplomacy.h`, `Diplomacy.cpp` | Las reglas (datos), el estado de cada par de clanes y de cada reunión, y el trueque |
| `TerritoryMap`, `ClanFootprint` | `GB/Game/Territory.h`, `Territory.cpp` | Las reclamaciones de tierra región a región |
| Ganchos en la tribu | `TribeOutside`, `MemberTransfer` en `Tribe.h`; `TribeClans.cpp` | Opciones de fuera (el enviado), avisos de cada recogida, un miembro que sale de un clan y entra en otro |
| `Society::Admit/Marry`, `ClanKnowledge::Admit/Instruct` | `Society.h`, `Knowledge.h` | Un miembro que llega con su persona y lo que sabe; una boda; una lección de otro clan |
| Editor | `Editor/src/Gameplay/EditorClans.cpp` | `clan new|list|select|relations|trade|visit|wed|move|territory|meetings|log`, sección del panel, guardado |

## El clan

Un **clan** es una tribu (almacén, campamento, miembros, conocimiento, cultura, atlas) con un id (`ClanId`) y un nombre. El mundo de clanes
(`ClanWorld`) guarda los clanes por id; la tribu puede ser suya (`Add(nombre)`, con una copia de la configuración y una hoguera con su propio nombre:
`Campfire <clan>`) o del anfitrión (`Add(nombre, &tribu)`: en el editor, la primera tribu de `tribe build` es el **clan 0**). Los nombres de los
miembros son únicos en todo el mundo (son los de los actores): los hijos de un clan no toman un nombre que ya use otro (`Tribe::SetNameTaken`).

Las tribus no saben unas de otras. El mundo de clanes se engancha a cada una (`Tribe::SetOutside`) con cuatro ganchos de una línea: añade al cerebro
de un miembro la opción `envoy` cuando es enviado de una reunión, la ejecuta y la termina, y se entera de cada recogida (para saber de quién era la
tierra). Todo lo que hace un clan hacia otro lo hace un miembro **como decisión propia**: las necesidades siguen mandando y, lejos de la cámara, el
enviado camina como agente abstracto igual que cualquier otro (el coste crece con lo visible, no con el número de clanes).

`ClanWorld::Update` actualiza cada tribu y después lo que hay entre ellas (`StepDiplomacy`), con sus relojes.

## Encuentros

Cada `encounters.every` segundos (2) se buscan, con una rejilla de celdas del tamaño del radio, miembros de clanes distintos a menos de `radius`
(10 m). Se **saludan** (una vez cada `cooldown`, 300 s, por pareja): el primer saludo es el **primer contacto** de los dos clanes; cada saludo suma
el evento `meet` a la relación y `memberBond` (× la apertura) al vínculo de esos dos miembros. El registro de cada tribu lo dice
(«Ana (forager) greets Unai of Blue»).

## Relación entre clanes

Por cada par (`ClanRelation`):

```
actitud   = base + memberWeight × media de los vínculos entre sus miembros − grievanceWeight × el agravio mayor     (−1..1)
base      += peso del evento × escala (× la apertura media si es bueno)                                          (se apaga un decayPerDay al día)
confianza += trustGain × evento bueno  /  trustLoss × evento malo                                                (0..1)
agravio   += peso de agravio del evento, para el clan que lo sufre                                               (baja grievanceDecay al día)
```

Eventos (`relations.events`): `meet` 0,03, `trade` 0,12, `gift` 0,1, `visit` 0,05, `teach` 0,04, `marriage` 0,15, `pact` 0,05, `overlap` −0,01 (por día
de solape), `theft` −0,08, `refuse` −0,03, `raid` −0,5. Agravios (`relations.grievances`): `theft` 0,25, `raid` 1.

**La cultura cuenta.** La **apertura** de un clan es `1 + Σ peso × (2v − 1)` sobre sus normas y la media de los rasgos de sus adultos (`openness`:
`sharing` 0,5, `sociability` 0,4, `boldness` −0,2), entre 0,2 y 2: los buenos tratos entre clanes abiertos cuentan más. La **agresividad** se calcula
igual (`aggression`: `boldness` 0,6, `daring` 0,4, `sharing` −0,3) y es la que lee el conflicto (H14).

**Postura** (derivada, no se guarda): `unknown` (nunca se vieron), `allied` (alianza), `truce` (tregua), `hostile` (actitud ≤ `hostile`, −0,3, o un
agravio ≥ `grievanceHostile`, 1), `friendly` (actitud ≥ `friendly`, 0,25), `wary` (por debajo de 0) o `neutral`. El registro avisa cuando cambia.

## Territorio

Cada `territory.every` segundos (30) cada clan acerca su reclamación sobre cada región a lo que puede sostener:

```
lo que puede = (miembros vivos × perMember + edificios × perBuilding) / (1 + distancia del campamento al ancla de la región / scale)
               (0 si el ancla está a más de reach, 160 m; distancia 0 para la región del campamento; nada en el agua)
reclamación += (lo que puede − reclamación) × min(1, grow × dt / día)
```

Es decir, la reclamación **crece con el tiempo** hacia lo que permiten la población y los edificios (`Clan::Buildings`, que pondrá H8; 0 hasta
entonces) y mengua si el clan se encoge o se muda. Una región tiene **dueño** si su reclamación más fuerte pasa de `min` (1). Las regiones
reclamadas por encima de `min` por dos o más clanes **se solapan**: entre aliados es tierra compartida; entre los demás es **disputada**
(`ClanWorld::Contested`) y cuesta `overlap` al día. **Recoger un nodo** en tierra que otro clan tiene y en la que el propio no pasa de `min` es
**robo**: el dueño guarda un agravio (`theft`) y el registro lo cuenta una vez por minuto.

## Reuniones: comercio, regalos, visitas y parlamentos

Cada `trade.every` segundos (60), por cada par de clanes que **se han visto**, sin reunión en marcha y pasado el `cooldown` (600 s) desde la
anterior:

- **Hostiles**: si sus agravios están por debajo de `pacts.truceGrievance` (0,6) y los dos son abiertos (`truceOpenness`, 0,9), mandan enviados a
  **parlamentar**; la reunión acaba en **tregua**.
- Con actitud ≥ `trade.minAttitude` (−0,1) **comparan almacenes** (`PlanBarter`): lo que a uno le falta (por debajo de su existencia objetivo, la de
  la sección `tribe`; nunca por encima del tope de su almacén, H18) y al otro le sobra (por encima de `surplus` × su objetivo, 1,25), hasta `maxUnits`
  por objeto, y el lado mayor se recorta hasta valer como mucho `balance` (1,5) veces el menor según `values` (solo se comercia con lo que lista:
  bayas 1, piedra 1, madera 1, sílex 2, cordel 2, mineral de cobre 3, comida conservada 2...). Si solo un lado tiene algo que dar, no hay trueque,
  salvo que el clan quiera al otro lo bastante (`gift`, 0,45): entonces **regala**.
- Sin nada que cambiar, clanes amistosos (`exchange.minAttitude`, 0,15) con algo que enseñarse se **visitan** cada `visitEvery` (900 s).

Cada clan elige a su **enviado** (el adulto más sociable, libre, sin expedición ni otra reunión, mejor cerca del campamento). La reunión es un
recorrido en cuatro tramos, todos como opción `envoy` del cerebro (puntuación `score`, 1,6, compromiso de 20 s):

1. En su campamento **coge del almacén** lo que su clan da (lo lleva la reunión, no la mochila: ni se lo come ni lo devuelve al almacén por el camino).
2. Va al **punto de encuentro**, entre los dos campamentos (el centro o, si alguno no puede llegar, más cerca de uno u otro).
3. **Espera** al otro hasta `wait` (240 s). Cuando están los dos, se hace el **intercambio**: los bienes cambian de manos (evento `trade` o `gift`;
   si no había bienes, `visit`), los dos enviados se aprecian más, y si los clanes son amistosos **cada uno enseña al otro** la primera tecnología que
   sabe y el otro puede aprender (`exchange.teach` de comprensión, H11: con 1 la aprende entera). Además, un parlamento firma una **tregua**, y unos
   clanes con actitud ≥ `allianceAttitude` (0,45) y confianza ≥ `allianceTrust` (0,4) firman (o renuevan) una **alianza**.
4. Vuelve y **deja en el almacén** lo que trae (lo recibido o, si no hubo encuentro, lo que llevaba).

Si uno espera en vano se vuelve; si en `4 × wait` nadie llegó, todos se vuelven. Un enviado que muere pierde lo que llevaba. Cada reunión queda en el
historial (`History`, las últimas 32) con su resultado. Lo aprendido por el enviado lo muestra en su campamento como siempre (`Share`), y desde ahí
se enseña al resto del clan en las lecciones: así **una técnica pasa de un clan a otro**.

## Bodas y mudanzas

Cada `marriage.every` segundos (600), por cada par de clanes que se han visto, con actitud ≥ `minAttitude` (0,1) y sin ser hostiles, se busca la
pareja de miembros de los dos clanes con más vínculo (al menos `minBond`, 0,04: se han saludado), libres, de edad de emparejarse, de distinto sexo y
con la diferencia de edad de las reglas de su sociedad. Con probabilidad `chance` (0,5; un hash de la semilla, los nombres y el tiempo) **se casan**:
el miembro del clan **más grande** se muda al **más pequeño** (empate: el del id mayor). La mudanza (`ClanWorld::MoveMember`, también para el editor)
saca al miembro de su tribu con todo lo suyo (`Tribe::Detach`: cuerpo, mente, aptitudes; su persona en la sociedad, con edad, sexo, genes y
rasgos; y lo que sabe) y lo mete en la otra (`Tribe::Admit`: le cuentan el mapa del clan y decide de nuevo). Sus vínculos se quedan atrás y la pareja
nueva se registra en la sociedad del clan (`Society::Marry`).

## Pactos

`alliance` (comparten territorio: el solape no cuenta y recoger allí no es robo; no se atacan) y `truce` (no se atacan), con inicio y fin (`days`, 8
días); se renuevan en una reunión si sus condiciones se siguen dando. Una alianza **se rompe** si la actitud cae por debajo de `breakBelow` (−0,05).
El registro lo dice («the alliance breaks», «the truce ends»).

## Ganchos para el conflicto (H14)

La violencia es de H14; H13 deja expuesto todo lo que necesita:

| Gancho | Qué es |
|---|---|
| `ClanWorld::Hostility(a, b)` | Cuánto está `a` dispuesto a pelear con `b`: agresividad de `a` × (−actitud + su agravio contra `b`); 0 con un pacto |
| `ClanWorld::MayRaid(a, b)` | Son hostiles y sin pacto |
| `ClanWorld::Contested()` | Regiones disputadas (reclamadas por clanes no aliados), con sus aspirantes del más fuerte al más débil |
| `ClanWorld::Relate(a, b, "raid", escala, por qué, víctima)` | Un ataque: baja la actitud y la confianza y deja a la víctima el agravio |
| `ClanWorld::Listen(f)` | Cada evento entre clanes (`contact`, `stance`, `meeting`, `trade`, `gift`, `visit`, `teach`, `pact`, `break`, `expire`, `theft`, `marriage`, `move`, `missed`, `raid`) |
| `ClanWorld::Aggression(c)`, `Openness(c)`, `Grievance(a, b)` | La cultura y los agravios de cada clan |

## Datos

```json
"diplomacy": {
  "seed": 1,
  "territory":  { "every": 30, "reach": 160, "scale": 60, "perMember": 1, "perBuilding": 2, "grow": 1, "min": 1 },
  "encounters": { "every": 2, "radius": 10, "cooldown": 300 },
  "relations":  { "events": { "meet": 0.03, ... }, "grievances": { "theft": 0.25, "raid": 1 }, "openness": { ... }, "aggression": { ... },
                  "trustGain": 0.5, "trustLoss": 1, "decayPerDay": 0.05, "grievanceDecay": 0.1, "memberWeight": 0.5, "memberBond": 0.05,
                  "grievanceWeight": 0.3, "friendly": 0.25, "hostile": -0.3, "grievanceHostile": 1 },
  "trade":      { "every": 60, "minAttitude": -0.1, "cooldown": 600, "surplus": 1.25, "maxUnits": 6, "balance": 1.5, "gift": 0.45, "wait": 240,
                  "score": 1.6, "meet": 6, "values": { "berry": 1, "flint": 2, ... } },
  "exchange":   { "minAttitude": 0.15, "teach": 0.5, "visitEvery": 900 },
  "marriage":   { "every": 600, "minAttitude": 0.1, "minBond": 0.04, "chance": 0.5 },
  "pacts":      { "allianceAttitude": 0.45, "allianceTrust": 0.4, "truceGrievance": 0.6, "truceOpenness": 0.9, "days": 8, "breakBelow": -0.05 }
}
```

`DefaultDiplomacyRules()` dice lo mismo (lo comprueba el test). Cada parte sustituye esa parte entera (los mapas que no da quedan por defecto);
un error deja todo como estaba, también dentro de un paquete (sección `diplomacy`, `AddDiplomacyContentSection`, que el editor registra sobre
`ClanWorld::Rules`).

## Guardado

- La primera tribu sigue en la sección `ai` (y su conocimiento en `knowledge-clan`): **una partida de antes de H13 carga como el clan 0**.
- Sección nueva **`clans`** (versión 1, opcional; solo se escribe con más de un clan): los clanes (id, nombre, edificios y, para las tribus que
  guarda el mundo de clanes, su tribu, su conocimiento y lo que sabe cada miembro), las relaciones, los vínculos entre miembros de clanes
  distintos, los saludos recientes, las reclamaciones, las reuniones en marcha y el historial, los relojes y el registro. Los miembros son
  actores normales (se guardan con su `kind`); al cargar se recrean los clanes con sus miembros (`ClanWorld::SavedClans`) y después se lee el
  resto (`ClanWorld::FromJson`). No sube `kFormatVersion`.

## Editor

```
clan new <nombre> [x z] [miembros] [modelo]  un clan nuevo: sin x z, el sitio lo elige el elector de sitios (ChooseHome / el de H8) lejos de los demás
clan list                                    una línea por clan: miembros, campamento, almacén, regiones, apertura, agresividad y postura con los demás
clan select <id|nombre>                      el clan que muestran el panel Tribe y los informes de `tribe`
clan relations                               cada par: postura, actitud (base y miembros), confianza, agravios, cuentas, hostilidad y lo último que la movió
clan trade <a> <b> [force] | visit <a> <b>   manda enviados a comerciar / a visitarse
clan wed <a> <b> | move <miembro> <clan>     una boda entre miembros de clanes distintos / un miembro que se cambia de clan
clan territory | meetings | log [n]          quién tiene qué y lo disputado / las reuniones en marcha y hechas / el registro entre clanes
```

El panel **Tribe** tiene la sección **Clans**: la lista de clanes (clic: seleccionar), la postura y la actitud de cada par, las reuniones y el
territorio.

## Pruebas

`tests/clans_test.cpp` (test `clans`, CPU): la sección de `era1.json` dice lo mismo que el código, va y vuelve y un error no cambia nada (también en un
paquete); un trueque toma de lo que a uno le sobra lo que al otro le falta, equilibrado, y el mismo almacén da el mismo trato; las reclamaciones son
deterministas, crecen con el tiempo y con los edificios y se solapan entre campamentos cercanos; en templado 5 dos clanes a 41 m se encuentran, sus
enviados cambian bayas por sílex y dos ejecuciones acaban igual; guardado a mitad de reunión, carga y se vuelve a guardar igual y la termina; una
tribu sola de antes carga como el clan 0; un clan amistoso aprende cestería del otro en una visita y la muestra en su campamento; una boda muda a un
miembro con su edad, su pareja y lo que sabe, y clanes que se han visto se casan solos; tras un ataque son hostiles y pueden atacarse, y tras una
disputa menor parlamentan y firman una tregua.

## Límites y siguientes pasos

- Los clanes no se crean solos: un grupo que se separa para fundar otro asentamiento (aceptación de H12) puede usar `ClanWorld::Add` y `MoveMember`, pero
  falta la decisión.
- Las rutas y caravanas son un viaje de ida y vuelta al punto medio; no hay rutas recurrentes ni mercados. La reunión no tiene animaciones de
  conversación (plan, apartado 7).
- La reputación es por par de clanes; no hay reputación ante terceros ni mensajeros que lleven noticias de otros clanes.
- Las reglas de las tribus nuevas son una copia de las de la primera al crearlas: un `pack load` posterior solo cambia la primera.
- El barrido de semillas que muestre clanes que acaban aliados y otros enemigos según su carácter (aceptación de H13) queda para el carril de QA
  (H20), con `Openness`/`Aggression` y el registro de eventos como métricas.
