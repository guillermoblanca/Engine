@echo off
rem Abre el laboratorio: el servidor local y la web (http://127.0.0.1:8765). Requiere Python 3 y GBSim compilado.
cd /d "%~dp0\..\.."
start "" http://127.0.0.1:8765
python tools\lab\server.py %*
