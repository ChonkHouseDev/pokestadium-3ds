#!/usr/bin/env python3
"""Shader de vertices de MK64 + camino de huesos de Stadium (skinning en el GPU).

Con skinEnabled: el vertice trae posicion de objeto (xyz) y en w el numero de hueso * 4. La matriz
(modelview del hueso * proyeccion, por columnas) sale de skinBones[] con el registro de direccion a0, y la
profundidad PICA (z - w) / 2 se calcula aqui. El resto (proyeccion de la pantalla, texturas, color) sigue
igual que el camino normal.

Uso: patch_shader.py entrada.v.pica salida.v.pica
"""
import sys

src = open(sys.argv[1]).read()


def rep(old, new):
    global src
    if old not in src:
        sys.exit("patch_shader: no encontre: " + old[:70])
    src = src.replace(old, new, 1)


rep(".bool originalFogEnabled\n", ".bool originalFogEnabled\n.bool skinEnabled\n.bool skinLit\n.bool skinFog\n.bool skinFogAlpha\n.bool residentEnabled\n")
rep(".fvec nativeLightDirection, nativeLightDiffuse, nativeLightAmbient\n",
    ".fvec nativeLightDirection, nativeLightDiffuse, nativeLightAmbient\n.fvec skinBones[66], skinLightColor[3], skinFogParams\n")
rep(".constf lightingConstants(128.0, 0.0078740157480315, 255.0, 0.0)\n",
    ".constf lightingConstants(128.0, 0.0078740157480315, 255.0, 0.0)\n.constf skinConstants(0.5, 0.00390625, 0.0, 0.0)\n")
rep("""    mov r4, inpos
""", """    mov r4, inpos
    mov r14, incolor
    ifu residentEnabled
        ; F2: el vertice trae la normal del N64 en bytes con signo; la luz espera normal + 128
        sge r15, r14, lightingConstants.xxxx
        mul r15, lightingConstants.xxxx, r15
        add r14, lightingConstants.xxxx, r14
        add r14, -r15, r14
        add r14, -r15, r14
    .end
""")
rep("""    dp4 r0.x, projection[0], r4""", """    ifu skinEnabled
        ; Stadium: posicion de objeto, hueso * 4 en w
        mova a0.x, inpos.w
        mov r5.xyz, inpos
        mov r5.w, shaderConstants.x
        dp4 r4.x, skinBones[a0.x], r5
        dp4 r4.y, skinBones[a0.x+1], r5
        dp4 r4.z, skinBones[a0.x+2], r5
        dp4 r4.w, skinBones[a0.x+3], r5
        add r8.x, r4.z, -r4.w
        mul r8.x, skinConstants.x, r8.x
        ifu skinFog
            ; niebla del N64 por vertice: z/w * mul + offset, entre 0 y 255 (como GfxSpVertex)
            rcp r13.x, r4.w
            mul r13.x, r4.z, r13.x
            mul r13.x, skinFogParams.x, r13.x
            add r13.x, skinFogParams.y, r13.x
            max r13.x, lightingConstants.w, r13.x
            min r13.x, lightingConstants.z, r13.x
            ; coordenada de la rampa de niebla original: (niebla + 0.5) / 256
            add r13.y, skinConstants.x, r13.x
            mul r13.y, skinConstants.y, r13.y
        .end
        ifu skinLit
            ; luz direccional del N64: la normal (+128) viene en el color; la direccion de la luz en el espacio
            ; del hueso es la quinta fila del hueso. color = ambiente + max(0, n.L / 127) * difusa, piso, tope 255
            add r10.xyz, -lightingConstants.xxx, r14.xyz
            mov r10.w, lightingConstants.w
            dp3 r11.x, skinBones[a0.x+4], r10
            dp3 r11.y, skinBones[a0.x+5], r10
            mul r11.xy, lightingConstants.yy, r11.xy
            max r11.xy, lightingConstants.ww, r11.xy
            ; como el N64: ambiente + luz 1, truncado; + luz 2, truncado (la 2a fila es cero con una luz)
            mul r12.xyz, skinLightColor[0].xyz, r11.xxx
            add r12.xyz, skinLightColor[2].xyz, r12.xyz
            flr r12.xyz, r12.xyz
            mul r10.xyz, skinLightColor[1].xyz, r11.yyy
            add r10.xyz, r12.xyz, r10.xyz
            flr r10.xyz, r10.xyz
            min r10.xyz, lightingConstants.zzz, r10.xyz
        .end
    .end
    ifu residentEnabled
        ; F2: U,V del N64 ya escalados por G_TEXTURE (horneados); por textura: U*k + o, recorte del atlas, *m + a
        ; (staticUv[0] = k, staticUv[1] = o, nativeMatrix[0..2] = recorte, m, a; las dos texturas en xy / zw)
        mul r9, staticUv[0], intex0.xyxy
        add r9, staticUv[1], r9
        min r9, nativeMatrix[0], r9
        mul r9, nativeMatrix[1], r9
        add r9, nativeMatrix[2], r9
    .end
    dp4 r0.x, projection[0], r4""")
rep("""    ifu nativeLightingEnabled""", """    ifu skinLit
        mov r2.xyz, r10.xyz
    .end
    ifu skinFogAlpha
        flr r2.w, r13.x
    .end
    ifu nativeLightingEnabled""")
rep("""    ifu originalFogEnabled
        mov r1.x, intex1.x
    .end""", """    ifu originalFogEnabled
        mov r1.x, intex1.x
    .end
    ifu residentEnabled
        ifu originalFogEnabled
            ; coordenada de la niebla original sin niebla por vertice: alfa del vertice o constante (skinFogParams.zw)
            mul r1.x, skinFogParams.z, incolor.w
            add r1.x, skinFogParams.w, r1.x
        .end
    .end
    ifu skinFog
        ifu originalFogEnabled
            mov r1.x, r13.y
        .end
    .end""")
# Segunda textura: el ensamblador rellena un swizzle corto repitiendo su ultima letra (".zw" es ".zwww") y el
# PICA toma cada componente por posicion, asi que "min r6.zw, textureClamp, r9.zw" dejaba U = V en la segunda
# textura (se veia como rayas horizontales en los efectos que mezclan dos texturas, p. ej. Sleep Powder).
rep("    min r6.zw, textureClamp, r9.zw\n", "    min r6.zw, textureClamp, r9.zwzw\n")
rep("        add r9.zw, staticUv[1].zwzw, r9.zw\n", "        add r9.zw, staticUv[1].zwzw, r9.zwzw\n")
open(sys.argv[2], "w").write(src)
print("patch_shader: ok")
