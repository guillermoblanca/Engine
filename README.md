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
Pendiente de esta fase: IBL real con HDRI, texturas (llegan con glTF), SSAO, CSM y luces con sombras.
Sandbox: flechas/WASD o stick derecho orbitan, Q/E zoom.

## Hoja de ruta

0. Base CMake, log, bucle headless, CI  ✔
1. Ventana (GLFW), OpenGL 4.5, input teclado/mando, capturas PNG  ✔
2. Render PBR, luces, sombras, HDR/bloom  ✔ (primera pasada, ver abajo)
3. Importación glTF 2.0 (Blender)
4. Cámaras, animación esquelética, partículas, audio
5. Editor (ImGui) y API para agentes (headless, capturas PNG, escenas en texto)
