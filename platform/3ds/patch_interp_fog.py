#!/usr/bin/env python3
"""Tinte parejo con el color de niebla (G_RM_FOG_PRIM_A).

El N64 tiene dos modos de niebla: G_RM_FOG_SHADE_A mezcla hacia el color de niebla segun el alfa de cada vertice
(la niebla de distancia), y G_RM_FOG_PRIM_A segun el ALFA del registro de niebla, igual para todo el modelo.
Stadium usa el segundo para pintar al Pokemon cuando le cae un estado (azul al dormirse, por ejemplo). El
interprete trataba los dos como el primero: con el alfa del vertice en 255 el Pokemon salia de un solo color.

El interprete ya tenia un modo con factor constante (use_blend_color: mezcla hacia el color de BLEND con el alfa
de niebla). Este modo es el mismo con el color de niebla, asi que se monta sobre el.

Uso: patch_interp_fog.py interprete.cpp   (modifica en el lugar; va despues de patch_interp_skin.py)
"""
import sys

p = sys.argv[1]
s = open(p).read()


def rep(old, new):
    global s
    if s.count(old) != 1:
        sys.exit("patch_interp_fog: esperaba una sola vez: " + old[:80])
    s = s.replace(old, new, 1)


rep("""    bool use_blend_color = blend_src == G_BL_CLR_BL;
    bool use_fog = blend_src == G_BL_CLR_FOG || use_blend_color;""",
    """    const bool fog_reg_alpha3DS = blend_src == G_BL_CLR_FOG && ((gfx->mRdp->other_mode_l >> 26) & 3) == G_BL_A_FOG;
    bool use_blend_color = blend_src == G_BL_CLR_BL || fog_reg_alpha3DS;
    bool use_fog = blend_src == G_BL_CLR_FOG || use_blend_color;""")
# triangulos por el CPU
rep("""                mBufVbo[mBufVboLen++] = mRdp->blend_color.r / 255.0f;
                mBufVbo[mBufVboLen++] = mRdp->blend_color.g / 255.0f;
                mBufVbo[mBufVboLen++] = mRdp->blend_color.b / 255.0f;
                mBufVbo[mBufVboLen++] = mRdp->fog_color.a / 255.0f;""",
    """                const auto& fc3DS = (mRdp->other_mode_l >> 30) == G_BL_CLR_BL ? mRdp->blend_color : mRdp->fog_color;
                mBufVbo[mBufVboLen++] = fc3DS.r / 255.0f;
                mBufVbo[mBufVboLen++] = fc3DS.g / 255.0f;
                mBufVbo[mBufVboLen++] = fc3DS.b / 255.0f;
                mBufVbo[mBufVboLen++] = mRdp->fog_color.a / 255.0f;""")
# modelos con huesos (y de ahi los residentes en el GPU)
rep("""        if (m.use_blend_color) {
            fogC[0] = rdp->blend_color.r / 255.0f; fogC[1] = rdp->blend_color.g / 255.0f; fogC[2] = rdp->blend_color.b / 255.0f;
            fogC[3] = rdp->fog_color.a / 255.0f;""",
    """        if (m.use_blend_color) {
            const auto& fc = (rdp->other_mode_l >> 30) == G_BL_CLR_BL ? rdp->blend_color : rdp->fog_color;
            fogC[0] = fc.r / 255.0f; fogC[1] = fc.g / 255.0f; fogC[2] = fc.b / 255.0f;
            fogC[3] = rdp->fog_color.a / 255.0f;""")
open(p, "w").write(s)
print("patch_interp_fog: ok")
