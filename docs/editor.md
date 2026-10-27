# Editor, entidades y escenas

Editor Dear ImGui, entidades con jerarquía, escenas JSON y edición desde consola.

[← Volver al README](../README.md)

## Editor

Editor Dear ImGui (`GB_BUILD_EDITOR=ON` por defecto, ejecutable `Editor`): dockspace con Viewport (el render va a un framebuffer mostrado como imagen), Propiedades, Mundo, Estadísticas, Modelo, Consola y Assets.

- Todo valor editable es una propiedad con ruta (`sun.intensity`, `post.ssao`, `tod.hours`, `water.lake.level`...) registrada en `PropertyRegistry`: los paneles y la consola cambian exactamente lo mismo (`list`, `get`, `set`).
- Consola y scripts (`--script=archivo`, `--exec="a;b"`, comandos `run`, `wait N`): `generate`, `time`, `weather`, `quality`, `camera`, `frame`, `load`, `hdri`, `screenshot` (solo viewport), `shot_ui` (ventana completa), `stats`, `quit`.
- Headless: `xvfb-run -a Editor --hidden --quality=low "--exec=generate temperate 5; time 18; wait 4; shot_ui ui.png; quit"`.
- Test `editor` (CPU): propiedades, parseo/recorte, comandos y buffer de log.

## Entidades y escenas

`GB/Game/Entity.h`: `EntityWorld` con entidades con nombre único, jerarquía (`SetParent` conserva la transformación mundial y rechaza ciclos), etiquetas, activación heredada y componentes opcionales `Mesh` (primitiva o modelo glTF), `Light` y `Body` (datos de física). Es solo datos: funciona headless.

- `GB/Game/SceneIO.h`: escenas en JSON (`"version": 1`), legibles y editables a mano o por agentes; guardar-cargar-guardar es idéntico y un fallo de carga no toca el mundo.
- `GB/Game/EntityRender.h`: `EntityRenderer::Build(world, scene)` añade objetos y luces puntuales a la `Scene` (requiere GL).
- Tests `entity` (CPU) y `entity_render` (GL).

## Edición de escenas en el editor

Paneles **Hierarchy** (árbol, arrastrar para cambiar de padre, menú contextual) e **Inspector** (nombre, transform y componentes), menú **Edit/Entity**, selección con clic en el viewport (rayo contra la forma de cada entidad), gizmo de mover por ejes (arrastrar una flecha) y deshacer/rehacer (`Ctrl+Z`/`Ctrl+Y`, instantáneas JSON de la escena; un arrastre es un solo paso). Las entidades nuevas aparecen en el punto que mira la cámara; la seleccionada brilla levemente. En Assets, clic derecho sobre un `.glb` → "Add to scene as an entity".

Todo es accesible por comandos (`SceneEditor`, sin GL ni ImGui):

- `entity create <empty|cube|sphere|plane|light> [nombre] [padre]`, `entity delete|duplicate|select|info [entidad]`, `entity rename <entidad> <nombre>`, `entity parent <entidad> <padre|none>`, `entity add|remove <mesh|light|body>`, `entity model <ruta.glb>`, `entity set <entidad> <propiedad> <valor...>` y `entity list`.
- La selección se expone como propiedades `sel.*` (`set sel.position 0 2 0`, `set sel.mesh.albedo #e04030`).
- `scene new|save <archivo>|load <archivo>`, `undo`, `redo`, `select <nombre|id|none>`.
- En scripts, un comentario es `#` seguido de espacio; `#rrggbb` es un color.
- Test `editor` ampliado: comandos, deshacer/rehacer, ficheros de escena y selección por rayo.
