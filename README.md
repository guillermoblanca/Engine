# GB Engine

Motor gráfico 3D en C++20 (OpenGL primero), pensado para poder ser usado y mejorado por agentes además de personas.

## Compilar (Windows / Linux)

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
./build/Sandbox/Sandbox --frames=3     # Windows: build\Sandbox\Release\Sandbox.exe
```

Las dependencias (spdlog, GLFW 3.4, stb) se descargan con CMake FetchContent; glad (OpenGL 4.5 core) está generado en `Engine/vendor/glad`.
En Linux hacen falta las cabeceras de X11: `sudo apt install xorg-dev libgl1-mesa-dev`.

## Ejecutar sin pantalla (agentes / CI)

```
xvfb-run -a ./build/Sandbox/Sandbox --hidden --frames=5 --screenshot=shot.png   # Linux
Sandbox.exe --hidden --frames=5 --screenshot=shot.png                           # Windows
```
Opciones: `--frames=N`, `--screenshot=file.png`, `--hidden`, `--novsync`. Sandbox: WASD/flechas o stick izquierdo mueven el triángulo, Esc/Back salen.

## Estructura

- `Engine/` – biblioteca estática `GB`
- `Sandbox/` – aplicación de ejemplo
- `tests/` – tests (ctest)

## Render (Fase 2)

![PBR showcase](docs/pbr_showcase.png)

Forward PBR (Cook-Torrance GGX, metálico/rugosidad), sol con sombras PCF, luces puntuales, cielo procedural como iluminación ambiental/reflejos, MSAA 4x en HDR (RGBA16F), bloom y tonemapping ACES.
### Prueba de texturas (atlas de 12 materiales)

`Sandbox --scene=textures` monta una galería 4×3 sobre cuadrados subdivididos (51 200 triángulos cada uno) con albedo sRGB, normal mapping y desplazamiento de vértices por el mapa de altura (que también atenúa las grietas como AO).
`--tile=N` (0-11) muestra una tesela grande, `--grid=N`, `--height=H`, `--nodisplace`, `--flipy`, `--noflipx`.

![Galería](docs/texture_gallery.jpg)
![Primeros planos](docs/texture_closeups.jpg)

Notas sobre los atlas de `assets/textures/` (512×384, teselas de 128 px):
- Tienen una franja negra de ~3 px entre teselas y en los bordes: se recorta con 4 px de margen (si no, aparecen costuras oscuras al repetir).
- Las normales son OpenGL (Y+) pero el canal rojo está invertido respecto al mapa de altura (medido en 6 teselas); `NormalFlipX` lo corrige.
- Las teselas no son perfectamente «tileables» (los bordes no casan) y a 120 px por tesela se nota la baja resolución de cerca. No hay mapas de rugosidad/metal: se usa un valor por tesela.

Pendiente de esta fase: IBL real con HDRI, texturas (llegan con glTF), SSAO, CSM y luces con sombras.
Sandbox: flechas/WASD o stick derecho orbitan, Q/E zoom.

## Importación glTF (Fase 3)

`GB::Model::Load("modelo.glb")` carga glTF 2.0 (`.glb`/`.gltf`, exportado de Blender: *File > Export > glTF 2.0*) y `model->AddToScene(scene)` lo añade al renderer.

Soportado: mallas (normales/tangentes generadas si faltan), jerarquía de nodos, materiales metálico-rugosidad con texturas (color base, metal/rugosidad, normal, oclusión, emisivo), modos alfa (opaco, máscara, mezcla ordenada), doble cara, `KHR_materials_emissive_strength`, texturas embebidas o externas.
Pendiente: esqueletos y animaciones (siguiente fase), morph targets, cámaras y luces del archivo, UV distintas de UV0, `KHR_texture_transform`.

`Sandbox --scene=model --model=ruta.glb [--yaw=GRADOS] [--nofloor]` (por defecto el casco de `assets/models`, CC BY 4.0).
Validado con el casco y modelos de prueba de Khronos (BoomBox, AlphaBlendModeTest, NormalTangentMirrorTest) y con un `.glb` generado por `tools/make_test_gltf.py` para el test automático.

![glTF](docs/gltf_models.jpg)

## Hoja de ruta

0. Base CMake, log, bucle headless, CI  ✔
1. Ventana (GLFW), OpenGL 4.5, input teclado/mando, capturas PNG  ✔
2. Render PBR, luces, sombras, HDR/bloom  ✔ (primera pasada, ver abajo)
3. Importación glTF 2.0 (Blender)  ✔ (sin animación todavía)
4. Cámaras, animación esquelética, partículas, audio
5. Editor (ImGui) y API para agentes (headless, capturas PNG, escenas en texto)
