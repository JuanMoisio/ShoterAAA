# ShoterAAA: guía para principiantes

Esta guía es para vos, que sos el dueño del proyecto y nunca usaste Unreal Engine (el motor de juegos con el que está hecho ShoterAAA). Sirve para jugar con tus amigos, pasarles el juego y entender lo básico para seguir desarrollándolo.

## 1. ¿Qué es ShoterAAA?

ShoterAAA es un **shooter en primera persona** (un juego donde ves con los ojos del personaje y disparás) de **ciencia ficción**. Pasa en una **arena de neón de noche**, con estilo futurista. Pueden jugar **hasta 8 personas** a la vez, en la misma casa o por internet.

Tiene tres modos de juego:

| Modo | Qué pasa | Objetivo |
|---|---|---|
| **Duelo por equipos** | Equipo Azul contra equipo Rojo. | Llegar primero a **50 bajas** (enemigos eliminados) o que se cumplan **10 minutos**. |
| **Todos contra todos** | Cada uno juega por su cuenta. | Llegar primero a **25 bajas**. Si se cumplen **10 minutos**, gana quien tenga más. |
| **Zombies (cooperativo)** | Todos del mismo lado contra oleadas de zombies, cada vez más difíciles. | Aguantar todas las oleadas que puedan. Se termina cuando todos están muertos. |

Palabras que vas a leer seguido:

- **PvP**: "jugador contra jugador". Son los dos primeros modos.
- **Bots**: jugadores controlados por la computadora. Completan las partidas PvP.
- **Cooperativo**: juegan en equipo contra el juego, no contra otras personas.

## 2. Cómo jugar con tus amigos

Antes de empezar, asegurate de que **todos tengan la misma versión** del juego (ver sección 5).

Hay dos formas de jugar juntos:

- **Con Steam**: la más fácil. Steam es la tienda y plataforma de juegos para PC.
- **Sin Steam**: se conectan por **IP** (la dirección de la PC dentro de la red). Sirve para la misma casa o para internet con una red virtual.

El menú principal tiene estos botones:

| Botón | Para qué sirve |
|---|---|
| **JUGAR** | Crear una partida: modo, mapa, bots y jugadores máximos. |
| **BUSCAR PARTIDAS** | Ver las partidas que hay y entrar a una. |
| **CONECTAR POR IP** | Entrar a una partida escribiendo la IP del anfitrión. |
| **OPCIONES** | Ajustes del juego (ver sección 3). |
| **SALIR** | Cerrar el juego. |

### 2.1 Con Steam

Todos tienen que tener Steam **abierto y con la sesión iniciada**.

**El anfitrión** (el que crea la partida):

1. En el menú principal, hacé clic en **JUGAR**.
2. Elegí el modo, el mapa **Arena Neón**, la cantidad de **bots** (de 0 a 7) y el máximo de jugadores.
3. Hacé clic en **CREAR PARTIDA**.
4. Para invitar amigos, apretá **Esc** (o **P**) y elegí **INVITAR AMIGOS**.

**Tus amigos** tienen dos caminos:

- En el juego: **BUSCAR PARTIDAS** y entrar a la partida del anfitrión.
- Desde Steam: apretá **Shift + Tab** (se abre el *overlay* de Steam, una ventana que se superpone al juego), buscá al anfitrión en tu lista de amigos y elegí **Unirse a la partida**.

Mientras juegan, Steam muestra el juego como **"Spacewar"**. Es normal (ver sección 8).

### 2.2 Sin Steam: LAN o IP

- **LAN**: red local, o sea, las PC conectadas al mismo wifi o router (el aparato que te da internet en casa).
- **IP**: la dirección de tu PC dentro de esa red. Se ve así: `192.168.1.10` (es solo un ejemplo).

Pasos:

1. Todos hacen doble clic en **Jugar sin Steam (LAN o IP).bat**, que está en la carpeta del juego. Todos tienen que jugar en este modo. Ese archivo lo crea `Package.ps1` (sección 5).
2. Cada uno escribe su **nombre de jugador** en **OPCIONES**. Sin Steam, es el nombre que ven los demás.
3. El anfitrión elige **JUGAR**, configura la partida, marca la casilla **Partida LAN** si todos están en la misma red y hace clic en **CREAR PARTIDA**.
4. El anfitrión averigua su IP: abrí PowerShell (una ventana para escribir comandos; buscala en el menú Inicio), escribí `ipconfig` (muestra los datos de red de la PC) y apretá Enter. Buscá **Dirección IPv4** (es tu IP) en el adaptador que usa (Wi-Fi o cable de red).
5. Los amigos eligen **CONECTAR POR IP** y escriben esa dirección. Podés escribir solo la IP o la IP con el puerto, por ejemplo `192.168.1.10:7777`.

### 2.3 Por internet: Radmin VPN o ZeroTier

Si tus amigos están en otra casa o ciudad, el truco más fácil es una **VPN** (red privada virtual): un programa que hace que todos parezcan estar en la misma red.

1. Todos instalan **Radmin VPN** o **ZeroTier** (los dos son gratuitos).
2. Una persona crea la red y comparte su nombre o código con los demás.
3. Todos entran a esa red.
4. El anfitrión mira la dirección que el programa muestra para su PC.
5. Los amigos eligen **CONECTAR POR IP** y usan esa dirección. El resto es igual que en la sección 2.2.

Alternativa más técnica: abrir el **puerto UDP 7777** en el router del anfitrión. Un puerto es una "puerta" numerada por donde entran los datos, y UDP es el tipo de conexión que usa el juego. Se hace con *port forwarding* (redirección de puertos) y depende del modelo de router. Si no te sentís cómodo, usá la red virtual. En ese caso, los amigos se conectan con la **IP pública** del anfitrión (la que ve internet; si buscás "cuál es mi IP" en el navegador, la ves).

### 2.4 El firewall de Windows

El **firewall** es el "portero" de Windows: decide qué programas pueden recibir conexiones. La primera vez que jugás en red, puede aparecer un aviso.

- Dejá tildado **redes privadas** (la de tu casa o trabajo) y confirmá con **Permitir acceso**.
- Si lo cancelaste sin querer, buscá **firewall** en el menú Inicio para revisarlo.

## 3. Controles

Se juega con **teclado y mouse**. El **gamepad** (control) también funciona.

| Acción | Tecla o botón |
|---|---|
| Moverse | **W A S D** |
| Mirar | Mover el **mouse** |
| Disparar | Clic izquierdo |
| Apuntar con la mira (ADS) | Clic derecho |
| Correr | **Shift** (mantenelo apretado mientras te movés) |
| Agacharse | **Ctrl** o **C** |
| Deslizarse (*slide*) | **Ctrl** o **C** mientras corrés |
| Saltar | **Espacio** |
| Doble salto | **Espacio** de nuevo, en el aire |
| Recargar | **R** |
| Interactuar o comprar | **E** o **F** |
| Granada | **G** |
| Cambiar de arma | **1**, **2**, **3** o la rueda del mouse |
| Ver la tabla de puntajes | **Tab** |
| Abrir el menú de pausa | **Esc** o **P** |

Aclaraciones:

- **ADS** (*aim down sights*) es apuntar con la mira del arma.
- El doble salto usa un **propulsor**: un impulso extra mientras estás en el aire.
- El menú de pausa **no frena** la partida: los demás siguen jugando.

### Ajustes en OPCIONES

| Opción | Para qué sirve |
|---|---|
| Sensibilidad | Qué tan rápido se mueve la vista con el mouse. |
| Sensibilidad al apuntar | Lo mismo, pero mientras apuntás con la mira. |
| Campo de visión | Cuánto abarca la pantalla a los costados. |
| Volumen general y Volumen de música | Suben o bajan el sonido. |
| Calidad gráfica | Baja, Media, Alta o Épica. Más calidad, más carga para la PC. |
| Invertir eje Y | Invierte el mouse en vertical: mirar hacia arriba baja la vista. |
| Mostrar FPS | Muestra los cuadros por segundo en pantalla. |
| Nombre | Se usa al jugar sin Steam. Con Steam se usa tu nombre de Steam. |

Cuando cambies algo, tocá **APLICAR** y después **VOLVER**.

## 4. Modos y armas

### 4.1 Vida y escudo

1. Primero se gasta el **escudo** (100). Se regenera si pasan **4 segundos** sin recibir daño.
2. Después se gasta la **vida** (100). Se regenera tras **7 segundos**.

### 4.2 Modos PvP (Duelo por equipos y Todos contra todos)

- **Bots**: el anfitrión elige entre 0 y 7 al crear la partida.
- **Arma principal**: el **Pulse Rifle** viene por defecto. Todos arrancan con la **Pulse Pistol**.
- **Armas especiales**: están en **casilleros** (*lockers*) repartidos por el mapa. Se agarran **gratis**, pero tienen un **tiempo de espera** (*cooldown*) antes de poder volver a usarse.

### 4.3 Zombies (cooperativo)

- Las **oleadas** son cada vez más difíciles.
- Ganás **puntos** por cada golpe y cada zombie eliminado.
- Gastás los puntos en las **estaciones de compra**: acercate y apretá **E** o **F**. La munición (*ammo*) cuesta **400** puntos.
- Si morís, volvés a aparecer en la siguiente oleada.
- Si todos están muertos, se termina la partida (*game over*).

| Enemigo | Desde qué oleada | Cómo es |
|---|---|---|
| **Walker** | Desde el principio | Velocidad y vida normales. |
| **Runner** | Desde la oleada 3 | Rápido, pero frágil: cae con pocos disparos. |
| **Brute** | Desde la oleada 5 | Grande y lento, muy resistente, pega fuerte. |

### 4.4 Armas

| Arma | Qué es | Cómo se consigue |
|---|---|---|
| **Pulse Pistol** | Pistola semiautomática (un disparo por clic). Arma secundaria. | Todos la tienen desde el inicio. |
| **Pulse Rifle** | Fusil de asalto. Arma principal por defecto. | Viene por defecto en PvP. En Zombies: 1200 puntos. |
| **Ripper** | Subfusil (SMG, una metralleta liviana). Dispara muy rápido. | PvP: casillero. Zombies: 900 puntos. |
| **Scatter** | Escopeta de energía. | PvP: casillero. Zombies: 1500 puntos. |
| **Rail** | Francotirador con mira. Un disparo en la cabeza mata de un tiro. | PvP: casillero. Zombies: 2000 puntos. |
| **Plasma Launcher** | Lanzador que dispara bolas explosivas. | PvP: casillero. Zombies: 2500 puntos. |

## 5. Cómo pasarle el juego a tus amigos

Para compartir el juego hay que generar una **versión empaquetada**: una carpeta lista para jugar y un archivo **ZIP** (un comprimido) que se puede subir a internet.

1. Abrí PowerShell y entrá a la carpeta del proyecto con `cd D:\ShoterAAA`.
2. Ejecutá el empaquetado:

   ```
   powershell -ExecutionPolicy Bypass -File Tools\Build\Package.ps1
   ```

3. Esperá a que termine. Puede tardar bastante, porque compila y empaqueta todo el juego.
4. Revisá los resultados: **`Build\Windows\`** (la carpeta jugable) y **`Build\ShoterAAA_Windows.zip`** (el archivo para compartir).
5. Subí el ZIP a **Google Drive**, **MEGA** o **WeTransfer** y mandá el enlace a tus amigos.
6. Tus amigos descargan el ZIP, hacen clic derecho → **Extraer todo** y abren **ShoterAAA.exe** (o el archivo **.bat** si juegan sin Steam).

> **Regla de oro:** todos tienen que tener **la misma versión**. Cada vez que cambies algo del juego, volvé a generar el ZIP y reenviarlo. Si no coinciden, aparece el mensaje "Tu versión del juego es distinta a la del host" (ver sección 7).

Si aparece un aviso de **SmartScreen** (el filtro de Windows para programas poco conocidos), hacé clic en **Más información** y después en **Ejecutar de todas formas**. Es normal: el juego todavía no tiene **firma digital** (un certificado que confirma quién lo publicó).

**¿Qué PC necesitan tus amigos?** Una placa de video (GPU) de los últimos 6 años más o menos, y **8 GB de RAM** como mínimo (la memoria de trabajo de la PC). Si el juego va lento, que bajen la calidad gráfica en **OPCIONES**.

## 6. Para vos como desarrollador

Dos conceptos clave:

- **Unreal Engine 5.8** es el motor del juego. Incluye el **editor**, el programa donde se arma y se prueba todo con ventanas y botones. Se instala con el **Epic Games Launcher** (el programa de Epic para instalar motores y juegos).
- **C++** es el lenguaje con el que está escrito el código (las reglas del juego). El editor no es el juego: es la herramienta con la que lo desarrollás.

### 6.1 Abrir el proyecto

1. Hacé doble clic en **`D:\ShoterAAA\Abrir Editor.bat`**. Abre el proyecto con tu Unreal 5.8 (`D:\Juegos epicos\UE_5.8`) aunque el launcher no lo tenga asociado. Si el doble clic en `ShoterAAA.uproject` te funciona, también sirve.
2. La primera vez tarda **varios minutos**: está preparando los **shaders** (instrucciones que la placa de video necesita para dibujar). Es normal, dejalo terminar.
3. Cuando se abra, ya podés modificar el proyecto.

### 6.2 Probar multijugador con 2 jugadores

En el editor, los nombres de los botones están en inglés:

1. Al lado del botón verde **Play**, abrí el menú **⋮** (los tres puntitos).
2. Poné **Number of Players** en `2`.
3. Poné **Net Mode** en `Play As Listen Server` (uno de los jugadores hace de servidor mientras juega).
4. Apretá **Play**. Para terminar la prueba, apretá **Stop** (el cuadrado).

### 6.3 Los scripts

Los **scripts** son archivos `.ps1` que hacen tareas automáticamente. Para correr uno, abrí PowerShell dentro de `D:\ShoterAAA` y escribí algo así:

```
powershell -ExecutionPolicy Bypass -File Tools\Build\Compile.ps1
```

`-ExecutionPolicy Bypass` permite correr ese script esta vez, sin cambiar la configuración de seguridad de Windows. Los scripts buscan Unreal Engine 5.8 instalado con el Epic Games Launcher; si no lo encuentran, te avisan.

| Script | Qué hace | Cuándo usarlo |
|---|---|---|
| `Tools\Build\Compile.ps1` | Compila el código C++ para el editor. | Después de cambiar código. |
| `Tools\Editor\run_build_content.ps1` | Regenera sonidos, materiales y mapas automáticamente (corre Unreal sin ventana). Ojo: **reconstruye los mapas desde cero**, así que pisa los cambios que les hayas hecho a mano en el editor. | Cuando quieras regenerar ese contenido. Con el editor cerrado. |
| `Tools\Audio\generate_sfx.py` | Genera los sonidos WAV de `RawAssets\Audio`. Se ejecuta con `python Tools\Audio\generate_sfx.py` (requiere Python 3.11). | Cuando quieras regenerar los sonidos originales. |
| `Tools\Build\TestMultiplayer.ps1` | Prueba automática con un servidor, bots y un cliente, sin dibujar nada en pantalla. Acepta `-Mode TDM`, `FFA` o `Zombies`. | Para comprobar que la red funciona. Deja los logs en `Saved\Logs\SmokeTest`. |
| `Tools\Build\Package.ps1` | Crea el juego jugable y el ZIP para compartir. | Antes de mandárselo a tus amigos. |

### 6.4 Dónde está cada cosa

| Carpeta o archivo | Qué hay adentro |
|---|---|
| `ShoterAAA.uproject` | El archivo del proyecto. Es lo que abrís con doble clic. |
| `Source\ShoterAAA\` | El **código C++** del juego (se detalla abajo). |
| `Content\Shoter\` | Los **assets** (recursos del juego): mapas, sonidos y materiales. Por ejemplo, el mapa Arena_Neon va en `Content\Shoter\Maps\`. |
| `RawAssets\Audio\` | Los **WAV originales** (archivos de audio) de donde salen los sonidos del juego. Los genera `generate_sfx.py`. No los borres. |
| `Tools\` | Los scripts de la sección 6.3. |
| `Build\` | Lo que genera `Package.ps1` al empaquetar: `Build\Windows\` y el ZIP. |
| `Config\` | Ajustes del proyecto de Unreal. |

Dentro de `Source\ShoterAAA\`:

- `Game\`: modos de juego (PvP y zombies) y estaciones de compra.
- `Weapons\`: las armas, con sus valores y precios.
- `Player\`: el jugador, sus controles y sus puntajes.
- `AI\`: los zombies.
- `UI\`: menús y pantalla de juego.
- `Online\`: Steam, búsqueda de partidas y conexión.
- `Core\`: tipos y datos compartidos.

**Flujo típico:** cambiás código → `Compile.ps1` → probás en el editor (o con `TestMultiplayer.ps1`) → `Package.ps1` → subís el ZIP.

## 7. Problemas comunes y soluciones

### 7.1 No aparece la partida en BUSCAR PARTIDAS

1. Revisá que todos estén en el **mismo modo**. Si unos juegan con Steam y otros sin Steam, es probable que no se vean.
2. Mirá el texto de estado del menú principal. Para jugar con Steam tiene que decir **"Steam conectado como..."**. Si dice **"Modo LAN / IP (Steam no detectado)"**, Steam no está abierto o no tiene sesión iniciada.
3. Tocá **ACTUALIZAR** para refrescar la lista.
4. Si sigue sin aparecer, usá **CONECTAR POR IP** (secciones 2.2 y 2.3).

### 7.2 "Tu versión del juego es distinta a la del host"

- Todos tienen que usar el **mismo ZIP** (sección 5). Pedile a tu amigo que descargue el último, lo extraiga en una carpeta nueva y juegue desde ahí.

### 7.3 No se conecta por IP

1. Revisá que la dirección sea la del **anfitrión**, no la tuya.
2. Confirmá que estén en la misma red: la de casa, o la red virtual de Radmin VPN o ZeroTier.
3. Revisá el firewall (sección 2.4).
4. Si es por internet sin red virtual, el **puerto UDP 7777** tiene que estar abierto en el router del anfitrión (sección 2.3).

### 7.4 El firewall o el antivirus bloquean el juego

- **Firewall:** permitilo en **redes privadas** (sección 2.4).
- **Antivirus:** puede dar un **falso positivo** (una alarma falsa, porque el juego no está firmado). Agregá la carpeta del juego a las excepciones del antivirus.
- Que tus amigos descarguen el ZIP **solo desde el enlace que les mandaste vos**.
- **SmartScreen:** "Más información" y después "Ejecutar de todas formas" (sección 5).

### 7.5 Va con pocos cuadros por segundo (FPS bajo)

- Activá **Mostrar FPS** en **OPCIONES**. Los FPS son los cuadros por segundo: cuanto más, más fluido.
- Bajá la **calidad gráfica** a **Baja** o **Media**.
- Cerrá programas que consuman mucha memoria, como un navegador con muchas pestañas.

### 7.6 No se escucha nada

- En **OPCIONES**, revisá el **Volumen general** y el **Volumen de música**.
- Revisá el volumen de Windows y que la salida de audio sea la correcta (auriculares o parlantes).

### 7.7 El editor tarda muchísimo al abrir

- Es normal la primera vez, porque está preparando los shaders. Dejalo terminar.

## 8. Limitaciones actuales y próximos pasos

Esta es la **primera versión jugable** de ShoterAAA. Sirve para jugar, probar y mostrarle el juego a tus amigos, pero todavía no es un producto terminado.

### Limitaciones

- **Personajes:** usan el **maniquí de Unreal** (un personaje de prueba que trae el motor).
- **Armas:** están hechas con **formas simples**, no con modelos detallados.
- **Chat de voz:** el juego no tiene. Para hablar mientras juegan, usen **Discord** u otra app de voz.
- **AppID 480:** es el número con el que Steam identifica cada juego. El 480 es un ID de prueba que comparten muchos proyectos ("Spacewar"). El empaquetado escribe ese número en un archivo `steam_appid.txt` junto al `.exe`. Para publicar en Steam necesitás **tu propio AppID**, a través de **Steam Direct** (el programa de Steam para publicar juegos). Tiene un pago único de unos **US$100 por juego**.

### Próximos pasos (ideas, no promesas)

1. **Más mapas**, además de Arena Neón.
2. **Modelos reales** de personajes y armas. Se pueden buscar recursos en **Fab**, la tienda de assets de Epic.
3. **Rachas de bajas** (*killstreaks*): premios por eliminar varios enemigos seguidos.
4. **Estadísticas para un ranking** (*ranked stats*): llevar la cuenta de cada jugador.

Para empezar, lo más útil es que tus amigos jueguen y te digan qué les pareció. Así sabés qué mejorar primero.
