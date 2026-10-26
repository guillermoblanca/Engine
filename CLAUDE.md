# GB Engine – guía para agentes

- C++20, CMake. Objetivo: Windows y Linux. Namespace `GB`, macros `GB_*`.
- Build y tests: ver README. Antes de commitear: configurar con `-DGB_WARNINGS_AS_ERRORS=ON`, compilar y pasar `ctest`.
- Usar `#include "GB/..."` con `/` (nunca `\`).
- No versionar binarios ni salidas de build (ver `.gitignore`). Dependencias vía FetchContent con versión fija.
- Todo código nuevo del motor debe poder ejecutarse headless (sin ventana) para tests y QA visual por capturas.
- El motor es una biblioteca estática `GB`; no usar `__declspec(dllexport)`.
