#!/usr/bin/env python3
# cc.py w0 w1: decodifica un G_SETCOMBINE
import sys
def dec(w0,w1):
    a0=(w0>>20)&0xF; c0=(w0>>15)&0x1F; Aa0=(w0>>12)&7; Ac0=(w0>>9)&7; a1=(w0>>5)&0xF; c1=w0&0x1F
    b0=(w1>>28)&0xF; b1=(w1>>24)&0xF; Aa1=(w1>>21)&7; Ac1=(w1>>18)&7; d0=(w1>>15)&7; Ab0=(w1>>12)&7; Ad0=(w1>>9)&7; d1=(w1>>6)&7; Ab1=(w1>>3)&7; Ad1=w1&7
    A=['COMB','TEX0','TEX1','PRIM','SHADE','ENV','1','NOISE']+['0']*8
    B=['COMB','TEX0','TEX1','PRIM','SHADE','ENV','CENTER','K4']+['0']*8
    C=['COMB','TEX0','TEX1','PRIM','SHADE','ENV','SCALE','COMB_A','TEX0_A','TEX1_A','PRIM_A','SHADE_A','ENV_A','LODF','PRIMLODF','K5']+['0']*16
    D=['COMB','TEX0','TEX1','PRIM','SHADE','ENV','1','0']
    AA=['COMB','TEX0','TEX1','PRIM','SHADE','ENV','1','0']
    AC=['LODF','TEX0','TEX1','PRIM','SHADE','ENV','PRIMLODF','0']
    return ('c0: (%s-%s)*%s+%s | a0: (%s-%s)*%s+%s || c1: (%s-%s)*%s+%s | a1: (%s-%s)*%s+%s'%(A[a0],B[b0],C[c0],D[d0],AA[Aa0],AA[Ab0],AC[Ac0],AA[Ad0],A[a1],B[b1],C[c1],D[d1],AA[Aa1],AA[Ab1],AC[Ac1],AA[Ad1]))
if __name__=='__main__':
    for i in range(1,len(sys.argv),2): print(sys.argv[i],sys.argv[i+1],dec(int(sys.argv[i],16),int(sys.argv[i+1],16)))
