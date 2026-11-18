#!/usr/bin/env python3
"""dx2.py: compara el codigo original (tomado del ELF del decomp) contra el C traducido por m2c.py (compilado con IDO),
corriendo los dos en un emulador MIPS con la misma memoria inventada.
Uso: dx2.py <inicio hex> <fin hex> <entrada hex> <objeto.o> <funcion> <args: p=puntero, i=entero> [semillas]"""
import sys, os, re, struct, subprocess
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__))); import dx
from dx import fillpage, fillword, h32, STACK, RA, TEXT
from unicorn import *; from unicorn.mips_const import *
ELF=os.environ.get('PS_ELF','/opt/src/pokestadium/build/pokestadium-us.elf')
SNAP={'func_80045C78':[0,16,8,8,8,8,104,0], 'func_80048590':[0,0,26,80], 'func_80048684':[0,0,0,80]}
def nmsyms():
    d={}
    for l in subprocess.run(['mips-linux-gnu-nm',ELF],capture_output=True,text=True).stdout.splitlines():
        p=l.split()
        if len(p)==3 and p[1] in 'Tt' and not p[2].startswith('.L'): d.setdefault(int(p[0],16),p[2])
    return d
SY=nmsyms()
def elfbytes(lo,hi):
    raw=subprocess.run(['mips-linux-gnu-objdump','-s','--start-address=%#x'%lo,'--stop-address=%#x'%hi,ELF],capture_output=True,text=True).stdout
    b=bytearray()
    for l in raw.splitlines():
        m=re.match(r'^ ([0-9a-f]{8}) ((?:[0-9a-f]+ ){1,4})',l)
        if m:
            for w in m.group(2).split(): b+=bytes.fromhex(w)
    return bytes(b)
def run(kind,spec,args,seed,mode,limit=3000000):
    pages=set()
    for it in range(400):
        r,miss=run1(kind,spec,args,seed,mode,limit,pages)
        if miss is None: return r
        pages.add(miss)
    return dict(calls=[],writes={},v0=0,err='paginas',n=0)
def run1(kind,spec,args,seed,mode,limit,pages):
    uc=Uc(UC_ARCH_MIPS,UC_MODE_MIPS32|UC_MODE_BIG_ENDIAN)
    mapped={}
    def mp(page):
        page&=~0xfff
        if page in mapped: return
        d=fillpage(page,seed,mode); uc.mem_map(page,0x1000); uc.mem_write(page,d); mapped[page]=d
    textpages=set()
    if kind=='elf':
        lo,hi,entry=spec
        base=0x70000000+(lo&0x0FFFFFFF); b=elfbytes(lo,hi)
        for p in range(base&~0xfff,(base+len(b)+0xfff)&~0xfff,0x1000): mp(p); textpages.add(p)
        uc.mem_write(base,b)
        inside=lambda a: base<=a<base+len(b)
        start=0x70000000+(entry&0x0FFFFFFF)
        name=lambda a: SY.get(0x80000000|(a&0x0FFFFFFF),'ind_%08x'%a)
    else:
        path,fn=spec
        o=dx.Obj(path,fn); o.fs=-1; o.fe=o.secs[o.ti]['sh_size']
        o.load(uc) if False else None
        # carga con todo el .text "adentro"
        o.textaddr=lambda off: TEXT+off
        o.load(uc)
        for b0,d in o.img.items():
            for p in range(b0&~0xfff,(b0+len(d)+0xfff)&~0xfff,0x1000):
                mp(p)
                if b0==TEXT: textpages.add(p)
            uc.mem_write(b0,d)
            for p in range(b0&~0xfff,(b0+len(d)+0xfff)&~0xfff,0x1000): mapped[p]=bytes(uc.mem_read(p,0x1000))
        for b0,n in o.bss:
            for p in range(b0&~0xfff,(b0+n+0xfff)&~0xfff,0x1000): mp(p)
        tsz=o.secs[o.ti]['sh_size']
        inside=lambda a: TEXT<=a<TEXT+tsz
        start=TEXT+[a for a,n in o.funcs.items() if n==fn][0]
        name=lambda a: o.names.get(a,'ind_%08x'%a)
    for p in range(STACK-0x40000,STACK+0x1000,0x1000): mp(p)
    mp(RA)
    for p in pages: mp(p)
    sp=STACK-0x400
    uc.reg_write(UC_MIPS_REG_SP,sp); uc.reg_write(UC_MIPS_REG_RA,RA)
    ireg=[UC_MIPS_REG_A0,UC_MIPS_REG_A1,UC_MIPS_REG_A2,UC_MIPS_REG_A3]
    for i,a in enumerate(args):
        v=0x41000000+i*0x100000 if a=='p' else (h32('ia',i,seed)%(1 if mode==2 else 60))*(4 if a=='i' else 1) if a in 'ij' else int(a,0)
        if i<4: uc.reg_write(ireg[i],v)
        else: uc.mem_write(sp+i*4,struct.pack('>I',v))
    calls=[]; st={'n':0,'miss':None}
    def hcode(uc,addr,size,ud):
        st['n']+=1
        if inside(addr): return
        nm=name(addr)
        a=[uc.reg_read(r) for r in ireg]; spn=uc.reg_read(UC_MIPS_REG_SP)
        a+=list(struct.unpack('>4I',uc.mem_read(spn+16,16)))
        snap=[]
        for k,n in enumerate(SNAP.get(nm,[])):
            if n:
                try: snap.append(bytes(uc.mem_read(a[k],n)).hex())
                except Exception: snap.append('?')
        calls.append((nm,[('PILA' if STACK-0x100000<=x<STACK+0x10000 else x) for x in a],snap))
        k=len(calls)
        uc.reg_write(UC_MIPS_REG_V0,fillword(h32('rv',nm,k)&~3,seed,mode)); uc.reg_write(UC_MIPS_REG_V1,0)
        uc.reg_write(UC_MIPS_REG_PC,uc.reg_read(UC_MIPS_REG_RA))
    def hun(uc,acc,addr,size,val,ud):
        st['miss']=addr&~0xfff
        if (addr&~0xfff) in mapped: st['miss']=(addr+size-1)&~0xfff
        return False
    uc.hook_add(UC_HOOK_CODE,hcode)
    uc.hook_add(UC_HOOK_MEM_READ_UNMAPPED|UC_HOOK_MEM_WRITE_UNMAPPED|UC_HOOK_MEM_FETCH_UNMAPPED,hun)
    err=None
    try: uc.emu_start(start,RA,count=limit)
    except UcError as e: err=str(e)
    if st['miss'] is not None and err is not None and 'UNMAPPED' in err: return None,st['miss']
    if uc.reg_read(UC_MIPS_REG_PC)!=RA and err is None: err='limite'
    writes={}
    for p,d in mapped.items():
        if STACK-0x100000<=p<STACK+0x10000 or p==RA or p in textpages: continue
        if 0x10000000<=p<0x1A000000: continue   # datos propios del objeto
        cur=bytes(uc.mem_read(p,0x1000))
        if cur!=d:
            for k in range(0x1000):
                if cur[k]!=d[k]: writes[p+k]=cur[k]
    return dict(calls=calls,writes=writes,v0=uc.reg_read(UC_MIPS_REG_V0),err=err,n=st['n']),None
if __name__=='__main__':
    lo,hi,entry=int(sys.argv[1],16),int(sys.argv[2],16),int(sys.argv[3],16)
    obj,fn,args=sys.argv[4],sys.argv[5],sys.argv[6].split(',') if sys.argv[6]!='-' else []
    nargs=int(sys.argv[7]) if len(sys.argv)>7 else 4
    seeds=range(int(sys.argv[8])) if len(sys.argv)>8 else range(16)
    retv=len(sys.argv)>9 and sys.argv[9]=='v0'
    ok=0
    for seed in seeds:
        mode=0 if seed%8<6 else (1 if seed%8==6 else 2)
        A=run('elf',(lo,hi,entry),args,seed,mode); B=run('obj',(obj,fn),args,seed,mode)
        d=[]
        ca=[(c[0],c[1][:nargs],c[2]) for c in A['calls']]; cb=[(c[0],c[1][:nargs],c[2]) for c in B['calls']]
        if ca!=cb:
            k=0
            while k<min(len(ca),len(cb)) and ca[k]==cb[k]: k+=1
            d.append('llamadas distintas desde la %d de %d/%d: %s / %s'%(k,len(ca),len(cb),str(ca[k:k+1])[:300],str(cb[k:k+1])[:300]))
        if A['writes']!=B['writes']:
            ks=sorted(k for k in set(A['writes'])|set(B['writes']) if A['writes'].get(k)!=B['writes'].get(k))
            d.append('memoria distinta en %d bytes: %s'%(len(ks),', '.join('%08x:%s/%s'%(k,A['writes'].get(k,'--'),B['writes'].get(k,'--')) for k in ks[:6])))
        if retv and A['v0']!=B['v0']: d.append('regresa %08x / %08x'%(A['v0'],B['v0']))
        if A['err']!=B['err']: d.append('fin distinto: %s / %s'%(A['err'],B['err']))
        if d: print('semilla',seed,'n=%d/%d'%(A['n'],B['n']),' | '.join(d)[:900])
        else: ok+=1; print('semilla',seed,'igual: %d instrucciones / %d, %d llamadas, %d bytes escritos, fin %s'%(A['n'],B['n'],len(A['calls']),len(A['writes']),A['err']))
    print('iguales',ok,'de',len(list(seeds)))
