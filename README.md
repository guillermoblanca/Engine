# GB Engine

Motor gráfico 3D en C++20 (OpenGL primero), pensado para poder ser usado y mejorado por agentes además de personas.

## Compilar (Windows / Linux)

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
./build/Sandbox/Sandbox --frames=3     # Windows: build\Sandbox\Release\Sandbox.exe
```

Las dependencias (spdlog, y más adelante GLFW, glad, etc.) se descargan con CMake FetchContent.

## Estructura

- `Engine/` – biblioteca estática `GB`
- `Sandbox/` – aplicación de ejemplo
- `tests/` – tests (ctest)

## Hoja de ruta

0. Base CMake, log, bucle headless, CI  ← actual
1. Ventana (GLFW), OpenGL 4.5, input teclado/mando
2. Render PBR, luces, sombras, HDR/bloom
3. Importación glTF 2.0 (Blender)
4. Cámaras, animación esquelética, partículas, audio
5. Editor (ImGui) y API para agentes (headless, capturas PNG, escenas en texto)
