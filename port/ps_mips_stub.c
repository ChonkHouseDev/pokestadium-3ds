/* Mini interprete MIPS para las funciones de entrada de los fragments de DATOS.
 *
 * Cada modelo/animacion de Pokemon es un FRAGMENT cuyo unico codigo es un stub del estilo
 *     sw a1,4(sp); bnez a0,1f; move v1,zero; lui v1,%hi(datos); addiu v1,v1,%lo(datos); 1: jr ra; move v0,v1
 * que regresa un apuntador a sus datos (ya reubicado). El juego lo llama como funcion; en el port
 * se interpreta. Torch deja estas instrucciones en palabras nativas (ver FragmentToNative). */
#include "global.h"
#include "os/ps_os.h"

#define MAX_STEPS 4096

/* Las plataformas atrapan el fallo al saltar a datos del pool y llaman al interprete (trap) */
extern u8 gPool[];
int ps_is_pool_code(u32 pc) {
    return pc >= (u32)(uintptr_t)gPool && pc < (u32)(uintptr_t)gPool + (POOL_END_6MB - POOL_START);
}

s32 ps_call_fragment_func(void* fn, s32 a0, s32 a1) {
    u32 r[32] = { 0 };
    u32 stack[64] = { 0 };
    u32 pc = (u32)(uintptr_t)fn;
    u32 base = pc;
    r[4] = (u32)a0;
    r[5] = (u32)a1;
    r[29] = (u32)(uintptr_t)&stack[48];
    r[31] = 0xFFFFFFF0u; /* regreso */

    /* el encabezado del fragment empieza con "j main": seguirlo dentro del mismo fragment */
    u32 delayTarget = 0;
    int inDelay = 0;
    for (int steps = 0; steps < MAX_STEPS; steps++) {
        u32 ins = *(u32*)(uintptr_t)pc;
        u32 op = ins >> 26, rs = (ins >> 21) & 31, rt = (ins >> 16) & 31, rd = (ins >> 11) & 31;
        s32 imm = (s16)(ins & 0xFFFF);
        u32 next = pc + 4;
        int branch = 0;
        u32 target = 0;

        switch (op) {
            case 0x00: /* SPECIAL */
                switch (ins & 0x3F) {
                    case 0x00: if (rd) r[rd] = r[rt] << ((ins >> 6) & 31); break;            /* sll / nop */
                    case 0x02: if (rd) r[rd] = r[rt] >> ((ins >> 6) & 31); break;            /* srl */
                    case 0x03: if (rd) r[rd] = (u32)((s32)r[rt] >> ((ins >> 6) & 31)); break; /* sra */
                    case 0x08: branch = 1; target = r[rs]; break;                           /* jr */
                    case 0x21: case 0x2D: if (rd) r[rd] = r[rs] + r[rt]; break;              /* addu / daddu */
                    case 0x23: if (rd) r[rd] = r[rs] - r[rt]; break;                         /* subu */
                    case 0x24: if (rd) r[rd] = r[rs] & r[rt]; break;                         /* and */
                    case 0x25: if (rd) r[rd] = r[rs] | r[rt]; break;                         /* or / move */
                    case 0x2A: if (rd) r[rd] = (s32)r[rs] < (s32)r[rt]; break;               /* slt */
                    case 0x2B: if (rd) r[rd] = r[rs] < r[rt]; break;                         /* sltu */
                    default: goto unknown;
                }
                break;
            case 0x02: branch = 1; target = (base & 0xF0000000u) | ((ins & 0x3FFFFFF) << 2); /* j */
                /* destino original del N64: llevarlo al mismo fragment ya cargado */
                target = (base & ~0xFFFFFu) + (target & 0xFFFFFu);
                break;
            case 0x04: if (r[rs] == r[rt]) { branch = 1; target = next + (imm << 2); } else { branch = 2; } break; /* beq */
            case 0x05: if (r[rs] != r[rt]) { branch = 1; target = next + (imm << 2); } else { branch = 2; } break; /* bne */
            case 0x09: if (rt) r[rt] = r[rs] + imm; break;          /* addiu */
            case 0x0A: if (rt) r[rt] = (s32)r[rs] < imm; break;     /* slti */
            case 0x0B: if (rt) r[rt] = r[rs] < (u32)imm; break;     /* sltiu: los escenarios con switch (tabla de saltos), p. ej. el de Free Battle */
            case 0x0E: if (rt) r[rt] = r[rs] ^ (ins & 0xFFFF); break; /* xori */
            case 0x0C: if (rt) r[rt] = r[rs] & (ins & 0xFFFF); break; /* andi */
            case 0x0D: if (rt) r[rt] = r[rs] | (ins & 0xFFFF); break; /* ori */
            case 0x0F: if (rt) r[rt] = ins << 16; break;            /* lui */
            case 0x23: if (rt) r[rt] = *(u32*)(uintptr_t)(r[rs] + imm); break; /* lw (pila) */
            case 0x2B: *(u32*)(uintptr_t)(r[rs] + imm) = r[rt]; break;        /* sw (pila) */
            default:
            unknown:
                ps_printf("[mips] instruccion no soportada %08x en %08x\n", ins, pc);
                return 0;
        }

        if (inDelay) {
            if (delayTarget == 0xFFFFFFF0u) {
                static int sLogged;
                if (sLogged < 6 || r[2] == 0) { sLogged++; ps_printf("[mips] stub %p -> %08x (%d pasos)\n", fn, r[2], steps); }
                return (s32)r[2];
            }
            pc = delayTarget;
            inDelay = 0;
            continue;
        }
        if (branch == 1) { inDelay = 1; delayTarget = target; }
        pc = next;
    }
    ps_printf("[mips] el stub en %p no termino\n", fn);
    return 0;
}
