@echo off
rem Reconstruye la wiki: exporta las reglas del juego sin ventana y genera docs\wiki\index.html.
rem Uso: tools\make_wiki.cmd [directorio-de-build]   (por defecto build_final)
cd /d %~dp0..
set BUILD=%1
if "%BUILD%"=="" set BUILD=build_final
if not exist docs\wiki\data mkdir docs\wiki\data
(
echo generate temperate 5
echo time 11
echo garden build 20 -6
echo tribe build fox 0 0 5
echo interactives build -8 -4
echo ecology spawn 4 4 8
echo docs export docs/wiki/data/rules.json
echo quit
) > docs\wiki\data\export.txt
"%BUILD%\Editor\Release\Editor.exe" --hidden --quality=low --script=docs/wiki/data/export.txt
python tools\make_wiki.py
echo.
echo Abre docs\wiki\index.html en el navegador.
