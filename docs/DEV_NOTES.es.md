# Pokémon Stadium port (libultraship → 3DS)

Port nativo de Pokémon Stadium (US 1.0) siguiendo la misma arquitectura que
Mario Kart 64 3DS (EstebanPdN): decomp → libultraship (PC) → backend de 3DS.
Se requiere una ROM propia; no se incluye ROM ni assets.

## Estructura

| Carpeta | Contenido |
|---|---|
| `src/`, `include/`, `lib/`, `yamls/`, `linker_scripts/` | Código del decomp `pret/pokestadium` (copiado, se modifica aquí como en SpaghettiKart) |
| `libultraship/` | Submódulo, fijado a `60c9bb71` (el mismo que usa el port de MK64 3DS) |
| `torch/` | Submódulo, fijado a `2d474dd` (el mismo que usa el port de MK64 3DS) |
| `port/` | Capa del port (motor, carga de recursos, extractor). `_referencia_spaghettikart/` es solo plantilla |
| `assets-config/` | Configuración de Torch para extraer assets de Stadium a `.o2r` |
| `platform/3ds/` | Backend de 3DS basado en `EstebanPdN/mario-kart-64-3ds` |

## Puntos de entrada para portar (medidos en el decomp)

La carga de assets desde la ROM está concentrada en pocas funciones, lo que
facilita cambiarla por recursos de libultraship:

- `func_80004454(segmento, rom_inicio, rom_fin)`: carga un rango de ROM a un segmento (14 llamadas).
- `func_800044F4(archivo, ...)`: carga archivos de assets (`textdata`, `backgrounds`, `battle_headers`) (5 llamadas).
- `func_80003B30`, `func_80004258`: otras cargas (3 llamadas).
- Referencias a direcciones de ROM: 335 símbolos en 24 archivos.
- Segmentos: 89 usos de `Memmap_GetSegmentVaddr` y 55 de `gSPSegment`.
- `src/D470.c` es el guardado en Flash RAM (no assets).

## Compatibilidad verificada

- Gráficos: F3DEX2 fifo 2.06 (ROM 0x66C90), soportado por el intérprete de libultraship.
- Compresión: Yay0, soportada por Torch.
- Microcódigos de la ROM (identificados):
  - `0x66BC0` rspboot.
  - `0x66C90` F3DEX2 (gráficos).
  - `0x68020` `aspMain` (audio): lo reemplaza el mezclador en software.
  - `0x68C80` `njpgdspMain` (decodificador JPEG): **no existe en libultraship**. Las imágenes
    JPEG del juego se decodifican al extraer con Torch (factory nueva) para no gastar CPU en el 3DS.
- Toda la carga de assets (ROM, Yay0, JPEG) está en `src/3FB0.c`: es el módulo principal a portar.

## Trabajo nuevo (no existe en Torch)

- Factories para modelos, animaciones, escenarios y menús de Stadium.
- Audio `libnaudio` (bancos estilo libultra): pasar como blobs y usar el sintetizador del juego.
- Modelos de Pokémon (215, `pokemon_models`) y animaciones (63, `0x8CC000`): son FRAGMENTs, pero su
  único código es un stub de 7 instrucciones que regresa un apuntador a datos. No hay MIPS que
  decompilar; el trabajo es convertir sus display lists y apuntadores a recursos.
- Fragments (77 overlays): enlazar estáticamente.

## Alcance inicial

1. Combate libre en PC sobre libultraship.
2. GB Tower: fuera del port. En su lugar el mapa muestra **ONLINE** (`src/fragments/37/fragment37.c`, `#ifdef PORT`):
   el letrero dice ONLINE, la pantalla del Game Boy del fondo trae el símbolo de señal y al elegirlo con A no se
   entra a nada (ni a la revisión de cartuchos): la caja de texto muestra el aviso en inglés "We are working on
   ONLINE mode..." durante 5 s. Los gráficos son propios y los genera `tools/port/gen_online_gfx.py` (lee la ROM
   solo para saber qué pixeles tapaban las letras viejas; el `.inc` no lleva pixeles del juego): un parche sobre el
   letrero de GB Tower (conservar / transparente / color) y la máscara del símbolo, que se pintan sobre lo cargado
   de la ROM. Los tres fondos con Doduo/Dodrio usan el mismo letrero y el mismo símbolo.
3. Transfer Pak: `osGbpak*` leyendo `.sav` de la SD. **Hecho** (control 1): las partidas van en
   `sdmc:/3ds/pokestadium/saves/` (`.sav`, `.srm` o `.dat` de 32 KB); al arrancar, la pantalla de abajo deja
   elegir cual se conecta. El host emula el cartucho (`platform/3ds/host/gb_saves.inc`): cabecera segun la
   version (o la de un `.gb` con el mismo nombre), bancos y RAM. Lo que Stadium escribe se guarda en el mismo
   archivo, con una copia `.bak` del original antes de la primera escritura. GB Tower sigue fuera.
   **Consola Virtual:** si Pokémon Rojo/Azul/Amarillo de la Consola Virtual está instalado (inglés, español,
   alemán, francés o italiano), su partida sale primero en esa lista sin copiar nada: se abre el guardado del
   propio título (`ARCHIVE_USER_SAVEDATA`, `sav.dat`). Si Stadium escribe, se escribe ahí mismo, se confirma
   (commit) y se vuelve a leer para comprobar; antes de la primera escritura quedan en `saves/`
   `ConsolaVirtual_<versión>_original.bak` (una sola vez) y `..._anterior.bak` (como estaba al arrancar). El
   resumen dice qué encontró (`[gb] Consola Virtual: ...`). Probado solo en el emulador (carpeta de guardado
   del título en `sdmc/Nintendo 3DS/.../title/00040000/00171000/data/00000001/`, con `gbauto.txt` y
   `gbprueba.txt` para forzar un guardado); `sinvc.txt` = 1 lo apaga.
   **Pantalla de abajo** (al arrancar, `gb_saves.inc`): los dos Transfer Pak (controles 1 y 2) con su cartucho,
   lista para elegirlos (cruz o tocando), X vuelve a buscar y abre la lista del Transfer Pak marcado, Y cambia el formato (pantalla completa o 4:3) y START
   empieza; si ya había algo recordado (`gbpak.cfg`) empieza sola en 5 s. Al jugar queda un tablero fijo, dibujado
   una vez. El control 2 solo "existe" para el juego cuando su Transfer Pak tiene cartucho (`osContInit`), sin
   botones. El formato 4:3 solo cambia el área de dibujo y el recorte de la pantalla en el backend
   (`gPsAspect43_3DS`, `patch_citro3d.py`). Letras y cartuchos son gráficos propios generados por
   `tools/port/gen_ui3ds.py` (`ui_gfx.inc`). Un `.gb` solo se usa por su cabecera (si su suma es correcta) junto
   a la partida del mismo nombre. No hay efecto 3D: el port dibuja una sola vista. Para el emulador:
   `sinmenu.txt` = 1 salta la pantalla (lo pone `run3ds.sh` salvo `MENU=1`) y `gbauto.txt` = 1 o 2 conecta los
   primeros cartuchos.
   **Etiquetas del usuario:** `tools/pc/agregar_portadas.bat [carpeta]` (en la PC) pega al final del `.3dsx` las
   imágenes `rojo`, `azul` y `amarillo` (`.png`, `.jpg`, `.bmp`) que el usuario ponga en `cartuchos\`; al arrancar
   el port las lee de su propio archivo (`[..][tamaño]"PSCV"`), las ajusta a la etiqueta (44×36, `stb_image.h`) y
   las guarda en `sdmc:/3ds/pokestadium/cartuchos/*.rgb`, donde se quedan. El repositorio y las compilaciones no
   traen ninguna imagen de esas.
4. Backend de 3DS y medición en Old 3DS.
5. Doble pantalla: arriba solo el combate; abajo UI táctil nativa (ataques con PP y tipo,
   cambio de Pokémon, HP/estado del equipo) que lee el estado del juego e inyecta la entrada,
   como `bottom_ui_3ds.cpp` del port de MK64.
6. Online por turnos (movimiento elegido + semilla de RNG). Usa la misma capa de entrada del punto 5.

## Assets (generar `pokestadium.o2r`)

```bash
tools/extraer_assets.sh ../baserom.z64
```

- 118 bins en la ROM → 710 recursos: 39 PERS-SZP sueltos, 10 archivos con 595 entradas internas,
  25 JPEG, 58 Yay0 y el bloque de audio (`0x15C0000`, 10 MB, formato `S1`).
- `STADIUM:PERS_SZP` exporta ya descomprimido (verificado byte a byte contra el algoritmo del juego).
- Por ahora JPEG, Yay0 y audio salen crudos; el juego los procesa como en N64.

## Audio y JPEG (HLE del RSP)

El audio (microcódigo naudio) y la descompresión JPEG de los fondos se emulan en alto nivel con
`third_party/mupen64plus-rsp-hle` (mupen64plus-rsp-hle, **GPLv2**, ver `third_party/mupen64plus-rsp-hle/LICENSES`
y `COMMIT`). Se compila en big-endian dentro del juego (`port/ps_rsp_hle.c` hace de puente). Por esa
dependencia, los binarios que incluyen el HLE quedan bajo GPLv2.

En el 3DS el sonido sale por NDSP: hace falta `sdmc:/3ds/dspfirm.cdc` (se saca una vez con DSP1).

Las voces (gritos de los Pokémon y narrador) son GSM. El decomp deja ese decodificador en ensamblador
(`func_80045FF0` y `asm/us/49190.s`); al compilar se traduce a C desde el ELF del decomp con `tools/port/m2c.py`
(instrucción por instrucción) y queda en `<salida>/gen/`. La traducción se comprobó contra el original con
`tools/port/verif/dx2.py`.

## Funciones del decomp que no hacen lo mismo que el original

El port compila con `NON_MATCHING` (el C de las funciones que todavía no igualan al original). Varias de esas
versiones en C tenían errores de verdad (fondo del laboratorio, recorte de las listas, voces, elección de equipo de
la consola...). `tools/port/verif/` las encuentra corriendo el original y el C en un emulador MIPS y comparando el
resultado; los arreglos están marcados con `PORT:` en el código. `PS_BOUNDS=1` al compilar anota en el registro los
índices fuera de sus arreglos (`[limites]`).

La pantalla de las cajas del laboratorio (PC > Pokémon) se congelaba al abrirse: `func_88209B54` (fragment23)
agregaba cada botón con `unk_44[i]`, con `i` sin valor; el original usa el índice del ciclo. El aviso
`-Wmaybe-uninitialized` de GCC lo señala (el port compila con `-w`); una pasada con ese aviso sobre todo `src/` no
deja ningún otro dentro de funciones `NON_MATCHING`.

## Laboratorio (PC, cajas del N64, intercambio)

Recorrido en el emulador con partidas de prueba hechas por `tools/port/gbsav.py` (`crear`: equipo, 12 cajas y objetos;
`cmp antes.sav despues.sav`: dónde quedó cada Pokémon y si algún dato cambió). Comprobado guardando y comparando
byte por byte: equipo ↔ caja guardada y caja actual, equipo/caja ↔ caja del N64 y de regreso, intercambiar, mover
cajas completas al N64 y de regreso, objetos mochila → PC, intercambio entre dos cartuchos, "Quit without saving".
El juego original no actualiza las sumas de las cajas en los bancos 2 y 3 (los juegos de Game Boy tampoco las
revisan) ni deja guardar con el equipo vacío. Dos errores del port que salieron ahí:

- **Aviso de lectura perdido** (`src/rsp.c`, `thread20_rsp`): el hilo que lee del cartucho manda el aviso de "listo"
  sin esperar; si la cola de quien pidió está llena, se pierde. En el N64 cada lectura tarda y nunca está llena; aquí
  leer es instantáneo y con dos hilos leyendo por la misma cola el segundo se quedaba esperando para siempre
  ("Release Pokémon" congelaba el juego). Ahora el hilo cede el paso hasta que el otro recoja su aviso.
- **Modelo del Pokémon en la ventana de la rejilla** (resumen: liberar, revisar, intercambio; es una imagen aparte,
  `G_PS_FB`): el juego pinta ahí primero el fondo con rectángulos y luego el modelo. El RDP por software repintaba
  el fondo en la memoria del juego en cada cuadro, encima de lo que el GPU había copiado (fondo + modelo). Ahora un
  destino que el cuadro anterior dibujó en el GPU ya no se toca por software (`prevGpuTargets`), su z se reconoce
  aunque sea el único z del cuadro (`PsFbIsFbZ3DS`), su color se limpia al entrar por primera vez en el cuadro y un
  rectángulo en modo copia marca la imagen entera como dibujada (bit de alfa de la copia).

**Campo de Free Battle.** Cada escenario de `stadium_models` es un fragment con una función `main(a0, a1)` en MIPS
que el port corre con un intérprete mínimo (`port/ps_mips_stub.c`). El escenario 0 (el campo de pasto de Free
Battle) es el único que usa `sltiu` (un `switch` con tabla de saltos) y no estaba soportado: `main` regresaba 0, el
escenario no se dibujaba y, como en batalla nadie limpia la pantalla, los Pokémon dejaban rastro. Con `sltiu` los 18
escenarios pasan (revisados sus `main` instrucción por instrucción); si falta otra queda `[mips] instruccion no
soportada` en el registro.

El vigilante de bloqueos anota además dónde quedó cada hilo del juego (`[bloqueo] hilo N: ...`), y
`mem_pool_alloc` avisa cuando no hay lugar (`[mem]`).

La barra de vida del HUD (`func_84310368`) se recorta a su ancho cuando la vida mostrada no corresponde a la
máxima (al cambiar de Pokémon podía salir varias veces más ancha); cada vez queda en el registro (`[hud]`).

## Imágenes aparte en el GPU (G_PS_FB)

Los retratos del HUD y el tablero del final de la pelea se dibujan en imágenes aparte. Dos detalles del N64 que
el port tiene que respetar: el juego limpia la imagen poniendo primero su z-buffer como destino y luego la imagen
(el relleno de color llega ya con `G_PS_FB`, que debe dejar al RDP apuntando a la imagen y limpiar la z del GPU);
y la textura del GPU va girada como la pantalla, así que su ancho es el alto de la imagen.

## Efectos de los ataques (fragment34)

Los efectos dibujan casi todo con un cuadrado compartido: cargan sus cuatro vértices en la lista del cuadro y
llaman una sublista que solo trae los triángulos (`D_8140E408`). Tres cosas del render los rompían:

- `gfx_convert.cpp` (mallas fijas) quitaba el `G_VTX` aunque sus triángulos estuvieran en una sublista, y el
  cuadrado salía con los vértices del dibujo anterior. Ahora solo quita los `G_VTX` que nadie más puede leer.
- `G_RM_FOG_PRIM_A` (tinte parejo con el alfa del registro de niebla, el color del Pokémon al recibir un estado)
  se trataba como niebla por vértice (`patch_interp_fog.py`).
- En el shader de vértices la segunda textura recibía U = V (`patch_shader.py`).
- Una malla dibujada por el CPU con geometría nativa (posiciones de objeto, matriz en el GPU) seguida de un lote
  de huesos o de malla fija salía con el estado nativo ya apagado: sus coordenadas de objeto se tomaban como de
  pantalla (el anillo de Earthquake cubría media pantalla). El lote ahora dibuja primero lo pendiente
  (`patch_interp_skin.py`).

Para verlos todos sin jugar cada ataque: con `sdmc:/3ds/pokestadium/ataques.txt` (contenido: el número del primer
movimiento) los dos lados recorren la lista de movimientos, uno nuevo cada vez que ya usaron el anterior, y ningún
golpe debilita (`fragment62_2F74E0.c`, `fragment62_2EC3C0.c` y `fragment62_359F90.c`, marcado `PORT`). El registro
anota `[ataque] a b` y `[usa] lado movimiento`. `tools/emu3ds/barrido.sh` arma la pelea en el emulador y
`tools/emu3ds/hoja.py` las hojas de capturas. Es solo para el banco de pruebas.

En modo repetición (`replay.psfd`): `sklog.txt` anota el material de cada lote (`[sk]`), las entradas del
combinador (`[tev]`) y el estado nativo de cada dibujo del CPU (`[nat]`); `convlog.txt`, qué hizo el convertidor
con cada lista (`[conv]`). `tools/port/cc.py w0 w1` decodifica un `G_SETCOMBINE`.

`tools/port/dl_traza.py volcado.psfd [-m] [-p]` imprime la lista de un volcado comando por comando (con `-m`
las matrices y con `-p` los vértices); así se encontraron. El ruido del combinador (`NOISE`) sigue siendo un
gris parejo: las nubes de polvo salen lisas en vez de chispeantes.

## Ritmo del juego y del sonido

El juego avanza un cuadro de lógica cada 1/30 s; cuando el 3DS se atrasa, se saltan cuadros de imagen para no
perder el paso. Dos reglas:

- **Atraso que se recupera:** a lo más 3 cuadros (0.1 s); lo demás se pierde (`ps_catchup_frames`,
  `port/os/os_events.c`, y `host_catchup_frames` en el host). Con 12, después de cada atorón el juego corría un
  rato más rápido que el tiempo real y se veía acelerado. Con la música del intro sonando son 12 (ver abajo).
  `alcance.txt` = N lo cambia para comparar.
- **Sonido:** se arma lo que falta en la salida para mantener una reserva (`ps_audio_fill`, `src/3D140.c`), como
  el N64 con `AI_LEN`. No se descarta sonido ya armado; si la salida se vacía, medio objetivo de silencio (0.16 s
  a lo más) y sigue. Antes iba por crédito de tiempo real y cada atorón terminaba en un silencio más varios
  cuadros de sonido descartados (cortes a media música). `sonidoreloj.txt` = 1 regresa a ese modo.
- **Intro (los Pokémon brincan al ritmo de la música): la imagen sigue a la música.** La música sale a su ritmo,
  con reserva grande (0.4 s, para aguantar la carga de cada escena) y se manda hasta 11 cuadros antes
  (`ps_intro_lead`). Cada ~0.2 s se compara lo que se oye contra el retrazo en que debería ir la imagen
  (`ps_frame_target_vi256`) y se corre el reloj de cuadros del juego (`ps_frame_clock_shift`): si la música va
  adelante, el juego queda atrasado y se pone al corriente sin dibujar esos cuadros (un brinco, hasta 12
  seguidos); si va atrás (se quedó sin sonido), la imagen la espera alargando los siguientes retrazos (va más lenta un momento, sin congelarse ni dejar al hilo de audio sin correr). El host decide qué cuadros dibuja con el
  atraso que le pasa el juego (`gPsDebtUs`), no con su propio reloj. Al arrancar la música se perdona el atraso
  que hubiera (`ps_frame_clock_resync`).
  Antes era al revés (la música seguía al reloj del juego con silencios, descartes y toques de 34 ms): con el
  3DS apurado la música se quedaba atrás y la imagen, al recuperar su atraso, se le adelantaba (medido en el
  emulador con la compilación de diagnóstico al 33 %: música 361 ms atrás en promedio, hasta 772).

El resumen (START+SELECT) trae `[rend] rafagas`: cuántas veces 10 cuadros del juego pasaron en menos de 222 ms
(más de 150 % del ritmo real, cuenta también los que no se dibujan); `[rend] acelerones vistos`: 5 cuadros
dibujados en menos de 100 ms (lo que sí se ve acelerado); `[rend] audio`: cortes y silencios; y las líneas
`[intro]`: desfase música - imagen y cuánto se adelantó o esperó la imagen. `tools/emu3ds/ritmo.sh` corre la
batalla de prueba al 33 % de reloj y `tools/emu3ds/intro.sh` deja correr el intro completo; los dos sacan esas
líneas.

## Pantalla de batalla en la pantalla de abajo (en curso)

Plan en 5 etapas: 1) leer la batalla, 2) los 4 ataques táctiles + Team + Run, 3) pantalla de equipos (minis sobre
su base, color según la vida, VS en medio; el rival no visto solo muestra la base), 4) animaciones (brinco según
la vida, adelante/atrás al atacar, base sola al debilitarse), 5) script para que el usuario ponga sus minis.
Todo en inglés. Solo se redibuja la franja del VS por cuadro; lo demás, al cambiar algo.

Etapa 1, lo ya comprobado en el emulador (`batalladump.txt` = 1 vuelca estos datos cada vez que el jugador 1
llega a su menú; `func_8430EF4C`, `fragment62_2F74E0.c`):

- `D_84390010[lado]->unk_654.unk_04 == 1`: ese lado está en su menú de turno.
- Pokémon activo de cada lado: `D_84390010[lado]->unk_654.unk_38` (`unk_D_800FCB18`, 0x64 bytes):
  `0x0B` especie (número de Pokédex), `0x0C` vida actual (u16), `0x14/0x15` estado, `0x1C/0x1D` tipos (ids de
  Game Boy), `0x1F..0x22` movimientos, `0x26` nivel, `0x28` vida máxima (u16), `0x2A/2C/2E/30` ataque, defensa,
  velocidad, especial, `0x32..0x35` PP (6 bits bajos).
- `D_800FCB18[lado]` apunta a una copia del activo, **no** al equipo.

Hecho (etapa 2, primera parte): tablero de batalla de solo lectura (`gb_bat_tick`, `gb_draw_battle` en
`gb_saves.inc`): el juego sube `gPsBatTick` en cada paso del jugador 1 y el host, cada 100 ms, lee el activo de
cada lado y redibuja solo si cambió algo. Nombres y tipos de los movimientos: `moves_gen1.inc`. `sintablero.txt`
= 1 lo apaga para comparar FPS (emulador, batalla de prueba al 33 %: 16.9 sin tablero, 16.8 con él).

Hecho (etapa 2, segunda parte): los 4 ataques, Pokemon y Run se tocan. Un ataque manda al control 1 pulsos de A
hasta que el juego abre su menú (`unk_04 == 1`) y luego el botón C de esa casilla (arriba, derecha, abajo,
izquierda = casillas 0-3), 4 s a lo más (`gb_bat_buttons`, sumado en `host_read_pad`). El tablero se muestra
mientras `gPsBatOn` (lo pone `func_84301A2C`). `toque.txt` = N toca solo el ataque N cada 3 s (prueba en el
emulador, que no tiene táctil): así se jugó una batalla completa sin botones.

Hecho (etapa 3): pantalla de equipos (`gb_draw_team`). El equipo de cada lado es
`D_84390010[lado]->unk_724` (`unk_D_800AE540_0004`: `unk_002` cuántos pelean, `unk_01C[]` de 0x54 bytes con
`+0x00` especie, `+0x02` vida, `+0x26` vida máxima); el que pelea es el índice `unk_654.unk_08`. Los del jugador
que pelean son los índices 0..2 en el orden de los botones C; los del rival pueden ser cualquiera de los seis, así
que se muestran conforme van saliendo. La vida del equipo baja junto con la barra que se ve; la de `unk_38` ya
trae el resultado desde antes de la animación, por eso se usa la del equipo. Solo se guarda el nivel de vida
(bien, media, poca, debilitado) para no redibujar con cada punto. Cambiar tocando: B hasta `unk_04 == 2` y luego
el botón C del lugar (arriba, derecha, abajo); si el Pokémon se debilitó, el botón C directo. `toque.txt` = 8
juega solo una batalla con cambio voluntario y cambios forzados (así se probó en el emulador).

Hecho (etapa 5): minis y base del usuario. `tools/pc/agregar_portadas.ps1` (también `agregar_minis.bat`) pega
al `.3dsx`, además de las etiquetas, un PNG por Pokémon (`mini_NNN.png`) y la base (`ball.png`); los busca en
`minis\` por nombre en inglés o número, o dentro de una copia de PokéSprite (`pokemon-gen8\regular`). El port
los recorta, los ajusta a 32×32 y los guarda en `cartuchos/minis.bin` (un registro RGBA por número de Pokédex; el
0 es la base); en batalla lee solo los que hacen falta (`gb_mini_get`). Sin ellos salen los marcadores con número.
El repositorio y las compilaciones no traen ninguna de esas imágenes.

Ajustes tras probar en la consola: lo tocado se ilumina 0.25 s (blanco; rojo si no se puede: sin PP, debilitado
o el que ya pelea) y el resumen cuenta los toques (`[tablero] toques detectados`). "Pokemon" abre siempre la
pantalla de equipos, también con equipos de 4 a 6 (Free Battle): ahí van en dos columnas y el cambio usa los
botones de `func_8430E72C` para ese caso (C arriba, C izquierda, B, C derecha, C abajo, A). El mini va delante de
la base. `toque.txt` = 8 ahora toca por coordenadas, como el dedo. El caso de 4 a 6 no se ha probado en el
emulador (la batalla de prueba es de 3).

El Pikachu de Yellow tiene su propio mini (registro 152 de `minis.bin`, `mini_152.png` al pegar): el juego marca al
Pikachu inicial al cargarlo del cartucho (`unk_52 & 0x80`, `func_82304BEC`, la misma marca de su voz) y el tablero la
lee del equipo. Sin ese mini sale el normal. `minialt.txt` = N (solo emulador) trata a la especie N como marcada: así
se vio dibujado; con un cartucho de Yellow de verdad no se ha probado.

`tools/pc/minis.py` (Python 3, sin dependencias) hace lo mismo que `agregar_portadas.ps1` y además puede bajar
PokéSprite a la PC del usuario y sacar a `minis\` los 151, los Pikachu y la Poké Ball (`--zip` para un ZIP ya bajado,
`--solo-pegar` para no bajar). El repositorio no lleva ninguna de esas imágenes.

La base (`ball.png`) se guarda aparte, de 40x40 (`base.bin`), y va centrada detrás del mini, que es de 32: así
asoma alrededor. La del rival se dibuja en espejo. Una base importada con una versión anterior (solo el registro 0,
de 32) se sigue viendo como antes hasta volver a pegar las imágenes.

Órdenes por toque (rehecho tras probar en la consola): cada toque es un encargo que se cumple paso a paso mirando
el menú del juego en cada lectura del control (`unk_10`: 0 principal, 1 ataques, 2 cambio, 3 orden dada, 4 pregunta
de huir; `unk_8C4.unk_00`: aviso en pantalla). Se aprieta un solo botón por paso, se sostiene hasta que el juego
reacciona y el siguiente sale del estado en que quedó; ya no hay pulsos repetidos. Si está abierto el otro menú se
cierra con L, y un aviso previo se quita con Z. Solo se acepta un toque cuando el juego está pidiendo la orden
(`gPsBatTick` avanzó en los últimos 0.3 s, o está en el menú de cambio o en la pregunta de huir); si no, los botones
salen apagados con "Wait..." y el toque se marca en rojo. "Run" abre la pregunta del juego y abajo salen "No, go
back" y "Yes, run". El resumen dice cuántas órdenes se cumplieron, cuántos toques fueron antes de tiempo, cuántas
negó el juego y cuántas quedaron sin respuesta; cada orden deja su línea `[tablero]` con botones y milisegundos.
`toque.txt` = 9 prueba todo eso en el emulador (incluye huir y contestar que no, y tocar antes de tiempo).

Minis: el del jugador va en espejo (quedan frente a frente). Hay tres bases de 40x40: `ball.png` (jugador),
`ball_rival.png` (rival; sin ella usa la del jugador) y `ball_swap.png` (transición). La de transición se ve sola, sin
mini, mientras el Pokémon no está a la vista: al empezar y tras debilitarse uno, hasta que el juego vuelve a pedir la
orden; en un cambio a media ronda, 2.5 s. También marca los lugares del rival que aún no han salido.

Animación (etapa 4), solo en la franja del VS y solo el mini (la base no se mueve): salta según la vida (completa:
un salto cada 0.6 s; media: cada 1.2 s; poca: cada 2.4 s; el rival a contratiempo) y, cuando a uno le empieza a bajar
la vida, ese se hace para atrás y el otro se le va encima (0.4 s). El golpe se detecta en la vida del equipo, que baja
junto con la barra: una vez por golpe (también salta con veneno o retroceso). `golpeprueba.txt` (emulador) lo dispara
cada 3 s para verlo.

Ajustes tras probar la animación en la consola: el que recibe el golpe ya no solo se hace para atrás: lo empujan,
cae de espaldas (0.6 s acostado: el mini girado un cuarto de vuelta) y se levanta con un brinco. La base de transición
gira (un cuarto de vuelta cada 0.2 s) mientras el Pokémon no está a la vista. Quién está en el campo sale del lugar
activo del equipo (`unk_08`), no del dato del que pelea (`unk_38`): ese cambia desde que se elige al siguiente, y por
eso el elegido se pintaba antes de entrar (y el del rival se adelantaba). El botón "Pokemon" también se apaga y no
responde cuando el juego no pide la orden. Para que el primer toque no detenga el juego, `minis.bin` y las bases se
leen al empezar la batalla (antes se leían de la SD la primera vez que salía cada mini).

Segundo ajuste (la consola se "paralizaba" la primera vez que se abría un menú por toque y tras debilitarse el
primero): las órdenes ya no usan milisegundos sino lecturas del control. El juego lee una vez por vuelta
(`Cont_ReadInputs`) y ahí mismo ve el botón nuevo; si se detiene a cargar algo no hay lecturas, así que el botón lo
espera apretado y no se pierde (antes se soltaba a los 250 ms y la orden caducaba a los 3 s: con el juego detenido, la
orden se quedaba a medias con el menú del juego abierto). Se sostiene dos lecturas al menos y hasta que el juego
reacciona (ocho a lo más), se suelta dos, y la orden se deja a las 150 vueltas. "Pidiendo la orden" tolera 0.6 s sin
latido. El resumen cuenta las vueltas del juego de más de 0.4 s durante la batalla (`[tablero] pausas del juego...`) y
cuántas cayeron con una orden en curso: sirve para saber si el tirón es del juego (cargar su menú) o del tablero.
En el emulador no hay pausas, así que la causa en la consola sigue sin confirmar.

La animación del golpe usa además `gPsAtkStart[lado]` (lo sube `func_84302A78` al arrancar un movimiento): el que cae
se queda tirado hasta que le toca atacar (se levanta con un brinco justo antes) o hasta que el juego vuelve a pedir la
orden; el que ataca estando de pie da un brinco de impulso, y se le va encima al otro cuando a ese le baja la vida.

Logo del autor: mientras abajo se eligen los cartuchos, en la pantalla de arriba queda el logo del autor (PokéThrasher,
`platform/3ds/host/assets/pokethrasher.png`, suyo) sobre negro; al empezar el juego la pantalla vuelve a negro. Va dentro
del programa como JPEG de 240x240 (8 KB, `tools/port/gen_splash.py` → `splash_logo.inc`), se abre con stb_image y se
suelta enseguida. No sale cuando se salta la pantalla de cartuchos (`sinmenu.txt`, `gbauto.txt`). El autor del SMDH es
"Pokethrasher".

**La causa de la "parálisis" de los primeros toques (confirmada con el log.txt de la consola, rev 1cade09):** un
resto de depuración en `dump_frame_if_listed` grababa el cuadro actual (toda la memoria del juego, 14 MB, a la SD)
cada vez que se tocaba la pantalla de abajo, las primeras 8 veces de cada corrida. En el registro son 8 líneas
`[dump] frame N`, cada una con su `[tablero] el juego se detuvo ~2.8 s` y su `[pico] ... juego 2700-2900 ms`. El
emulador no tiene pantalla táctil, por eso ahí nunca salió. Quitado: los volcados solo se hacen con `dump.txt`. Los
`dump_NNNNN.psfd` que dejaron los toques se borran solos al arrancar si no hay `dump.txt` (`dump_cleanup`).
Con ese mismo registro: en la consola un dibujo completo del tablero cuesta 16 ms y un paso de animación 1.6 ms
(8 ms por segundo de batalla).

Memoria del tablero: una sola capa guardada (230 KB de la lineal, se repinta al cambiar entre batalla y equipos),
los cuatro botones de ataque (82 KB) y 14 minis en memoria (56 KB) que se leen todos al empezar la batalla. Tener
`minis.bin` entero y dos capas (1.1 MB) dejaba la lineal en 5.7 MB en batalla y los modelos retenidos fallaban por
memoria (`[r] fallas: ... mem`); en el heap normal le quitaba el margen al cache de assets.

Costo del dibujo de la pantalla de abajo (todo por software): la animación redibuja solo las dos celdas, a 10 pasos
por segundo a lo más: se repone su fondo guardado (ya con la base) y se pinta el mini. La pantalla tiene dos páginas,
así que tras cada dibujo completo la otra página se deja igual. Lo tocado se ilumina guardando y reponiendo su
rectángulo, sin dibujo completo. Lo fijo de cada pantalla (fondo, título, tableros, botones) se guarda pintado en una
página de la memoria lineal, y cada botón de ataque en su propio pedazo (se rehace solo si cambia el ataque o sus PP).
Apagar los botones es un oscurecido directo. En el emulador (reloj al 33 %): un dibujo completo bajó de ~100 ms a ~20
ms y un paso de animación cuesta ~1.4 ms; en total ~17 ms por segundo de batalla. El resumen lo dice en la consola:
`[tablero] dibujo: N completos de X us; animacion M pasos de Y us (Z ms por segundo ...)`. Equipos de más de 3
(si algún modo los usa) abren el menú del juego en vez de la pantalla de equipos.

## Banco de pruebas del 3DS (sin consola)

`platform/3ds/test/build_test.sh <salida de build_blob_3ds.sh>` arma el mismo bloque ARM BE8 del .3dsx
con un host mínimo que corre bajo `qemu-armeb` (Linux): vuelca frames (para `tools/viewer`), guarda el
audio y avisa si el juego se atora.

## Instalable (.cia)

`platform/3ds/cia/` tiene lo del titulo instalable: `pokestadium.rsf` (titulo `000400000F5A1700`, 64 MB como una app
de la Homebrew Launcher, acceso directo a la SD), `icon.png` (48x48, tambien es el icono del .3dsx), `banner.png`
(256x128) y `banner.wav` (1 s de silencio); las imagenes son el logo del autor sobre negro. El .cia se arma en la PC
con `tools/pc/hacer_cia.bat` (makerom de 3DSGuy/Project_CTR y bannertool de Steveice10; los baja la primera vez) a
partir de `pokestadium.elf` de la compilacion normal. Sin RomFS: todo se lee de `sdmc:/3ds/pokestadium/` igual que
con el .3dsx; las imagenes pegadas se siguen importando de `sdmc:/3ds/pokestadium.3dsx` si esta ahi.

## Pantalla de abajo fuera de batalla

- **Pantalla libre** (`platform/3ds/host/gb_roam.inc`): los Pokémon de los equipos de los cartuchos conectados (se leen
  de la partida, `0x2F2C`) caminan por un campo de pasto, chocan entre ellos y se pueden agarrar y aventar con la pluma;
  el que sale del campo regresa caminando por una orilla. El aro de cada uno es del color de su cartucho. `L` (o tocar
  el botón) cambia a la pantalla de los cartuchos y de regreso. Dibujo: el fondo queda guardado y cada paso (10 por
  segundo; 30 con uno agarrado o volando) solo repone los pedazos de los minis. En el emulador: 1.5 a 3 ms por paso
  (13 a 37 ms por segundo). `libreprueba.txt` = 1 o 2 la prueba sin pantalla táctil; línea `[libre]` del resumen.
- **Elección antes de la batalla** (`gb_sel.inc`, pantalla "Choose the order" del fragment64): el juego publica en
  `gPsSel` el estado del jugador humano del control 1 (`func_84802C40`). Tocar la tarjeta de un Pokémon manda el botón
  que le toca en el original (B, C izquierda, C arriba, A, C abajo, C derecha); "OK!" contesta "Yes" y "Start over"
  borra lo elegido. La cantidad sale de la regla (3; en Free Battle sin copa, todos los que traiga el equipo).
  `selprueba.txt` = 1 la prueba sola; línea `[eleccion]`.
- En batalla la barra "A BATTLE / B POKéMON / S RUN" de arriba ya no se dibuja (`func_84313148`).
