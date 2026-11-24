#!/usr/bin/env python3
"""Reusar el material entre comandos de triangulos mientras el estado del RDP/RSP no cambie.

El interprete solo reusaba el material dentro de una racha de G_TRI seguidos; cada G_VTX la cortaba, asi que
el estadio resolvia el material completo (~8 us) por triangulo. Ahora un contador de estado sube con cualquier
comando que no sea de vertices, matrices, triangulos o control de flujo; si no subio, el material sigue valido.

Uso: patch_interp_mat.py interprete.cpp   (modifica en el lugar)
"""
import sys

p = sys.argv[1]
s = open(p).read()


def rep(old, new):
    global s
    if old not in s:
        sys.exit("patch_interp_mat: no encontre: " + old[:80])
    s = s.replace(old, new, 1)


rep("static TriangleMaterial3DS Mk64ResolveTriangleMaterial3DS(Interpreter* gfx, bool is_rect) {",
    "static TriangleMaterial3DS Mk64ResolveTriangleMaterial3DS(Interpreter* gfx, bool is_rect) {\n"
    "    static uint32_t savedStateGen3DS = 0;")
rep("""    TriangleMaterial3DS material3DS;
    if (materialHit3DS) {""", """    TriangleMaterial3DS material3DS;
    const bool stateHit3DS = !is_rect && savedStateGen3DS == sPsStateGen3DS && materialOwner3DS == gfx;
    gPsMatDbg3DS[is_rect ? 2 : (materialHit3DS || stateHit3DS) ? 0 : 1]++;
    if (materialHit3DS || stateHit3DS) {""")
rep("""        if (reusable3DS && !imported3DS) {
            savedMaterial3DS = material3DS;
            materialGeneration3DS = sTriangleRun3DS.generation;
            materialOwner3DS = gfx;
        } else {
            materialGeneration3DS = 0;
        }""", """        if (!is_rect && !imported3DS) {
            savedMaterial3DS = material3DS;
            materialGeneration3DS = reusable3DS ? sTriangleRun3DS.generation : 0;
            savedStateGen3DS = sPsStateGen3DS;
            materialOwner3DS = gfx;
        } else {
            materialGeneration3DS = 0;
            savedStateGen3DS = 0;
        }""")

# cada comando que puede cambiar estado sube el contador
rep("""    if (Mk64StaticRunStep3DS(cmd, opcode, runUcode3DS)) return;""",
    """    sPsCurOp3DS = (uint8_t)opcode;
    switch ((uint8_t)opcode) {
        case 0x00: case 0x01: case 0x05: case 0x06: case 0x07: // NOOP VTX TRI1 TRI2 QUAD
        case 0xDA: case 0xD8: case 0xDE: case 0xDF: case 0xE0: // MTX POPMTX DL ENDDL SPNOOP
        case 0xE6: case 0xE7: case 0xE8: case 0xE9:             // syncs
            break;
        // comandos idempotentes: repetir el mismo valor que el ultimo de su tipo no cambia nada
        case 0xD7: case 0xD9: case 0xE2: case 0xE3: case 0xF0: case 0xF2: case 0xF3: case 0xF4:
        case 0xF5: case 0xF7: case 0xF8: case 0xF9: case 0xFA: case 0xFB: case 0xFC: case 0xFD: case 0xED:
            PsStateCommand3DS((uint8_t)opcode, cmd->words.w0, cmd->words.w1);
            break;
        default:
            PsStateChanged3DS();
            sPsStateEpoch3DS++;
            break;
    }
    if (Mk64StaticRunStep3DS(cmd, opcode, runUcode3DS)) return;""")
rep("void Interpreter::StartFrame() {", "void Interpreter::StartFrame() {\n    PsStateChanged3DS();\n    sPsStateEpoch3DS++;")
# texturas de framebuffer y vaciados de cache cambian lo que esta puesto
rep("void Interpreter::TextureCacheClear() {",
    "static uint32_t sPsStateGen3DS = 1;\n"
    "extern \"C\" { uint32_t gPsMatDbg3DS[4]; uint32_t gPsBumpOp3DS[256]; }\n"
    "static uint8_t sPsCurOp3DS = 0;\n"
    "static void PsStateChanged3DS() { gPsBumpOp3DS[sPsCurOp3DS]++; if (++sPsStateGen3DS == 0) sPsStateGen3DS = 1; }\n"
    "static uint32_t sPsStateEpoch3DS = 1;\n"
    "static uint32_t sPsLastW3DS[256][8][3];\n"
    "static void PsStateCommand3DS(uint8_t op, uint32_t w0, uint32_t w1) {\n"
    "    const uint32_t tile = (op == 0xF5 || op == 0xF2 || op == 0xF3 || op == 0xF4 || op == 0xF0) ? (w1 >> 24) & 7 : 0;\n"
    "    uint32_t* e = sPsLastW3DS[op][tile];\n"
    "    if (e[2] == sPsStateEpoch3DS && e[0] == w0 && e[1] == w1) return;\n"
    "    e[0] = w0; e[1] = w1; e[2] = sPsStateEpoch3DS;\n"
    "    PsStateChanged3DS();\n"
    "}\n"
    "void Interpreter::TextureCacheClear() {\n    PsStateChanged3DS();")
# el skin deja el backend con su propio estado: lo que sigue resuelve otra vez
rep("""        sPsSkinGpu3DS.active = false; // el lote sigue abierto: lo cierra el siguiente Flush()
        // el interprete queda""", """        sPsSkinGpu3DS.active = false; // el lote sigue abierto: lo cierra el siguiente Flush()
        PsStateChanged3DS();
        sPsStateEpoch3DS++; // el skin cambio el modo de geometria por dentro
        // el interprete queda""")
# el camino retenido tambien cambia el estado
rep("""                sPsSkinGpu3DS.active = false;
                return false;""", """                sPsSkinGpu3DS.active = false;
                PsStateChanged3DS();
                sPsStateEpoch3DS++;
                return false;""")
open(p, "w").write(s)
print("patch_interp_mat: ok")
