# SHOTER AAA

Shooter sci-fi multijugador en primera persona hecho con **Unreal Engine 5.8** y C++. Hasta 8 jugadores en una arena de neón.

## ⬇️ Descargar y jugar

1. Entrá a **[Releases](https://github.com/JuanMoisio/ShoterAAA/releases/latest)** y bajá `ShoterAAA_Windows.zip`.
2. Descomprimilo en cualquier carpeta.
3. Abrí **Steam** (tiene que estar abierto) y ejecutá `ShoterAAA.exe`.
   - Si Windows muestra un aviso de SmartScreen: **Más información → Ejecutar de todas formas** (el juego no está firmado).
   - Sin Steam: usá `Jugar sin Steam (LAN o IP).bat` (todos tienen que usar ese modo).

Requisitos: Windows 10/11 de 64 bits y una placa de video con DirectX 12.

## 🎮 Jugar con amigos

- **Con Steam:** uno entra a **JUGAR → CREAR PARTIDA**. Los demás van a **BUSCAR PARTIDAS** o aceptan la invitación de Steam (menú de pausa → **INVITAR AMIGOS**). Mientras jugás, Steam muestra el juego como "Spacewar": es normal.
- **Sin Steam:** el host crea la partida y los demás usan **CONECTAR POR IP** (misma red, o una red virtual como Radmin VPN o ZeroTier; si no, el host tiene que abrir el puerto UDP 7777).
- Todos tienen que tener **la misma versión** del juego.

## Modos

| Modo | Cómo se gana |
|---|---|
| Duelo por equipos | Azul contra Rojo, el primero en llegar a 50 bajas (o 10 minutos) |
| Todos contra todos | El primero en llegar a 25 bajas (o 10 minutos) |
| Zombies (cooperativo) | Sobrevivir oleadas cada vez más difíciles; los puntos se gastan en armas y munición |

Los bots completan las partidas PvP cuando faltan jugadores.

## Controles

| Acción | Teclado y mouse |
|---|---|
| Moverse / mirar | WASD / mouse |
| Disparar / apuntar | Clic izquierdo / clic derecho |
| Correr / agacharse (deslizar) | Shift / Ctrl o C |
| Saltar (doble salto en el aire) | Espacio |
| Recargar / usar o comprar | R / E o F |
| Granada | G |
| Cambiar de arma | 1, 2, 3 o rueda |
| Puntajes / pausa | Tab / Esc o P |

También funciona con joystick.

## Para desarrolladores

Todo el proyecto (código, scripts, cómo compilar y empaquetar) está explicado en **[GUIA.md](GUIA.md)**.
Después de clonar, instalá Unreal Engine 5.8 y corré `Tools\Setup\CopyTemplateContent.ps1`, que copia el maniquí de las plantillas de Epic (no se guarda en el repo).
