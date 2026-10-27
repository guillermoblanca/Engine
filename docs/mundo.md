# Mundo: terreno, agua, vegetación y mundos procedurales

Terreno, rastros, vegetación, agua, ríos, rocas, erosión y generación de mundos.

[← Volver al README](../README.md)

## Terreno y desierto

```cpp
GB::DuneParams dunes;  dunes.WindAngleDegrees = 28;  dunes.Wavelength = 58;  dunes.Height = 7.5f;
auto terrain = GB::Terrain::CreateDunes(dunes, 360.0f, 641);    // o CreateHills({...}, ..., GB::TerrainStyle::Meadow / Snow)
terrain->AddToScene(scene);
weather.SetGroundFunction(terrain->MakeGroundFunction());       // lluvia, nieve, tornado... se apoyan en el relieve
GB::SandSpray spray(*terrain);  spray.AddToScene(scene);        // arena soplada desde las crestas (spray.Update(dt, camera))
```

- **Dunas procedurales** (`Terrain::CreateDunes`): cara de barlovento suave y de sotavento empinada (medido: 0,13 frente a 0,46 de pendiente media), crestas que serpentean, dunas de distinto tamaño y ondulaciones de fondo; determinista por semilla.
- **Material del terreno**: grano de arena, **rizado de viento** (normal map que sigue la dirección del viento), **mapa de color a gran escala** por pendiente y altitud (`Material::MacroMap`, sombra más oscura en las caras de sotavento, crestas claras). Estilos `Desert`, `Snow` y `Meadow`; `CreateHills` para colinas.
- `Heightmap` (altura bilineal, normales, límites) y colisión de partículas con el relieve (`ParticleEmitterDesc::GroundHeightFn`).
- `SandSpray` (granos alargados que viajan con el viento, con ráfagas, y polvo suave), `ParticlePresets::DustDevil()` (remolino de polvo).
- Sandbox: `--scene=desert [--terrain=dunes|hills --style=desert|snow|meadow --seed=N --wind-angle=° --wavelength=m --height=m --weather=sandstorm|tornado|snow|... --nospray]`.

## Rastros en el suelo

```cpp
GB::GroundInteraction marks({ /*Size*/ 48.0f, /*Resolution*/ 1024, /*RecoveryPerSecond*/ 0.035f });
scene.Ground = &marks;                                       // los materiales con TrailDepth > 0 muestran las marcas
marks.StampFootprint(position, heading, 0.28f, 0.11f);       // también StampCircle y StampLine (estelas de ruedas, colas...)
marks.Update(dt, player.Position);                           // dibuja, recupera y desplaza la ventana
GB::FootstepTracker steps(2);                                // detecta cuándo cada pie toca el suelo
for (auto& plant : steps.Update(feetWorldPositions, terrain->MakeGroundFunction(), heading, dt)) ...
```

- `GroundInteraction` es una textura (RGBA16F) alrededor del jugador donde se «estampan» marcas de profundidad 0..1. Las marcas se **recuperan con el tiempo** (`exp(-k·dt)`: la arena se rellena en segundos, la nieve guarda el rastro minutos) y la ventana **se desplaza** con el jugador sin copiar nada (toroidal; solo se limpian las franjas que entran). Marcas solapadas toman el máximo.
- Los materiales con `TrailDepth` (el terreno ya lo trae según estilo) **hunden** la superficie, la **oscurecen** (`TrailTint`, compactación o sombra azulada en la nieve) y **tuercen la normal** en los bordes de la huella.
- `FootstepTracker` detecta aterrizajes de pies con histéresis, a partir de las posiciones (p. ej. los huesos del pie de un personaje animado) y la altura del terreno.
- La hierba (siguiente etapa) leerá la misma textura para aplastarse.
- Sandbox: `--scene=desert --walker=cesium|fox [--style=snow --weather=snow]` camina en círculo dejando huellas y una nubecilla de polvo en cada paso (`--walker-speed`, `--walker-yaw`, `--distance`, `--pitch`, `--yaw`).
- Modelos de ejemplo `CesiumMan.glb` y `Fox.glb` en `assets/models` (atribuciones en su README).

## Vegetación y agua

```cpp
GB::GrassField grass(grassParams);  scene.Grass.push_back(&grass);   // hierba instanciada: viento, aplastada por los pasos
GB::TreeFactory trees;  auto oak = trees.Create({ GB::TreeSpecies::Broadleaf, /*Height*/ 8.0f });
oak->AddToScene(scene, position, yaw, scale);                       // Broadleaf, Conifer, Palm, Bare (todo procedural)
GB::WaterSurface lake(60.0f);  lake.Position = { x, level, z };    scene.Water.push_back(&lake);
GB::WaterRipples ripples;  scene.Ripples = &ripples;
ripples.Impulse(hit, 0.17f, 0.02f);  ripples.Update(dt, player.Position);   // gotas de lluvia, pasos, nadadores
```

- **Hierba**: hojas instanciadas generadas en CPU por chunks (con *frustum culling* por distancia), alpha-to-coverage, viento (`Environment::Wind`, lo fija `Weather`) y **se aplasta** donde `GroundInteraction` tiene marcas; la nieve acumulada la tapa. `GrassParams::DensityFn/HeightFn/NormalFn` la excluyen de agua, arena o pendientes.
- **Árboles procedurales** (`TreeFactory`, `ScatterTrees`): corteza, hojas, agujas y frondas con texturas propias, copas con normales «de copa», balanceo con el viento en el shader (`Material::WindSway/WindHeight/WindFlutter`) y colocación con reglas (altura, pendiente, separación).
- **Agua** (`WaterSurface`): cuatro olas de Gerstner (mismas en CPU, `HeightAt`, y en GPU), color y transparencia por **profundidad** (se lee el depth de la escena), reflejo del cielo con Fresnel y brillo del sol, **espuma** en la orilla y en las crestas, detalle de normales y anillos de `WaterRipples`.
- `WaterRipples`: ecuación de ondas 2D (RG16F, ping-pong, pasos fijos estables) en una ventana toroidal que sigue al jugador; la lluvia y los pasos en el agua generan ondas que se propagan, rebotan poco y se apagan.
- `Terrain::CreateHills` admite un **lago** (`HillParams::LakeRadius/LakeCenter/LakeDepth/WaterLevel`): excava la cuenca y eleva las orillas; `MakeGroundFunction(true)` hace que la lluvia salpique sobre el agua.
- Sandbox: `--scene=desert --terrain=hills [--lake=R --lake-x= --lake-z= --lake-view --waves=m --trees=N --gallery --grass=none --weather=rain] [--walker=cesium --walker-center=lake --walker-radius=m]`.

![Lago](lake.jpg)
![Vadeando](lake_wading.jpg)

## Ríos y rocas procedurales

```cpp
GB::HillParams hp; hp.RiverPath = { {-175,40}, {-70,-12}, {20,-28}, {178,-110} };   // puntos de control (spline Catmull-Rom)
hp.RiverWidth = 6.0f; hp.RiverDepth = 1.4f;
auto terrain = GB::Terrain::CreateHills(hp, 360.0f, 641);      // excava el valle; el agua solo desciende
GB::WaterSurface river(*terrain->GetRiver());                   // cinta de agua con corriente
scene.Water.push_back(&river);
world.SetCurrent([&](float x, float z) { auto f = terrain->GetRiverFlow(x, z); return glm::vec3(f.x, 0, f.y); });   // arrastra cuerpos

GB::RockParams rp; rp.Seed = 7; rp.Size = 1.3f; rp.Moss = 0.7f;
auto rock = GB::RockFactory::Create(rp);
rock->AddToScene(scene, position, yaw, scale, /*sink*/ 0.1f, /*waterLine*/ riverLevel);
```

- **Río**: el cauce siempre **baja** (el nivel solo desciende a lo largo del camino), valle en V con lecho plano y orillas que contienen el agua. `Terrain::GetRiverFlow` da la velocidad (más rápida en el centro y con más pendiente), `DistanceToRiver`, `GetRiverLevelAt`.
- **Corriente en el shader**: el detalle de la superficie se **advecta** a lo largo del flujo con dos fases mezcladas (sin saltos visibles) y aparece **espuma** en los tramos rápidos. Los objetos flotantes son arrastrados (`PhysicsWorld::SetCurrent`).
- **Rocas** (`RockFactory`): icosfera desplazada con ruido fbm + términos de arista, estiramiento por eje (losas, pilares), base plana, normales suaves o facetadas; determinista por semilla. `ScatterTrees` coloca rocas igual que árboles; en el Sandbox también a lo largo de las orillas.
- **Capas de superficie** en `Material` (`Grain`, `Moss`, `Sand`, `WaterLine`): grano y relieve por ruido 3D, **musgo** en las caras que miran arriba y en la humedad, **arena** acumulada en la base, y **banda mojada** (oscura y brillante) bajo la línea del agua.
- Sandbox: `--scene=desert --terrain=hills --river [--river-view --river-width=m] [--rocks=N] [--physics --physics-x= --physics-z=]`; las rocas se adaptan al estilo (musgo en prado, arena en desierto).
- Test `river`: nivel descendente, valle, orillas sin fugas, campo de corriente, cinta de agua, arrastre de una caja, geometría de rocas (determinismo, base plana, tamaño) y capas (musgo, arena, mojado).

![Río y rocas](river_rocks.jpg)

## Erosión, mundos procedurales y bosques

```cpp
GB::ErosionParams ep; ep.Droplets = 60000; ep.Seed = 3;
auto stats = GB::HydraulicErosion::Run(terrain->GetEditableHeightmap(), ep);   // gotas que ruedan, arrancan y depositan
terrain->RefreshMesh(stats.MinI, stats.MinJ, stats.MaxI, stats.MaxJ);          // sube solo la zona cambiada

GB::WorldDesc wd; wd.Type = GB::WorldType::Temperate; wd.Seed = 5; wd.TreeBudget = 420;
GB::GeneratedWorld world = GB::WorldGenerator::Generate(wd);   // terreno + lago + río + biomas + árboles + rocas
world.BiomeAt(x, z); world.GrassDensityAt(x, z); world.Trees; world.Rocks;
tree.AddToScene(scene, pos, yaw, scale, /*maxDistance*/ 340.0f);   // los bosques se recortan por vista y distancia
```

- **Erosión hidráulica** (`HydraulicErosion`): gotas con inercia que siguen la pendiente, cargan sedimento según velocidad, agua y desnivel, lo depositan donde la pendiente cede y erosionan con un pincel suave (barrancos y abanicos). Determinista, acotada (nunca excava más que el desnivel de su paso) y localizable en un círculo. `Terrain::RefreshMesh` actualiza solo la región tocada (`Mesh::UpdateVertices`).
- **Lluvia que erosiona**: en el Sandbox, `--erosion=GOTAS_POR_S` (300-3000 = lento; 30000 = time-lapse) hace que, con lluvia, las gotas cerca de la cámara remodelen el terreno en vivo (`--erosion-strength`).
- **Mundos procedurales** (`WorldGenerator`): `Temperate`, `Alpine` (picos, nieve, coníferas) y `Desert` (dunas y palmeras). Busca la cuenca más baja para el **lago**, traza un **río** que baja de lo alto (con meandros) hasta el lago, clasifica **biomas** (agua, orilla, prado, bosque, roca, nieve, dunas) por altura, pendiente, humedad (ruido) y cercanía al agua, y reparte árboles (bosques densos, árboles sueltos en prados, especies según altitud) y rocas (en roca y orillas) respetando el presupuesto. Todo determinista por semilla.
- **Bosques**: `RenderObject::BoundingCenter/BoundingRadius/MaxDistance`; el renderer descarta por **frustum** (también en el mapa de sombras) y por **distancia** (`Renderer::GetStats`). Árboles y rocas ya traen sus esferas.
- Sandbox: `--scene=desert --terrain=hills --world=temperate|alpine|desert [--seed=N --trees=N --rocks=N --forest=0..1 --river=off --lake=off]`.
- **Personaje controlable en tercera persona** (`GB/Game/CharacterController.h`, lógica pura sin GL): aceleración/deceleración lineales, giro hacia la dirección de avance por el camino corto con tope de velocidad angular, movimiento relativo a la cámara, adherencia al terreno, deslizamiento contra árboles/rocas (círculos en XZ), pendiente máxima, vadeo (más lento) y aguas profundas como pared. `ThirdPersonCamera` orbita con ratón/flechas/stick derecho, sigue con suavizado y se acerca para no entrar en el terreno. `CharacterAnimation` elige idle/walk/run del modelo (`Survey`/`Walk`/`Run` del zorro) con fundido y ajusta la velocidad de reproducción a la del suelo; con un solo clip (CesiumMan) lo acelera o lo congela.
- Sandbox: `--scene=desert --terrain=hills --world=temperate --player[=fox|cesium]` (WASD / stick izquierdo, Shift o pulsar el stick = correr, ratón/flechas/Q,E/stick derecho = cámara, rueda = zoom; `--player-x= --player-z= --player-yaw=`). Reutiliza el *walker* (huellas, polvo, chapoteo). Headless: `--player-demo` o `--player-keys="WD:1.5,RW:3,:1"` (W A S D mover, R correr, Q/E girar la cámara, `:segundos`) con `--hidden --record=DIR --fps=30 --frames=N`; escribe posición y velocidad una vez por segundo.
- Editor: menú View > Play as fox, o `player fox|cesium|off [script]` (también headless: `player fox WD:1.5,RW:3,:1; wait 150; shot_ui x.png`). Los árboles/rocas del mundo y los cuerpos que añadas son obstáculos.
- Test `character` (CPU): rampa de velocidad sin saltos, tope, parada, diagonales, giro por el camino corto y con tope, mismo resultado a 30/120 fps, obstáculos (no entra y rodea), vadeo y aguas profundas, rampa y acantilado, límites del mapa, histéresis de locomoción, guion de entrada, búsqueda de punto libre y colisión de la cámara con el terreno.
- Test `world`: erosión (determinismo, conservación aproximada de masa, cumbre rebajada y pie con sedimento, fuerza 0, localidad, estabilidad con fuerza alta), culling, refresco de malla y generador (lago y río, árboles en suelo seco, bosque mucho más denso que prado, biomas, nieve alpina, palmeras).

![Mundo templado](world_temperate.jpg)
![Erosión antes/después](erosion_before_after.jpg)

## Refinado de malla en orillas

El terreno es una malla regular, así que donde el agua toca el suelo la línea de orilla seguía los escalones de las celdas (~0,5 m). `HillParams::ShoreSubdivision` (4 por defecto; 0 o 1 lo desactiva) divide cada celda de la orilla del lago y de los bancos del río en NxN sobre una superficie Catmull-Rom a través del mapa de alturas, con `ShoreMarginCells` celdas extra de margen.

- Los vértices del borde de la zona refinada conservan la arista gruesa, de modo que la malla es estanca frente a la cuadrícula normal sin costuras.
- `Terrain::RefreshMesh` mantiene sincronizados los vértices nuevos con la erosión y los cráteres.
- Las consultas de altura (física, hierba, árboles) no cambian: usan el mapa de alturas original.
