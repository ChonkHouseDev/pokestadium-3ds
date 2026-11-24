#!/usr/bin/env bash
# Arma pokestadium.3dsx: bloque del juego (ARM big-endian) + host del 3DS con libultraship/citro3d.
#
# Requiere:
#   DEVKITPRO        devkitPro con devkitARM, libctru y citro3d
#   MK64_3DS         checkout de EstebanPdN/mario-kart-64-3ds (third_party/SpaghettiKart poblado)
#   MK64_3DS_BUILD   build de ese proyecto con el objetivo mk64-3ds-interpreter-probe ya compilado
# Uso: platform/3ds/build_3ds.sh <DECOMP_DIR> <SALIDA>
#   PS_DIAG=1  compilacion de diagnostico (pokestadium_diag.3dsx): verificacion de la cache de modelos siempre
#              encendida, registro con bufer y resumen final en sdmc:/3ds/pokestadium/log.txt. Mas lenta; no es para jugar.
#   PS_PROF=1  contadores por opcode en el interprete
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DECOMP=$(cd "$1" && pwd)
OUT=$(mkdir -p "$2" && cd "$2" && pwd)
DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
MK64_3DS=${MK64_3DS:-/tmp/mk64}
MK64_3DS_BUILD=${MK64_3DS_BUILD:-/opt/mk64-3ds-build}
# 1) Bloque del juego (GCC arm-none-eabi del sistema: admite big-endian BE8)
"$ROOT/tools/port/build_blob_3ds.sh" "$DECOMP" "$OUT/blob" > "$OUT/blob.log" 2>&1 || { tail -20 "$OUT/blob.log"; exit 1; }
export PATH=$DEVKITPRO/devkitARM/bin:$DEVKITPRO/tools/bin:$PATH

# 2) Datos del bloque para el host
sym() { arm-none-eabi-nm "$OUT/blob/blob.elf" | awk -v s="$1" '$3==s{print "0x"$1}'; }
symz() { local v=$(sym "$1"); echo "${v:-0x0}"; }
END=$(sym __ps_end)
SIZE=$(printf '0x%08X' $(( ((END - 0x0A000000) + 0xFFFF) & ~0xFFFF )))
cat > "$OUT/blob_info.h" <<EOF
/* Generado por platform/3ds/build_3ds.sh */
#define BLOB_BASE      0x0A000000u
#define BLOB_SIZE      ${SIZE}u
#define BLOB_TEXT_END  $(sym __ps_text_end)u
#define BLOB_ENTRY     $(sym _blob_start)u
#define BLOB_MIPS_TRAP $(sym ps_mips_trap_shim)u
#define BLOB_CUR_THREAD $(sym gPsCurThread)u
#define BLOB_DIAG_WAIT $(sym gPsDiagWait)u
#define BLOB_BATTLE_MON $(sym D_84390010)u
#define BLOB_ACMD_TICKS $(symz gPsAcmdTicks)u
#define BLOB_ACMD_N $(symz gPsAcmdN)u
#define BLOB_AUD_SYNC $(symz gPsAudSync)u
#define BLOB_INTRO $(symz gPsIntro)u
#define BLOB_DEBT $(symz gPsDebtUs)u
#define BLOB_BAT_TICK $(symz gPsBatTick)u
#define BLOB_BAT_ON $(symz gPsBatOn)u
#define BLOB_GAME_STATE $(symz gCurrentGameState)u
#define BLOB_PROF_PORTRAIT $(symz gPsProfPortrait)u
#define BLOB_PROF_NAME $(symz gPsProfName)u
#define BLOB_LANG_GEN $(symz gPsLangGen)u
#define BLOB_JP_ON $(symz gPsJpSceneOn)u
#define BLOB_JP_SFX $(symz gPsJpSnd)u
#define BLOB_ATK_START $(symz gPsAtkStart)u
#define BLOB_HPBAR $(symz gPsHpBar)u
#define BLOB_FIBERS $(symz sFibers)u
#define BLOB_SEL $(symz gPsSel)u
#define BLOB_DYNFREE $(symz gPsDynFree)u
#define BLOB_DYNSIZE $(symz gPsDynSize)u
#define BLOB_SND $(symz gPsSnd)u
#define BLOB_NOT_SCREEN $(symz gPsNotScreenFb)u
#define BLOB_AE540 $(symz D_800AE540)u
#define BLOB_HOUR $(symz gPsHour3DS)u
#define BLOB_TIMEMODE $(symz gPsTimeMode)u
#define BLOB_CHAL_END $(symz gPsChalEnd)u
#define BLOB_BGTEST $(symz gPsBgTest3DS)u
#define BLOB_JUMBO $(symz gPsJumboTex)u
#define BLOB_JUMBO_ON $(symz gPsJumboOn)u
EOF
(cd "$OUT" && cp blob/blob.bin blob.bin && bin2s -H blob_bin.h blob.bin > blob_bin.s)

# 3) Host
ARCH="-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -mword-relocations"
INC="-I$OUT -I$OUT/psinc -I$ROOT/platform/3ds/host -I$MK64_3DS/platform/3ds/include -I$MK64_3DS/platform/3ds/probe_stubs -I$MK64_3DS/third_party/SpaghettiKart/libultraship/include -I$MK64_3DS/platform/3ds/compat \
 -I$MK64_3DS/third_party/SpaghettiKart/libultraship/include/libultraship -I$DEVKITPRO/libctru/include"
CXXFLAGS="$ARCH -O2 -g -std=gnu++20 -ffunction-sections -D__3DS__ -DF3DEX_GBI=1 -include array -Wno-unused-parameter"
REV=$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo "?")$(git -C "$ROOT" diff --quiet HEAD 2>/dev/null || echo "+cambios")
HOSTDEFS="-DPS_BUILD_REV=\"$REV\""
NAME=pokestadium; TITLE="Pokemon Stadium"
[ -n "${PS_HANG_TEST:-}" ] && HOSTDEFS="$HOSTDEFS -DPS_HANG_TEST=1"
# PS_BLACKBOX=1: caja negra (pokestadium_caja.3dsx): validacion de todo lo que va al GPU y estado en la pantalla de abajo
BOXOBJ=""; BOXLD="-Wl,--wrap=C3D_UpdateUniforms,--wrap=C3D_DrawArrays,--wrap=C3D_DrawElements" # red de seguridad: ningun uniforme llega al GPU con NaN o infinito (host_3ds.cpp)
# PS_LOGS=1: con registro (log.txt, terminal de 3dslink). Sin eso el juego no registra nada.
if [ -n "${PS_LOGS:-}" ]; then HOSTDEFS="$HOSTDEFS -DPS_LOGS=1"; fi
if [ -n "${PS_BLACKBOX:-}" ]; then
  HOSTDEFS="$HOSTDEFS -DPS_BLACKBOX=1"; BOXOBJ="$OUT/blackbox.o"
  # PS_BOXLITE=1: solo la captura del GPU colgado (sin migas ni pantalla de diagnostico): corre como el juego normal
  [ -n "${PS_BOXLITE:-}" ] && HOSTDEFS="$HOSTDEFS -DPS_BOXLITE=1"
  BOXLD="$BOXLD -Wl,--wrap=GX_ProcessCommandList,--wrap=GX_DisplayTransfer,--wrap=GX_TextureCopy,--wrap=GX_MemoryFill,--wrap=GX_BindQueue"
fi
if [ -n "${PS_MEMPROF:-}" ]; then HOSTDEFS="$HOSTDEFS -DPS_MEMPROF=1"; BOXLD="$BOXLD -Wl,--wrap=memset,--wrap=memcpy,--wrap=memcmp"; fi
[ -n "${PS_F2_OFF:-}" ] && HOSTDEFS="$HOSTDEFS -DPS_F2_OFF=1" # pruebas: ruta F1 sin presionar L
if [ -n "${PS_DIAG:-}" ]; then HOSTDEFS="$HOSTDEFS -DPS_DIAG=1"; NAME=pokestadium_diag; TITLE="Pokemon Stadium (diagnostico)"; fi
if [ -n "${PS_BLACKBOX:-}" ] && [ -z "${PS_BOXLITE:-}" ]; then NAME=pokestadium_caja; TITLE="Pokemon Stadium (caja negra)"; fi
arm-none-eabi-g++ $CXXFLAGS $HOSTDEFS $INC -c "$ROOT/platform/3ds/host/host_3ds.cpp" -o "$OUT/host_3ds.o"
arm-none-eabi-g++ $CXXFLAGS $INC -c "$ROOT/platform/3ds/host/gfx_convert.cpp" -o "$OUT/gfx_convert.o"
mkdir -p "$OUT/migas"
if [ -n "${PS_BLACKBOX:-}" ] && [ -z "${PS_BOXLITE:-}" ]; then
  python3 "$ROOT/platform/3ds/crumbs.py" "$ROOT/platform/3ds/host/model_cache.cpp" "$OUT/migas/model_cache.cpp" 4000 "$OUT/migas/n_model.inc" || exit 1
  arm-none-eabi-g++ $CXXFLAGS $INC -I"$ROOT/platform/3ds/host" -c "$OUT/migas/model_cache.cpp" -o "$OUT/model_cache.o" || exit 1
else
arm-none-eabi-g++ $CXXFLAGS $INC -c "$ROOT/platform/3ds/host/model_cache.cpp" -o "$OUT/model_cache.o"
fi
[ -n "$BOXOBJ" ] && arm-none-eabi-g++ $CXXFLAGS ${PS_BOXDBG:+-DPS_BOXDBG=1} $INC -c "$ROOT/platform/3ds/host/blackbox.cpp" -o "$BOXOBJ"
if [ -n "${PS_BLACKBOX:-}" ] && [ -z "${PS_BOXLITE:-}" ]; then
  python3 "$ROOT/platform/3ds/crumbs.py" "$ROOT/platform/3ds/host/soft_rdp.cpp" "$OUT/migas/soft_rdp.cpp" 5000 "$OUT/migas/n_soft.inc" || exit 1
  arm-none-eabi-g++ $CXXFLAGS $INC -I"$ROOT/platform/3ds/host" -c "$OUT/migas/soft_rdp.cpp" -o "$OUT/soft_rdp.o" || exit 1
else
arm-none-eabi-g++ $CXXFLAGS $INC -c "$ROOT/platform/3ds/host/soft_rdp.cpp" -o "$OUT/soft_rdp.o"
fi
arm-none-eabi-gcc $ARCH -c "$ROOT/platform/3ds/host/tramp.s" -o "$OUT/tramp.o"
arm-none-eabi-gcc $ARCH -c "$OUT/blob_bin.s" -o "$OUT/blob_bin.o"

B=$MK64_3DS_BUILD
# El interprete de MK64 se compila para F3DEX; Stadium usa F3DEX2 (G_SHADING_SMOOTH y otras banderas cambian
# de bit). Se recompila la misma fuente con F3DEX_GBI_2 y se enlaza antes que la libreria.
# Ademas: Stadium mezcla 2D en coordenadas de pantalla con 3D; la correccion de aspecto de MK64 solo encoge el 3D
# (quedaba amontonado). Sin correccion todo se estira igual a los 400 px.
# F2: el backend de citro3d gana DrawPsResident3DS (host/ps_resident_backend.inc); su clase se declara en una copia del
# encabezado de MK64 que todos los que lo incluyen ven primero
mkdir -p "$OUT/psinc"
python3 - "$MK64_3DS/platform/3ds/include/gfx_citro3d.h" "$OUT/psinc/gfx_citro3d.h.new" <<'PYEOF' || exit 1
import sys
s = open(sys.argv[1]).read()
a = "    void DrawStaticRun(const StaticRunDraw3DS& draw);\n"
if a not in s: sys.exit("gfx_citro3d.h: no encontre DrawStaticRun")
s = s.replace(a, a + "    void DrawPsResident3DS(const ::PsResidentDraw3DS& draw);\n    bool PsLayerBegin3DS(int layer);\n    void PsLayerEnd3DS();\n    void PsLayerComposite3DS(int layer);\n", 1)
s = s.replace("#pragma once\n", "#pragma once\nstruct PsResidentDraw3DS;\n", 1)
open(sys.argv[2], "w").write(s)
PYEOF
cmp -s "$OUT/psinc/gfx_citro3d.h.new" "$OUT/psinc/gfx_citro3d.h" && rm "$OUT/psinc/gfx_citro3d.h.new" || mv "$OUT/psinc/gfx_citro3d.h.new" "$OUT/psinc/gfx_citro3d.h"
# Espera de la cola del GPU al empezar el cuadro: el original se sale del proceso a los 5 s (svcExitProcess), y eso en
# la consola se ve como congelamiento total (imagen fija, HOME sin responder, hay que apagar). Aqui a los 5 s se avisa
# al host y se sigue esperando: el host atiende HOME mientras tanto, y si el GPU termina el juego continua.
python3 - "$MK64_3DS/platform/3ds/include/frame_begin_3ds.hpp" "$OUT/psinc/frame_begin_3ds.hpp.new" <<'PYEOF' || exit 1
import sys
s = open(sys.argv[1]).read()
a = """            Mk64StartupFailure("startup-gpu-queue-timeout-kernel-exit");
            if (Mk64Gpu3DSFenceFailure) Mk64Gpu3DSFenceFailure("gpu-queue-timeout");
            svcExitProcess();"""
b = """            Mk64StartupFailure("startup-display-present-timeout-kernel-exit");
            if (Mk64Gpu3DSFenceFailure) Mk64Gpu3DSFenceFailure("display-present-timeout");
            svcExitProcess();"""
if a not in s or b not in s: sys.exit("frame_begin_3ds.hpp: no encontre las salidas por tiempo")
s = s.replace(a, "            psStalled = true; PsGpuStall3DS(1, static_cast<unsigned>(osGetTime() - started));")
s = s.replace(b, "            psStalled = true; PsGpuStall3DS(2, static_cast<unsigned>(osGetTime() - presentStarted));")
s = s.replace("    const uint64_t started = osGetTime();\n    while (!C3D_FrameBegin(C3D_FRAME_NONBLOCK)) {", "    bool psStalled = false;\n    const uint64_t started = osGetTime();\n    while (!C3D_FrameBegin(C3D_FRAME_NONBLOCK)) {")
s = s.replace('    Mk64StartupStage("startup-gpu-queue-wait-returned");', '    if (psStalled) PsGpuStall3DS(0, static_cast<unsigned>(osGetTime() - started));\n    Mk64StartupStage("startup-gpu-queue-wait-returned");')
s = s.replace("namespace mk64_3ds {", 'extern "C" void PsGpuStall3DS(int what, unsigned ms); // 1 cola del GPU, 2 presentacion, 0 ya paso\nnamespace mk64_3ds {', 1)
s = s.replace("if (osGetTime() - started >= 5000) {", "if (osGetTime() - started >= 1500) {")
if "started >= 1500" not in s: sys.exit("frame_begin_3ds.hpp: no encontre el limite de la cola")
if "bool psStalled = false;" not in s or "PsGpuStall3DS(0" not in s: sys.exit("frame_begin_3ds.hpp: parche incompleto")
open(sys.argv[2], "w").write(s)
PYEOF
cmp -s "$OUT/psinc/frame_begin_3ds.hpp.new" "$OUT/psinc/frame_begin_3ds.hpp" && rm "$OUT/psinc/frame_begin_3ds.hpp.new" || mv "$OUT/psinc/frame_begin_3ds.hpp.new" "$OUT/psinc/frame_begin_3ds.hpp"
PSDEPS="$ROOT/platform/3ds/host/ps_hud_interp.inc $ROOT/platform/3ds/host/ps_resident.h $ROOT/platform/3ds/host/ps_resident_interp.inc $ROOT/platform/3ds/host/ps_resident_backend.inc $OUT/psinc/gfx_citro3d.h $OUT/psinc/frame_begin_3ds.hpp"
newer() { local o=$1; shift; [ ! -f "$o" ] && return 0; for f in "$@"; do [ "$f" -nt "$o" ] && return 0; done; return 1; }
INTERP_SRC="$OUT/interpreter_stadium.cpp"
sed 's/sMk64AspectCorrection3DS = ::mk64_3ds::Fast3DAspectCorrection(/sMk64AspectCorrection3DS = 1.0f; (void)(/' \
  "$B/generated/interpreter_3ds.cpp" > "$INTERP_SRC.new"
grep -q "sMk64AspectCorrection3DS = 1.0f" "$INTERP_SRC.new" || { echo "no encontre la correccion de aspecto"; exit 1; }
# Alpha compare sin transparencia (sprites en modo copia/opaco con G_AC_THRESHOLD): el backend de citro3d solo
# saca el alfa de la textura del TEV si el shader tiene ALPHA, y la prueba de alfa descartaba todo. Se pide el
# alfa en el shader; la mezcla sigue apagada porque SetUseAlpha recibe use_alpha.
perl -0pi -e 's/(    if \(alpha_threshold\) \{\n        cc_options \|= SHADER_OPT\(ALPHA_THRESHOLD\);)/    if (alpha_threshold \&\& !use_alpha) {\n        cc_options |= SHADER_OPT(ALPHA);\n    }\n$1/' "$INTERP_SRC.new"
grep -q "alpha_threshold && !use_alpha" "$INTERP_SRC.new" || { echo "no pude parchear alpha threshold"; exit 1; }
# (Probado: camino nativo GPU para triangulos con descarte. En la consola duplico los envios al GPU, un
# lote por matriz de hueso, y fue mas lento: 3.4 s contra 2.6 s de dibujo cada 60 cuadros. Se dejo apagado.)
# Corridas estaticas de MK64 (geometria ya armada que se vuelve a dibujar en el GPU): MK64 solo las graba para
# listas de sus archivos de recursos. En Stadium los vertices convertidos tienen direccion fija (gfx_convert),
# asi que todas las listas cuentan como "recurso"; el contenido se valida por hash.
if [ -z "${PS_NO_STATIC:-}" ]; then
  perl -0pi -e 's/return sMk64ExecStackDepth3DS != 0 && sMk64ResourceFrame3DS\[sMk64ExecStackDepth3DS - 1\];/return sMk64ExecStackDepth3DS != 0;/' "$INTERP_SRC.new"
  grep -q "return sMk64ExecStackDepth3DS != 0;" "$INTERP_SRC.new" || { echo "no pude activar las corridas estaticas"; exit 1; }
fi
# Normalizar un vector de largo cero daba 0/0 = NaN (luz o lookat con direccion cero, o hueso con escala cero). En la
# PC no pasa nada; aqui ese NaN llega al GPU como direccion de luz en los uniformes de los huesos y el PICA200 de la
# consola se queda colgado dibujando (Earthquake, Fissure, Sky Attack). Un vector de largo cero se queda en cero.
perl -0pi -e 's/(void Interpreter::NormalizeVector\(float v\[3\]\) \{\n    float s = sqrtf\(v\[0\] \* v\[0\] \+ v\[1\] \* v\[1\] \+ v\[2\] \* v\[2\]\);\n)/$1    if (!(s > 0.0f) || s > 3.0e38f) { v[0] = v[1] = v[2] = 0.0f; return; }\n/' "$INTERP_SRC.new"
grep -q "if (!(s > 0.0f) || s > 3.0e38f)" "$INTERP_SRC.new" || { echo "no pude parchear NormalizeVector"; exit 1; }
python3 "$ROOT/platform/3ds/patch_interp_tex.py" "$INTERP_SRC.new" || exit 1
python3 "$ROOT/platform/3ds/patch_interp_skin.py" "$INTERP_SRC.new" || exit 1
python3 "$ROOT/platform/3ds/patch_interp_mat.py" "$INTERP_SRC.new" || exit 1
python3 "$ROOT/platform/3ds/patch_interp_atlas.py" "$INTERP_SRC.new" || exit 1
python3 "$ROOT/platform/3ds/patch_interp_fb.py" "$INTERP_SRC.new" || exit 1
python3 "$ROOT/platform/3ds/patch_interp_fog.py" "$INTERP_SRC.new" || exit 1
python3 "$ROOT/platform/3ds/patch_interp_vtx.py" "$INTERP_SRC.new" || exit 1
if [ -n "${PS_BLACKBOX:-}" ]; then python3 "$ROOT/platform/3ds/patch_interp_box.py" "$INTERP_SRC.new" || exit 1
  if [ -z "${PS_BOXLITE:-}" ]; then python3 "$ROOT/platform/3ds/crumbs.py" "$INTERP_SRC.new" "$INTERP_SRC.new" 0 "$OUT/migas/n_interp.inc" "$ROOT/platform/3ds/host" || exit 1; fi; fi
[ -n "${PS_PROF:-}" ] && python3 "$ROOT/platform/3ds/prof_interp.py" "$INTERP_SRC.new"
cmp -s "$INTERP_SRC.new" "$INTERP_SRC" && rm "$INTERP_SRC.new" || mv "$INTERP_SRC.new" "$INTERP_SRC"
if newer "$OUT/interpreter_gbi2.o" "$INTERP_SRC" $PSDEPS; then
  arm-none-eabi-g++ -DF3DEX_GBI_2 -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -mword-relocations -ffunction-sections \
    -D__3DS__ -g -DNDEBUG -std=gnu++20 -O3 -fomit-frame-pointer -fdata-sections -fno-strict-aliasing -include array -w \
    -I$OUT/psinc -I$ROOT/platform/3ds/host \
    -I$MK64_3DS/platform/3ds/include -I$MK64_3DS/platform/3ds/probe_stubs -I$MK64_3DS/third_party/SpaghettiKart/libultraship/include \
    -I$B/.dkp-generated/mk64-3ds-fast3d-shader-bin -I$MK64_3DS/platform/3ds/compat \
    -I$MK64_3DS/third_party/SpaghettiKart/libultraship/include/libultraship -isystem $DEVKITPRO/libctru/include \
    -c "$INTERP_SRC" -o "$OUT/interpreter_gbi2.o"
fi
# Shader de vertices de MK64 + camino de huesos (patch_shader.py). Mismo simbolo que el original: se enlaza antes.
mkdir -p "$OUT/shader"
python3 "$ROOT/platform/3ds/patch_shader.py" "$MK64_3DS/platform/3ds/shaders/fast3d_passthrough.v.pica" "$OUT/shader/fast3d_passthrough.v.pica" || exit 1
picasso -o "$OUT/shader/fast3d_passthrough.shbin" "$OUT/shader/fast3d_passthrough.v.pica" || exit 1
(cd "$OUT/shader" && bin2s -H fast3d_passthrough_shbin.h fast3d_passthrough.shbin > fast3d_passthrough_shbin.S) || exit 1
arm-none-eabi-gcc -c "$OUT/shader/fast3d_passthrough_shbin.S" -o "$OUT/shader/fast3d_passthrough_shbin.o" || exit 1
# Backend citro3d de MK64 con el arreglo de constantes del combinador (ver patch_citro3d.py)
python3 "$ROOT/platform/3ds/patch_citro3d.py" "$MK64_3DS/platform/3ds/source/gfx_citro3d.cpp" "$OUT/gfx_citro3d_stadium.cpp.new" || exit 1
python3 "$ROOT/platform/3ds/patch_atlas_backend.py" "$OUT/gfx_citro3d_stadium.cpp.new" || exit 1
if [ -n "${PS_BLACKBOX:-}" ]; then
  if [ -n "${PS_BOXLITE:-}" ]; then : > "$OUT/migas/n_interp.inc"; : > "$OUT/migas/n_back.inc"; : > "$OUT/migas/n_model.inc"; : > "$OUT/migas/n_soft.inc"; else
  python3 "$ROOT/platform/3ds/crumbs.py" "$OUT/gfx_citro3d_stadium.cpp.new" "$OUT/gfx_citro3d_stadium.cpp.new" 2000 "$OUT/migas/n_back.inc" "$ROOT/platform/3ds/host" || exit 1
  fi
  printf 'extern "C" { volatile unsigned gPsCrumb[8]; volatile unsigned gPsCrumbN; }\nstatic const struct { unsigned id; const char* name; } kN[] = {\n#include "n_interp.inc"\n#include "n_back.inc"\n#include "n_model.inc"\n#include "n_soft.inc"\n};\nextern "C" const char* PsCrumbName3DS(unsigned id) { for (unsigned i = 0; i < sizeof kN / sizeof kN[0]; i++) if (kN[i].id == id) return kN[i].name; return "?"; }\n' > "$OUT/migas/crumb_names.cpp"
  arm-none-eabi-g++ $CXXFLAGS -I"$OUT/migas" -c "$OUT/migas/crumb_names.cpp" -o "$OUT/migas/crumb_names.o" || exit 1
  BOXOBJ="$BOXOBJ $OUT/migas/crumb_names.o"
fi
cmp -s "$OUT/gfx_citro3d_stadium.cpp.new" "$OUT/gfx_citro3d_stadium.cpp" && rm "$OUT/gfx_citro3d_stadium.cpp.new" || mv "$OUT/gfx_citro3d_stadium.cpp.new" "$OUT/gfx_citro3d_stadium.cpp"
if newer "$OUT/gfx_citro3d_stadium.o" "$OUT/gfx_citro3d_stadium.cpp" "$OUT/shader/fast3d_passthrough_shbin.h" $PSDEPS; then
  arm-none-eabi-g++ -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -mword-relocations -ffunction-sections -D__3DS__ -g -DNDEBUG \
    -O3 -fomit-frame-pointer -fdata-sections -fno-strict-aliasing -w -I$OUT/shader -I$OUT/psinc -I$ROOT/platform/3ds/host -I$MK64_3DS/platform/3ds/source \
    -I$MK64_3DS/platform/3ds/include -I$MK64_3DS/third_party/SpaghettiKart/libultraship/include \
    -I$B/.dkp-generated/mk64-3ds-fast3d-shader-bin -I$MK64_3DS/platform/3ds/compat \
    -I$MK64_3DS/third_party/SpaghettiKart/libultraship/include/libultraship -isystem $DEVKITPRO/libctru/include \
    -c "$OUT/gfx_citro3d_stadium.cpp" -o "$OUT/gfx_citro3d_stadium.o" || exit 1
fi
arm-none-eabi-g++ $ARCH -specs=3dsx.specs $BOXLD -Wl,--gc-sections -Wl,--allow-multiple-definition \
  -L$DEVKITPRO/libctru/lib -L$DEVKITPRO/portlibs/3ds/lib -o "$OUT/pokestadium.elf" \
  "$B/CMakeFiles/mk64-3ds-citro3d-texture-compat.dir/platform/3ds/compat/citro3d-1.7.1/texture.o" \
  "$B/CMakeFiles/mk64-3ds-interpreter-probe.dir/platform/3ds/source/performance_trace_3ds.o" \
  "$OUT/host_3ds.o" "$OUT/gfx_convert.o" "$OUT/model_cache.o" $BOXOBJ "$OUT/soft_rdp.o" "$OUT/tramp.o" "$OUT/blob_bin.o" "$OUT/interpreter_gbi2.o" "$OUT/gfx_citro3d_stadium.o" "$OUT/shader/fast3d_passthrough_shbin.o" \
  "$B/libmk64-3ds-fast3d-interpreter.a" "$B/libmk64-3ds-platform.a" "$B/libmk64-3ds-fast3d-renderer.a" "$B/libmk64-3ds-platform.a" \
  -lcitro3d -lctru -lm 2> "$OUT/link.err" || { grep -m30 "undefined\|error" "$OUT/link.err"; exit 1; }

smdhtool --create "$TITLE" "Port nativo (decomp pret/pokestadium) $REV" "Pokethrasher" \
  "$ROOT/platform/3ds/cia/icon.png" "$OUT/pokestadium.smdh" 2>/dev/null || \
  smdhtool --create "$TITLE" "Port nativo $REV" "Pokethrasher" "$DEVKITPRO/libctru/default_icon.png" "$OUT/pokestadium.smdh"
3dsxtool "$OUT/pokestadium.elf" "$OUT/$NAME.3dsx" --smdh="$OUT/pokestadium.smdh"
ls -la "$OUT/$NAME.3dsx"
