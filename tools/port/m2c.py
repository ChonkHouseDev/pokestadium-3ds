#!/usr/bin/env python3
"""m2c.py: traduce a C, instruccion por instruccion, un tramo de codigo MIPS del juego original (del ELF del decomp).

Para las funciones de sonido que el decomp dejo en ensamblador (el decodificador de las voces). El C generado guarda los
registros en variables y lee/escribe memoria igual que el original (el port es big-endian y de 32 bits, como el N64).
Uso: m2c.py <nombre> <inicio hex> <fin hex> <entrada=nombre[:nargs]>... [--dato dir=expr] > salida.c
"""
import sys, re, subprocess, struct
import os
ELF=os.environ.get('PS_ELF','/opt/src/pokestadium/build/pokestadium-us.elf')
name=sys.argv[1]; lo=int(sys.argv[2],16); hi=int(sys.argv[3],16)
entries=[]; over={}; datos=[]
for a in sys.argv[4:]:
    if a.startswith('--datos='):
        x,y=a[8:].split(':'); datos.append((int(x,16),int(y,16)))
    elif a.startswith('--sobre='):
        k,v=a[8:].split('=',1); over[int(k,16)]=v
    else:
        k,v=a.split('='); entries.append((int(k,16),v))
dis=subprocess.run(['mips-linux-gnu-objdump','-d','-z','-M','no-aliases','--no-show-raw-insn','--start-address=%#x'%lo,'--stop-address=%#x'%hi,ELF],capture_output=True,text=True).stdout
raw=subprocess.run(['mips-linux-gnu-objdump','-s','--start-address=%#x'%lo,'--stop-address=%#x'%hi,ELF],capture_output=True,text=True).stdout
data=bytearray()
for l in raw.splitlines():
    m=re.match(r'^ ([0-9a-f]{8}) ((?:[0-9a-f]+ ){1,4})',l)
    if m:
        for w in m.group(2).split(): data+=bytes.fromhex(w)
syms={}
todos={}
for l in subprocess.run(['mips-linux-gnu-nm',ELF],capture_output=True,text=True).stdout.splitlines():
    p=l.split()
    if len(p)==3 and not p[2].startswith('.L') and p[1] in 'TtW':
        todos.setdefault(int(p[0],16),[]).append(p[2])
for k,ns in todos.items():   # varios nombres para la misma direccion: bzero, no _blkclr
    pref=[n for n in ns if n in('bzero','bcopy','memcpy')] or sorted(n for n in ns if not n.startswith('_')) or sorted(ns)
    syms[k]=pref[0]
ins=[]
for l in dis.splitlines():
    m=re.match(r'^([0-9a-f]{8}):\s+(\S+)\s*(.*)$',l)
    if m:
        ops=re.sub(r'\s*<.*?>','',m.group(3)).strip()
        ad=int(m.group(1),16)
        if any(x<=ad<y for x,y in datos): ins.append((ad,'nop',[]))
        else: ins.append((ad,m.group(2),[o.strip() for o in ops.split(',')] if ops else []))
A={a:i for i,(a,_,_) in enumerate(ins)}
GPR='zero at v0 v1 a0 a1 a2 a3 t0 t1 t2 t3 t4 t5 t6 t7 s0 s1 s2 s3 s4 s5 s6 s7 t8 t9 k0 k1 gp sp s8 ra'.split()
def R(r):
    assert r in GPR, r
    return 'r_'+r
def W(r,expr):
    if r=='zero': return '(void)(%s);'%expr
    return '%s = %s;'%(R(r),expr)
def imm(s): return int(s,0)
def mem(op):
    m=re.match(r'^(-?\w+)\((\w+)\)$',op); return int(m.group(1),0),m.group(2)
def F(r): return 'f_'+r[2:]
labels=set(a for a,_ in entries); indirect=set(a for a,_ in entries)
BR={'beq':'%s == %s','bne':'%s != %s','beql':'%s == %s','bnel':'%s != %s'}
BZ={'blez':'(s32)%s <= 0','bgtz':'(s32)%s > 0','bltz':'(s32)%s < 0','bgez':'(s32)%s >= 0','blezl':'(s32)%s <= 0','bgtzl':'(s32)%s > 0','bltzl':'(s32)%s < 0','bgezl':'(s32)%s >= 0'}
for a,m,o in ins:
    if m in BR or m in BZ or m in('bc1f','bc1t','bc1fl','bc1tl','j','bgezal','bltzal'):
        t=int(o[-1],16)
        if lo<=t<hi and not (m=='bltzal' and o[0]=='zero'): labels.add(t)
    if m in('jalr','jal','bgezal','bltzal'): indirect.add(a+8); labels.add(a+8)
for k,(a,m,o) in enumerate(ins):
    if m=='bltzal' and o[0]=='zero':
        for a2,m2,o2 in ins[k+1:k+4]:
            if m2=='addiu' and o2[1]=='ra':
                val=a+8+imm(o2[2])
                if any(x<=val<y for x,y in datos): over[a2]='r_%s = PS_TEXTO(%#xu);'%(o2[0],val)
                else: indirect.add(val); labels.add(val)
def stmt(a,m,o):
    """traduce una instruccion que no es salto"""
    if a in over: return over[a]
    if m=='nop': return ';'
    if m in('addiu','addi'): return W(o[0],'%s + %d'%(R(o[1]),imm(o[2])))
    if m in('addu','add'): return W(o[0],'%s + %s'%(R(o[1]),R(o[2])))
    if m in('subu','sub'): return W(o[0],'%s - %s'%(R(o[1]),R(o[2])))
    if m=='and': return W(o[0],'%s & %s'%(R(o[1]),R(o[2])))
    if m=='or': return W(o[0],'%s | %s'%(R(o[1]),R(o[2])))
    if m=='xor': return W(o[0],'%s ^ %s'%(R(o[1]),R(o[2])))
    if m=='nor': return W(o[0],'~(%s | %s)'%(R(o[1]),R(o[2])))
    if m=='andi': return W(o[0],'%s & %#xu'%(R(o[1]),imm(o[2])))
    if m=='ori': return W(o[0],'%s | %#xu'%(R(o[1]),imm(o[2])))
    if m=='xori': return W(o[0],'%s ^ %#xu'%(R(o[1]),imm(o[2])))
    if m in('negu','neg'): return W(o[0],'0u - %s'%R(o[1]))
    if m in('move',): return W(o[0],R(o[1]))
    if m in('dli','li'): return W(o[0],'(u32)%d'%imm(o[1]))
    if m=='lui': return W(o[0],'%#xu'%((imm(o[1])&0xffff)<<16))
    if m=='slt': return W(o[0],'((s32)%s < (s32)%s)'%(R(o[1]),R(o[2])))
    if m=='sltu': return W(o[0],'(%s < %s)'%(R(o[1]),R(o[2])))
    if m=='slti': return W(o[0],'((s32)%s < %d)'%(R(o[1]),imm(o[2])))
    if m=='sltiu': return W(o[0],'(%s < (u32)%d)'%(R(o[1]),imm(o[2])))
    if m=='sll': return W(o[0],'%s << %d'%(R(o[1]),imm(o[2])))
    if m=='srl': return W(o[0],'%s >> %d'%(R(o[1]),imm(o[2])))
    if m=='sra': return W(o[0],'(u32)((s32)%s >> %d)'%(R(o[1]),imm(o[2])))
    if m=='sllv': return W(o[0],'%s << (%s & 31)'%(R(o[1]),R(o[2])))
    if m=='srlv': return W(o[0],'%s >> (%s & 31)'%(R(o[1]),R(o[2])))
    if m=='srav': return W(o[0],'(u32)((s32)%s >> (%s & 31))'%(R(o[1]),R(o[2])))
    if m=='mult': return 'PS_MULT(%s, %s);'%(R(o[0]),R(o[1]))
    if m=='multu': return 'PS_MULTU(%s, %s);'%(R(o[0]),R(o[1]))
    if m=='div': return 'if (%s) { r_lo = (u32)((s32)%s / (s32)%s); r_hi = (u32)((s32)%s %% (s32)%s); }'%(R(o[2]),R(o[1]),R(o[2]),R(o[1]),R(o[2]))
    if m=='divu': return 'if (%s) { r_lo = %s / %s; r_hi = %s %% %s; }'%(R(o[2]),R(o[1]),R(o[2]),R(o[1]),R(o[2]))
    if m=='mfhi': return W(o[0],'r_hi')
    if m=='mflo': return W(o[0],'r_lo')
    if m in('lw','lh','lhu','lb','lbu'):
        off,b=mem(o[1]); t={'lw':'s32','lh':'s16','lhu':'u16','lb':'s8','lbu':'u8'}[m]
        return W(o[0],'(u32)(s32)*(%s*)(%s + %d)'%(t,R(b),off))
    if m in('sw','sh','sb'):
        off,b=mem(o[1]); t={'sw':'u32','sh':'u16','sb':'u8'}[m]
        return '*(%s*)(%s + %d) = (%s)%s;'%(t,R(b),off,t,R(o[0]))
    if m=='lwc1': off,b=mem(o[1]); return '%s.u = *(u32*)(%s + %d);'%(F(o[0]),R(b),off)
    if m=='swc1': off,b=mem(o[1]); return '*(u32*)(%s + %d) = %s.u;'%(R(b),off,F(o[0]))
    if m=='mtc1': return '%s.u = %s;'%(F(o[1]),R(o[0]))
    if m=='mfc1': return W(o[0],'%s.u'%F(o[1]))
    if m in('add.s','sub.s','mul.s','div.s'): return '%s.f = %s.f %s %s.f;'%(F(o[0]),F(o[1]),{'add.s':'+','sub.s':'-','mul.s':'*','div.s':'/'}[m],F(o[2]))
    if m=='cvt.s.w': return '%s.f = (f32)(s32)%s.u;'%(F(o[0]),F(o[1]))
    if m=='trunc.w.s': return '%s.u = (u32)(s32)%s.f;'%(F(o[0]),F(o[1]))
    if m=='mov.s': return '%s.u = %s.u;'%(F(o[0]),F(o[1]))
    if m=='neg.s': return '%s.f = -%s.f;'%(F(o[0]),F(o[1]))
    if m in('c.lt.s','c.le.s','c.eq.s'): return 'fcc = (%s.f %s %s.f);'%(F(o[0]),{'c.lt.s':'<','c.le.s':'<=','c.eq.s':'=='}[m],F(o[1]))
    raise SystemExit('sin traducir: %x %s %s'%(a,m,o))
out=[]
def L(a): return 'L_%08X'%a
def jump(t,a):
    if lo<=t<hi: return 'goto %s;'%L(t)
    raise SystemExit('salto fuera del tramo: %x -> %x'%(a,t))
i=0
while i<len(ins):
    a,m,o=ins[i]
    if a in labels: out.append('%s:'%L(a))
    isbr=m in BR or m in BZ or m in('bc1f','bc1t','bc1fl','bc1tl','j','jr','jal','jalr','bgezal','bltzal')
    if not isbr:
        out.append('    '+stmt(a,m,o)+'  /* %x %s %s */'%(a,m,','.join(o))); i+=1; continue
    da,dm,do=ins[i+1]
    assert da not in labels or (m in('jal','jalr','bgezal','bltzal')), 'etiqueta en hueco de retardo %x'%da
    d=stmt(da,dm,do)
    cm='  /* %x %s %s ; %s %s */'%(a,m,','.join(o),dm,','.join(do))
    if m in BR or m in BZ:
        cond=(BR[m]%(R(o[0]),R(o[1]))) if m in BR else (BZ[m]%R(o[0]))
        t=int(o[-1],16)
        if m.endswith('l'): out.append('    if (%s) { %s %s }%s'%(cond,d,jump(t,a),cm))
        elif m=='beq' and o[0]=='zero' and o[1]=='zero': out.append('    %s %s%s'%(d,jump(t,a),cm))
        else: out.append('    br = (%s); %s if (br) %s%s'%(cond,d,jump(t,a),cm))
    elif m in('bc1f','bc1t','bc1fl','bc1tl'):
        cond='fcc' if 't' in m[3:] else '!fcc'; t=int(o[-1],16)
        if m.endswith('l'): out.append('    if (%s) { %s %s }%s'%(cond,d,jump(t,a),cm))
        else: out.append('    br = (%s); %s if (br) %s%s'%(cond,d,jump(t,a),cm))
    elif m=='j': out.append('    %s %s%s'%(d,jump(int(o[0],16),a),cm))
    elif m=='jr':
        if o[0]=='ra' and len(entries)==1 and not any(x[1] in('jalr','bgezal') or (x[1]=='bltzal') for x in ins):
            out.append('    %s return r_v0;%s'%(d,cm))
        else: out.append('    pc = %s; %s goto despacho;%s'%(R(o[0]),d,cm))
    elif m=='jal':
        t=int(o[0],16)
        if lo<=t<hi: out.append('    r_ra = %#xu; %s goto %s;%s'%(a+8,d,L(t),cm)); labels.add(t)
        else:
            fn=syms[t]
            out.append('    %s r_v0 = ((u32 (*)(u32, u32, u32, u32, u32, u32, u32, u32))%s)(r_a0, r_a1, r_a2, r_a3, *(u32*)(r_sp + 16), *(u32*)(r_sp + 20), *(u32*)(r_sp + 24), *(u32*)(r_sp + 28));%s'%(d,fn,cm))
            out.append('EXT:'+fn)
    elif m=='jalr':
        rs=o[-1]; out.append('    pc = %s; r_ra = %#xu; %s goto despacho;%s'%(R(rs),a+8,d,cm))
    elif m=='bgezal':   # bal: llamada local
        assert o[0]=='zero'; t=int(o[-1],16)
        out.append('    r_ra = %#xu; %s goto %s;%s'%(a+8,d,L(t),cm))
    elif m=='bltzal':   # nunca salta: solo deja en ra la direccion siguiente (para leer datos metidos en el codigo)
        assert o[0]=='zero'
        out.append('    r_ra = %#xu; %s%s'%(a+8,d,cm))
    i+=2
ext=sorted(set(x[4:] for x in out if x.startswith('EXT:')))
body=[x for x in out if not x.startswith('EXT:')]
need_dispatch=any('goto despacho' in x for x in body)
print('/* Generado por tools/port/m2c.py a partir del codigo original (%#x-%#x). No editar a mano. */'%(lo,hi))
print('typedef unsigned int u32; typedef int s32; typedef unsigned short u16; typedef short s16; typedef unsigned char u8; typedef signed char s8; typedef float f32;')
print('typedef union { f32 f; u32 u; } PsF;')
for e in ext: print('extern u32 %s();'%e)
print('''#define PS_MULTU(a, b) do { u32 x_ = (a), y_ = (b), xl = x_ & 0xFFFF, xh = x_ >> 16, yl = y_ & 0xFFFF, yh = y_ >> 16; \\
    u32 ll = xl * yl, lh_ = xl * yh, hl = xh * yl, hh = xh * yh, mid = (ll >> 16) + (lh_ & 0xFFFF) + (hl & 0xFFFF); \\
    r_lo = x_ * y_; r_hi = hh + (lh_ >> 16) + (hl >> 16) + (mid >> 16); } while (0)
#define PS_MULT(a, b) do { u32 sa_ = (a), sb_ = (b); PS_MULTU(sa_, sb_); if ((s32)sa_ < 0) r_hi -= sb_; if ((s32)sb_ < 0) r_hi -= sa_; } while (0)''')
print('static const u8 %s_texto[] = {%s};'%(name,','.join(str(b) for b in data)))
print('#define PS_TEXTO(dir) ((u32)%s_texto + ((dir) - %#xu))'%(name,lo))
frame=max([-imm(o[2]) for a,m,o in ins if m=='addiu' and o[0]=='sp' and o[1]=='sp' and imm(o[2])<0]+[0])
print('static u32 %s_corre(u32 pc, u32 r_a0, u32 r_a1, u32 r_a2, u32 r_a3) {'%name)
print('    u32 r_at = 0, r_v0 = 0, r_v1 = 0, r_t0 = 0, r_t1 = 0, r_t2 = 0, r_t3 = 0, r_t4 = 0, r_t5 = 0, r_t6 = 0, r_t7 = 0, r_t8 = 0, r_t9 = 0;')
print('    u32 r_s0 = 0, r_s1 = 0, r_s2 = 0, r_s3 = 0, r_s4 = 0, r_s5 = 0, r_s6 = 0, r_s7 = 0, r_s8 = 0, r_k0 = 0, r_k1 = 0, r_gp = 0, r_ra = 0xDEAD0000u, r_hi = 0, r_lo = 0, r_sp, br = 0;')
print('    const u32 r_zero = 0; int fcc = 0;')
print('    PsF '+', '.join('f_%d'%k for k in range(32))+';')
print('    u32 pila[%d];'%(frame//4+48))
print('    r_sp = (u32)&pila[%d];'%(frame//4+16))
print('    '+' '.join('f_%d.u = 0;'%k for k in range(32)))
print('despacho:')
print('    switch (pc) {')
for a in sorted(indirect):
    if a in A or a==hi: 
        if a in labels and a in A: print('        case %#xu: goto %s;'%(a,L(a)))
print('        default: return r_v0;')
print('    }')
for x in body: print(x)
print('    return r_v0;')
print('}')
for a,n in entries:
    print('u32 %s(u32 a0, u32 a1, u32 a2, u32 a3) { return %s_corre(%#xu, a0, a1, a2, a3); }'%(n,name,a))
