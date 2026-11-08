@echo off
rem Reconstruye la wiki: exporta las reglas del juego sin ventana y genera ..\documentation\wiki\index.html.
rem La documentacion vive fuera del motor (D:\__game_engine__\documentation, repo propio).
rem Uso: tools\make_wiki.cmd [directorio-de-build]   (por defecto build_final)
cd /d %~dp0..
set BUILD=%1
if "%BUILD%"=="" set BUILD=build_final
if not exist ..\documentation\wiki\data mkdir ..\documentation\wiki\data
(
echo generate temperate 5
echo time 11
echo garden build 20 -6
echo tribe build fox 0 0 5
echo interactives build -8 -4
echo ecology spawn 4 4 8
echo docs export ../documentation/wiki/data/rules.json
echo quit
) > ..\documentation\wiki\data\export.txt
"%BUILD%\Editor\Release\Editor.exe" --hidden --quality=low --script=../documentation/wiki/data/export.txt
python tools\make_wiki.py
echo.
echo Abre ..\documentation\wiki\index.html en el navegador.
