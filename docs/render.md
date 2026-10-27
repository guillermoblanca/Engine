# Render, luz y calidad

Pipeline de render (PBR, sombras, cielo, reflejos, postproceso) y la demo completa.

[← Volver al README](../README.md)

## Render

![PBR showcase](pbr_showcase.png)

Forward PBR (Cook-Torrance GGX, metálico/rugosidad), sol con sombras PCF, luces puntuales, cielo procedural como iluminación ambiental/reflejos, MSAA 4x en HDR (RGBA16F), bloom y tonemapping ACES.
### Prueba de texturas (atlas de 12 materiales)

`Sandbox --scene=textures` monta una galería 4×3 sobre cuadrados subdivididos (51 200 triángulos cada uno) con albedo sRGB, normal mapping y desplazamiento de vértices por el mapa de altura (que también atenúa las grietas como AO).
`--tile=N` (0-11) muestra una tesela grande, `--grid=N`, `--height=H`, `--nodisplace`, `--flipy`, `--noflipx`.

![Galería](texture_gallery.jpg)
![Primeros planos](texture_closeups.jpg)

Notas sobre los atlas de `assets/textures/` (512×384, teselas de 128 px):
- Tienen una franja negra de ~3 px entre teselas y en los bordes: se recorta con 4 px de margen (si no, aparecen costuras oscuras al repetir).
- Las normales son OpenGL (Y+) pero el canal rojo está invertido respecto al mapa de altura (medido en 6 teselas); `NormalFlipX` lo corrige.
- Las teselas no son perfectamente «tileables» (los bordes no casan) y a 120 px por tesela se nota la baja resolución de cerca. No hay mapas de rugosidad/metal: se usa un valor por tesela.

Pendiente de esta fase: IBL real con HDRI, texturas (llegan con glTF), SSAO, CSM y luces con sombras.
Sandbox: flechas/WASD o stick derecho orbitan, Q/E zoom.

## Luz, cielo y refracción

```cpp
GB::SkyDesc sky; sky.Hours = 22.0f; sky.CloudCover = 0.4f;
auto st = GB::Sky::Compute(sky, scene.Env, scene.Sun);   // sol/luna, colores, estrellas, nubes, exposición
scene.Env = st.Env;  scene.Sun = st.Sun;                  // st.NightFactor: enciende las farolas
GB::PointLight lamp; lamp.Intensity = 34.0f; lamp.Radius = 16.0f;   // hasta 32 luces puntuales con caída suave
```

- **Ciclo día/noche** (`Sky`): la hora fija la posición del sol (amanecer 6 h, atardecer 18 h), su color (cálido cerca del horizonte), el cielo (azul → naranja de crepúsculo → noche), la niebla, el **brillo ambiental** y la exposición (el «ojo» se adapta a la oscuridad). De noche la luz direccional pasa a ser la **luna**, fría y débil.
- **Cielo de fondo**: estrellas redondas con parpadeo, banda de la Vía Láctea, disco lunar con halo y **nubes procedurales** (fbm) que derivan con el viento y se oscurecen con las tormentas (`Environment::Stars/CloudCover`, el clima los ajusta).
- **Luces puntuales**: hasta 32, con `Radius` opcional (ventana suave: la luz llega exactamente a 0 en el radio, para farolas y antorchas).
- **Refracción del agua**: el agua lee el color de la escena opaca y lo desvía según la normal y la profundidad (solo toma muestras que están detrás de la superficie), con absorción **Beer-Lambert** (el rojo desaparece primero) y color de dispersión.
- **Charcos**: con lluvia (`Wetness` alto) las zonas planas y bajas se llenan de charcos de rugosidad 0.03: reflejan cielo y luces, oscurecen el albedo y muestran **anillos de gotas**.
- Sandbox: `--scene=desert --terrain=hills --lake=22 --time=22 [--clouds=0.4 --lanterns=10 --weather=rain]`.

![Noche con farolas](night_lanterns.jpg)
![Charcos](puddles.jpg)

## Reflejos y entorno HDRI

```cpp
auto sky = GB::EnvironmentMap::Load("assets/hdri/sunset_sky.hdr");   // Radiance .hdr equirectangular
scene.Env.EnvMap = sky.get();                // fondo + luz ambiente + reflejos (null = cielo procedural)
scene.ScreenSpaceReflections = true;         // el agua refleja objetos cercanos (árboles, rocas, farolas)
```

- **`EnvironmentMap`**: HDRI equirectangular en RGB16F con cadena de mips. Las superficies rugosas y la luz ambiente leen mips más borrosos (prefiltrado barato); `Rotation` y `Exposure` ajustables. Lo usan el fondo, el PBR, la hierba y el agua. `tools/make_hdri.py` genera un cielo de ejemplo (`assets/hdri/sunset_sky.hdr`, con disco solar por encima de 1.0).
- **Reflejos en espacio de pantalla (SSR)** en el agua: rayo reflejado marchado contra el depth, con refinado binario, desvanecido en los bordes de pantalla; si el rayo sale a cielo se usa el entorno.
- **Destellos de farolas** sobre el agua (especular de cada luz puntual con su radio).
- Sandbox: `--hdri=sunset|ruta.hdr [--hdri-rotation=GRADOS] --ssr=on|off`.
- Límite: los charcos reflejan el entorno y las luces (no objetos); el reflejo de objetos en charcos requeriría un buffer de reflexión propio.

![Destellos](lake_night_glints.jpg)

## Calidad final y demo completa

```cpp
scene.Post.SSAO = 0.7f;                         // oclusión ambiental en pantalla (medio res + desenfoque que respeta bordes)
scene.Post.DepthOfField = 2.0f;                 // desenfoque en % de la altura de pantalla; FocusDistance / FocusRange en metros
scene.Post.AutoExposure = true;                 // adaptación del ojo: exposición según la luminancia media (sin lectura a CPU)
scene.Post.Mapper = GB::Tonemap::Neutral;       // ACES, Neutral (Khronos PBR Neutral) o Filmic (Hable)
scene.Post.Saturation = 1.1f; scene.Post.Contrast = 1.06f; scene.Post.Tint = {1.0f, 0.98f, 0.95f};
scene.Post.ChromaticAberration = 0.2f; scene.Post.Grain = 0.2f;
```

- **SSAO**: 14 muestras en el hemisferio de la normal reconstruida desde el depth, rotadas por píxel (ruido de gradiente entrelazado), con rango y sesgo dependientes de la distancia; blur de 4×4 que no cruza bordes de profundidad.
- **Profundidad de campo**: círculo de confusión por distancia al plano de enfoque, 20 muestras en espiral áurea y los píxeles nítidos no contaminan a los desenfocados.
- **Auto-exposición**: la escena se reduce a 32×18, se promedia el logaritmo de la luminancia en un destino 1×1 y la exposición se acerca al objetivo con `1 − exp(−dt·velocidad)` (el primer frame se ajusta al instante; un cambio brusco de luz deslumbra y se adapta).
- **Tonemap y gradación**: ACES, Neutral y Filmic; saturación, contraste, tinte, aberración cromática hacia las esquinas y grano de película.
- **Calidad**: `--quality=low|medium|high|ultra` en el Sandbox (MSAA 2/4, sombras 1024-4096, SSAO, auto-exposición, grano...) y cada efecto se puede ajustar: `--ssao= --dof= --focus= --focus-range= --autoexposure --tonemap= --saturation= --contrast= --grain= --ca=`.
- **Demo completa** `--scene=showcase`: mundo procedural (lago, río, bosque, rocas), personaje caminando por la orilla, objetos que flotan, un **día de 100 s** (mañana, mediodía, lluvia, tormenta con relámpagos, atardecer y noche con farolas que se encienden y parpadean) y una cámara con seis planos que cambia el enfoque según lo que mira. Se repite en bucle. `--start=SEGUNDOS` salta a un punto del guion, `--record=carpeta --fps=30 --frames=N` graba, `--quality=` ajusta.
- Test `post`: AO (oscurece el pie de la caja, no el suelo abierto), DOF (franjas lejanas borrosas, nítidas al enfocarlas; lo enfocado se mantiene), auto-exposición (oscuro se aclara, primer frame deslumbrante y luego baja, escena muy brillante termina más oscura), tonemappers distintos y válidos, saturación 0 = gris, tinte, contraste, grano y aberración.

![Demo completa](showcase.jpg)

## Follaje con alpha

Las hojas, agujas y frondas son tarjetas con máscara alpha (`AlphaMode::Mask`):

- Las texturas se generan proceduralmente a 3x3 muestras por texel, de modo que los bordes llevan alpha parcial (roble, hoja redonda, arce, rama de conífera y fronda de palma; los árboles de hoja ancha eligen la textura por semilla).
- El pase de sombras hace alpha-test de los casters enmascarados: las copas proyectan sombras con forma de hoja, no placas sólidas.
- En el buffer MSAA el material enmascarado sale como cobertura (alpha-to-coverage), con el alpha afilado en torno al umbral mediante `fwidth` para que los mips no adelgacen la silueta. Con una sola muestra por píxel se recorta de forma dura.
