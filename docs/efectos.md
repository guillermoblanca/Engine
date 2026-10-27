# Partículas y clima

Sistema de partículas y efectos atmosféricos.

[← Volver al README](../README.md)

## Partículas

```cpp
GB::ParticleSystem fire(GB::ParticlePresets::Fire());   // Fire, Smoke, Sparks, Explosion, Snow, Magic
fire.SetPosition({0, 0.2f, 0});
scene.Particles.push_back(&fire);                        // el Renderer las dibuja tras la geometría
fire.Update(dt);                                         // cada frame
```

Simulación en CPU **determinista** (misma semilla + mismos `Update` = mismas partículas), render por instancias en GPU (billboards).
`ParticleEmitterDesc`: formas de emisión (punto, esfera, cono, caja), tasa y ráfagas, bucle/duración, vida/velocidad/tamaño aleatorios por rango, gravedad (negativa = flotabilidad), viento, rozamiento, espacio local o mundial, color e intensidad HDR (alimenta el bloom), fundido, rotación, **estiramiento por velocidad** (chispas), mezcla aditiva o alfa (ordenada), sprites procedurales, textura propia y hoja de sprites (flipbook).
Sandbox: `--scene=particles` (hoguera nocturna: fuego, humo, chispas con luz parpadeante, nieve, orbe mágico y una explosión periódica; tecla B = explosión; `--simulate=S` calienta la simulación antes del primer frame).
Pendiente: partículas suaves (fade contra la geometría, necesita la profundidad de la escena: hoy los sprites grandes se recortan con el suelo), colisiones, emisión desde mallas, sombras de partículas, GPU compute para cientos de miles.

## Clima y efectos atmosféricos

```cpp
GB::Weather weather;                                 // lluvia, nieve, polvo, tormenta de arena, tormenta eléctrica, tornado
weather.SetBase(scene);                              // captura el entorno y las partículas propios de la escena
weather.SetPreset(GB::WeatherPreset::Thunderstorm);  // o SetRain(0.7), SetWind({8,0,2}), SetTornado(1)...
weather.Update(dt, camera);                          // transiciones suaves; los volúmenes siguen a la cámara
weather.Apply(scene);                                // cielo cubierto, niebla, suelo mojado/nevado, partículas, relámpago
for (auto& t : weather.GetLightning().TakeThunder()) // truenos con retardo = distancia / 343 m/s (para el audio)
    ...;
```

- **Lluvia** (miles de gotas alargadas con la caída inclinada por el viento), **salpicaduras** donde impactan, **neblina** a ras de suelo, **nieve** con turbulencia que se acumula sobre las superficies, **polvo**, **tormenta de arena** y **tornado** (embudo con vórtice, anillo de polvo, escombros y nube).
- **Relámpagos** procedurales con ramificaciones (`GB::Lightning`): parpadeo de varios pulsos, luz y destello del cielo, y eventos de trueno con retardo y volumen según la distancia. Se dirigen hacia donde mira la cámara.
- **Renderer**: niebla exponencial con altura, superficies mojadas (más oscuras y brillantes), cobertura de nieve en superficies orientadas hacia arriba, **partículas suaves** (se funden con la geometría) y partículas con niebla.
- **Partículas**: turbulencia (ruido 3D), vórtice (giro, atracción, elevación), colisión con el suelo (matar o rebotar) con emisor de impactos (`SetHitEmitter`), `EmitAt`, sprite `Streak`.
- Sandbox: `--scene=weather --weather=clear|lightrain|rain|storm|snow|blizzard|dust|sandstorm|tornado` (teclas 1-9, L = rayo, `--strike`).

## Nieve acumulada

La nieve se acumula mientras nieva y se derrite en unos 15 s al pasar a tiempo despejado (`Weather::Update`).
