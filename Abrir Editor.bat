@echo off
rem Abre el proyecto ShoterAAA en Unreal Engine 5.8.
set "UE=D:\Juegos epicos\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
if not exist "%UE%" (
    echo No se encontro Unreal Engine 5.8 en "%UE%".
    echo Si lo instalaste en otra carpeta, edita la ruta en este archivo.
    pause
    exit /b 1
)
start "" "%UE%" "%~dp0ShoterAAA.uproject"
