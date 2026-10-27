# Física y destrucción

Cuerpos rígidos, colisiones, flotabilidad y efectos de destrucción.

[← Volver al README](../README.md)

## Colisiones y física

```cpp
GB::PhysicsWorld world;
world.SetTerrain(&terrain->GetHeightmap());              // o SetGround(altura, normal)
world.SetWater([&](float x, float z) { return lakeLevel; });   // flotabilidad
GB::BodyDesc crate; crate.HalfExtents = glm::vec3(0.45f); crate.Density = 380.0f;   // kg/m³: <1000 flota
int id = world.AddBody(crate, position);
world.AddStatic(trunkCapsule);                           // árboles (cápsula), rocas (esfera), muros (caja orientada)
world.Vortices = { vortex };                             // el tornado (cada frame)
world.Step(dt);                                          // pasos fijos de 1/120 s, determinista
for (auto& hit : world.TakeContacts()) ...               // impactos (impulso, normal) → polvo, sonido, rotura
for (auto& splash : world.TakeSplashes()) ...            // entradas en el agua → ondas, salpicaduras
auto ray = world.Raycast(origin, dir, 100.0f);           // terreno, estáticos y cuerpos
```

- `PhysicsWorld`: cuerpos rígidos **esfera y caja** con inercia, rozamiento de Coulomb, restitución y **sueño**; solver de impulsos secuenciales (24 iteraciones) con corrección de penetración. Caja-caja con **SAT + recorte de caras** (apilables, tres cajas estables) y aristas; esfera/caja contra **cápsulas verticales** (troncos), esferas y cajas estáticas; el terreno se prueba con la esfera o las 8 esquinas.
- **Flotabilidad y agua**: `Density` < 1000 flota (madera) y > 1000 se hunde (piedra); arrastre en el agua y eventos de salpicadura que el Sandbox convierte en **ondas** de `WaterRipples`.
- **Viento y tornados**: cada cuerpo recibe arrastre aerodinámico hacia el viento; un `Vortex` (perfil de Rankine: rotación sólida en el núcleo, 1/r fuera, succión hacia el eje y corriente ascendente) **levanta y lanza** cajas ligeras mientras las rocas pesadas resisten.
- Todo corre **sin ventana** (test `physics`: rebote, reposo, pendiente y fricción, conservación del momento, pilas, estáticos, agua, vórtice, raycasts y determinismo).
- Sandbox: `--scene=desert --terrain=hills --physics[=N] [--physics-spread=m --physics-x= --physics-z=] --weather=tornado` suelta cajas, barriles, bolas y tablones; los árboles y las rocas del mapa son colisionadores; en el lago flotan.

![Tornado y escombros](tornado_debris.jpg)

## Articulaciones y grupos de colisión

```cpp
GB::JointDesc j = world.MakeJoint(a, b, worldPoint, worldAxis, worldRef);   // anclas y ejes en el espacio de cada cuerpo, desde la pose actual
j.SwingLimit = glm::radians(50.0f);                       // cono: ángulo máximo entre los ejes (hombros, caderas, cuello)
j.Hinge = true; j.TwistMin = 0.0f; j.TwistMax = 2.4f;     // bisagra: los ejes siguen paralelos; la torsión (giro sobre el eje) tiene límites
int id = world.AddJoint(j);                               // b = -1 ancla el cuerpo a un punto del mundo (péndulos, cuerdas)
world.GetJointError(id); world.GetJointSwing(id); world.GetJointTwist(id);
GB::BodyDesc d; d.Group = 7; d.InertiaScale = 4.0f;       // mismo grupo: no chocan entre sí; más inercia: los cuerpos delgados no giran como peonzas
```

- **`JointDesc`**: ancla (los puntos se mantienen juntos), límite de cono (`SwingLimit`), bisagra (`Hinge`: los ejes siguen paralelos y solo gira sobre ese eje) y límites de torsión relativos a la pose en la que se creó la articulación. Se resuelven en el mismo bucle de impulsos secuenciales que los contactos (con corrección de posición tipo Baumgarte). Un grupo unido por articulaciones **se duerme junto**, y quitar un cuerpo quita sus articulaciones.
- **`BodyDesc::Group`**: los cuerpos del mismo grupo no colisionan entre sí (las extremidades de un ragdoll), pero sí con todo lo demás del mundo.
- Test `physics_joint` (CPU): péndulo (longitud, energía, cruce), bisagra (el empujón fuera del plano lo absorbe), cadena de seis eslabones, límite de cono bajo un empujón fuerte, límite de torsión en los dos sentidos, grupos, un par unido que se duerme y una explosión que lo lanza sin separarlo.

## Efectos de destrucción

```cpp
GB::DestructionSystem destruction(world);                  // sobre un PhysicsWorld
GB::DestructibleDesc crate; crate.Toughness = 450.0f;      // impulso (N·s) que lo rompe
crate.Split = { 2, 2, 2 };                                 // cajas: fragmentos por eje; esferas: 4 esferas menores
destruction.Add(bodyId, crate);
destruction.OnFragment = [&](int newBody, int from, const glm::vec3& size) { /* crear el visual del fragmento */ };

world.Step(dt);
destruction.Update(dt, world.TakeContacts());              // impactos duros rompen; los pequeños se acumulan
destruction.Explode(center, 9.0f, 22.0f);                  // onda expansiva: empuja y rompe
for (auto& b : destruction.TakeBreaks()) ...               // posición, tamaño, impulso → polvo, chispas, sonido
```

- **`PhysicsWorld::Explode`**: impulso radial con caída cuadrática, sesgo hacia arriba, menos efecto en cuerpos pesados y giro por el punto de aplicación.
- **`DestructionSystem`**: un cuerpo se rompe por un golpe duro, por daño acumulado o por una explosión. Las cajas se trocean en rejilla (`Split`), las esferas en cuatro; los fragmentos **heredan velocidad y giro** más un empujón hacia fuera, conservan la masa (densidad) y desaparecen tras `DebrisLifetime` (encogiendo el último segundo: `GetVisualScale`). `FragmentsBreakAgain` permite romper varias veces.
- **Solver de física mejorado** (necesario para muros): impulsos **acumulados** con rozamiento acotado por la carga real, cuerpos dormidos como apoyos inmóviles, **islas** que se duermen juntas, y corrección de penetración más suave. Un muro de ladrillos de 8 hileras se mantiene en pie y se duerme; una bala pesada o una explosión lo derriban. Los eventos de contacto ya solo informan de impactos reales (no de la carga de un cuerpo en reposo) y para **ambos** cuerpos.
- Sandbox: `--scene=desert --terrain=hills --destruction [--physics=N --wall-rows=N --shoot=T --explode=T]` (T en segundos de simulación, contando el calentamiento de `--simulate`); teclas **X** (explosión en el objetivo) y **F** (disparar una bola desde la cámara). Las explosiones lanzan fuego/humo, el polvo de los ladrillos rotos y dejan **un cráter chamuscado** en el suelo. Con `--weather=tornado` los escombros se rompen al caer.
- Test `destruction` (CPU): explosión (alcance, caída, masa), rotura por explosión y por golpe, acumulación, fragmentos (8 de la mitad de tamaño, masa conservada, velocidad, vida útil), esferas, rotura al caer, re-rotura, muro estable, muro derribado por bola y por explosión.

![Muro derribado](destruction_sequence.jpg)
