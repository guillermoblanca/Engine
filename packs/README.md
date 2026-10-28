# Packs de personajes y animaciones

`packs/full/` reúne los 184 ficheros FBX con los que se prueba el motor: el personaje **X Bot** (`X Bot.fbx`) y los clips de animación de los packs de Mixamo (locomoción, granja, heridos, magia, arco, espada y escudo, gestos). Están todos juntos en una carpeta plana: el motor reconoce los clips por su nombre y los asigna a los sets y montajes con `assets/animation/mixamo.manifest.json`.

## Uso

Las rutas son relativas a la raíz del repositorio:

```
build/Editor/Release/Editor.exe --script=tribu.txt
```

con, por ejemplo, `tribe build "packs/full/X Bot.fbx" 0 0 5` en `tribu.txt`. Para los tests que usan los packs:

```
set GB_PACKS_DIR=packs/full
set GB_MIXAMO_DIR=packs/full
set GB_INJURED_DIR=packs/full
set GB_FARMING_DIR=packs/full
ctest --test-dir build -C Release
```

(En Linux, `export GB_PACKS_DIR=packs/full`, etc.)

## Origen y licencia

Los modelos y animaciones proceden de [Mixamo](https://www.mixamo.com/) (Adobe) y se incluyen aquí **solo para desarrollar y probar el motor**, por decisión del autor del proyecto. Las condiciones de uso de Mixamo permiten usar estos recursos dentro de un proyecto, pero no distribuirlos como colección independiente: si el repositorio se publica o se reparte más allá del equipo, hay que **quitar esta carpeta** (o sustituirla por los packs que cada colaborador descargue de Mixamo con su cuenta) y volver a las rutas locales. Cuando el artista entregue los modelos definitivos, esta carpeta desaparece.
