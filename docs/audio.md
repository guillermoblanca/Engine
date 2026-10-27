# Audio

Sintetizadores, mezclador 3D y dispositivo de salida.

[← Volver al README](../README.md)

## Audio

`GB/Audio/`: sonido sin archivos ni tarjeta de sonido.

- `AudioClip`: datos mono en float; sintetizadores deterministas (misma semilla, mismas muestras): `Tone`, `Noise` (blanco/rosa/marrón), `Wind`, `Rain`, `River`, `Thunder`, `Birds`, `Crickets`, `Footstep` (hierba, tierra, piedra, arena, madera, nieve, agua) y `Boom`; los ambientes son bucles sin clic. También carga wav/flac/mp3 (`LoadFile`) y escribe wav (`SaveWav`).
- `AudioMixer`: mezclador por software (hasta 64 voces) con volumen, tono, bucle, fundidos, sonido 3D (atenuación por distancia y panorámica según el oyente), buses `sfx/ambient/music/ui`, volumen maestro, limitador suave y medidores de nivel. `Render()` es CPU puro y determinista: lo llama el dispositivo, un test o una exportación offline.
- `AudioDevice`: salida por miniaudio (v0.11.22 fijada, una cabecera); sin tarjeta de sonido (CI, contenedores) usa el backend silencioso, así que todo el camino se ejecuta igual.
- `AudioEngine`: mezclador + biblioteca de clips por nombre (`wind`, `rain_heavy`, `step_stone:3`, `tone:440` o una ruta de archivo) + dispositivo opcional; `RenderToWav` vuelca lo que suena a un wav.
- Tests `audio` (CPU: sintetizadores, panorámica, distancia, tono, bucles, buses, limitador, huella de la mezcla y estadísticas doradas de los presets) y `audio_device`.
