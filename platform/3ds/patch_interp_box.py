#!/usr/bin/env python3
# Caja negra (PS_BLACKBOX=1): el interprete deja anotado el comando de la lista de dibujo que esta ejecutando, para que
# la caja lo muestre si el cuadro se queda atorado dentro del interprete.
import sys
p = sys.argv[1]
s = open(p).read()
a = "        gfx_step();\n"
assert s.count(a) == 1, "no encontre el ciclo de comandos"
s = s.replace(a, """        { const uint32_t psK = gPsBoxCmdN & 3;
          gPsBoxCmd[psK][0] = (uint32_t)(uintptr_t)cmd; gPsBoxCmd[psK][1] = (uint32_t)cmd->words.w0; gPsBoxCmd[psK][2] = (uint32_t)cmd->words.w1;
          gPsBoxCmdN = gPsBoxCmdN + 1; }
""" + a)
i = s.index("#include")
s = s[:i] + '#include <cstdint>\nextern "C" { volatile uint32_t gPsBoxCmd[4][3]; volatile uint32_t gPsBoxCmdN; }\n' + s[i:]
open(p, "w").write(s)
