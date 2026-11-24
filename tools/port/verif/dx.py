#!/usr/bin/env python3
# dx.py: ejecucion diferencial. Corre la funcion original (ensamblador) y la version en C (compilada con IDO) en un
# emulador MIPS con la misma memoria inventada, y compara las llamadas que hacen, lo que escriben y lo que regresan.
import sys, os, re, struct, hashlib, subprocess, glob
WORK=os.environ.get('PS_VERIF','/tmp/ps_verif'); os.makedirs(WORK,exist_ok=True)
from elftools.elf.elffile import ELFFile
from unicorn import *
from unicorn.mips_const import *
R=os.environ.get('DECOMP','/opt/src/pokestadium')
TEXT=0x70000000; SECB={'.data':0x10000000,'.rodata':0x14000000,'.bss':0x18000000,'.rdata':0x15000000,'.lit4':0x16000000,'.lit8':0x16800000}
SLOT=0x71000000; STACK=0x7FF00000; RA=0x7FFE0000; HEAP=0x40000000
def h32(*a): return int.from_bytes(hashlib.md5(repr(a).encode()).digest()[:4],'big')
def slot(name):
    # los datos con nombre D_8xxxxxxx conservan su distancia entre si (el original a veces apunta a D_x - 3)
    m=re.match(r'^(D|jtbl)_(8[0-9A-Fa-f]{7})$',name)
    if m: return 0x60000000+(int(m.group(2),16)&0x0FFFFFFF)
    return SLOT+(h32('s',name)%12000)*0x4000
FL=[1.0,0.5,2.0,100.0,-1.0,0.25,16.0,3.0]
def fillword(addr,seed,mode):
    h=h32(addr,seed)
    if mode==2: return 0
    if mode==1: return HEAP+((h>>8)%0x8000)*16
    r=h%100
    if r<50: return ((h>>8)%9)*4
    if r<60: return 0
    if r<75: return HEAP+((h>>8)%0x8000)*16
    if r<85: return struct.unpack('>I',struct.pack('>f',FL[(h>>8)%8]))[0]
    if r<93: return (((h>>8)%5)*4<<16)|(((h>>12)%40)*4)
    return ((h>>8)%64)
PROT={}
def protos():
    for f in glob.glob(R+'/src/**/*.[ch]',recursive=True)+glob.glob(R+'/include/**/*.h',recursive=True)+glob.glob(R+'/lib/ultralib/include/**/*.h',recursive=True):
        try: s=open(f,errors='replace').read()
        except: continue
        for m in re.finditer(r'^[A-Za-z_][\w\s\*]*?\b(\w+)\s*\(([^;{}()]*)\)\s*[;{]',s,re.M):
            n,a=m.group(1),m.group(2).strip()
            if n in('if','while','for','switch','return','sizeof'): continue
            args=[] if a in('','void') else [x.strip() for x in a.split(',')]
            PROT.setdefault(n,args)
class Obj:
    def __init__(s,path,fn):
        s.e=ELFFile(open(path,'rb')); s.fn=fn
        s.secs=list(s.e.iter_sections()); s.base={}
        for i,sec in enumerate(s.secs):
            if sec.name=='.text': s.base[i]=TEXT
            elif sec.name in SECB: s.base[i]=SECB[sec.name]
        s.sym=s.e.get_section_by_name('.symtab'); s.syms=list(s.sym.iter_symbols())
        s.ti=[i for i,x in enumerate(s.secs) if x.name=='.text'][0]
        s.funcs={}
        for y in s.syms:
            if y['st_shndx']==s.ti and y.name and (y['st_info']['type']=='STT_FUNC' or re.match(r'^(func_|[a-zA-Z])',y.name)) and not y.name.startswith('.L') and not y.name.startswith('L8'):
                s.funcs[y['st_value']]=y.name
        st=sorted(s.funcs)
        s.fs=[a for a,n in s.funcs.items() if n==fn][0]
        nx=[a for a in st if a>s.fs]; s.fe=nx[0] if nx else s.secs[s.ti]['sh_size']
        s.names={}
    def symaddr(s,y,add_is_off=None):
        # regresa (direccion base, es_seccion_text)
        shn=y['st_shndx']
        if shn=='SHN_UNDEF' or shn==0:
            a=slot(y.name); s.names[a]=min(s.names.get(a,y.name),y.name); return a
        if shn==s.ti:
            off=y['st_value']
            return ('text',off)
        if isinstance(shn,int) and shn in s.base: return s.base[shn]+y['st_value']
        a=slot(y.name); s.names[a]=min(s.names.get(a,y.name),y.name); return a
    def textaddr(s,off):
        if s.fs<off<s.fe: return TEXT+off
        if off in s.funcs:
            a=slot(s.funcs[off]); s.names[a]=min(s.names.get(a,s.funcs[off]),s.funcs[off]); return a
        return TEXT+off
    def load(s,uc):
        img={}
        for i,sec in enumerate(s.secs):
            if i in s.base and sec['sh_type']!='SHT_NOBITS': img[i]=bytearray(sec.data())
        for sec in s.secs:
            if sec['sh_type']!='SHT_REL': continue
            ti=sec['sh_info']
            if ti not in img: continue
            d=img[ti]; P0=s.base[ti]; pend=[]
            for r in sec.iter_relocations():
                off=r['r_offset']; t=r['r_info_type']; y=s.syms[r['r_info_sym']]
                w=struct.unpack_from('>I',d,off)[0]
                S=s.symaddr(y)
                def res(add,S=S):
                    if isinstance(S,tuple): return s.textaddr(S[1]+add)
                    return S+add
                if t==2: struct.pack_into('>I',d,off,res(w)&0xffffffff)
                elif t==4:
                    add=(w&0x3ffffff)<<2
                    tgt=res(add)
                    struct.pack_into('>I',d,off,(w&0xfc000000)|((tgt>>2)&0x3ffffff))
                elif t==5: pend.append((off,w,res))
                elif t==6:
                    lo=w&0xffff; slo=lo-0x10000 if lo&0x8000 else lo
                    for (ho,hw,hres) in pend:
                        v=hres(((hw&0xffff)<<16)+slo)&0xffffffff
                        struct.pack_into('>I',d,ho,(hw&0xffff0000)|(((v+0x8000)>>16)&0xffff))
                    if pend: lastadd=((pend[-1][1]&0xffff)<<16)+slo
                    else: lastadd=slo
                    pend=[]
                    v=res(lastadd)&0xffffffff
                    struct.pack_into('>I',d,off,(w&0xffff0000)|(v&0xffff))
        s.img={s.base[i]:bytes(v) for i,v in img.items()}
        s.bss=[(s.base[i],sec['sh_size']) for i,sec in enumerate(s.secs) if i in s.base and sec['sh_type']=='SHT_NOBITS']
FILL={}
def fillpage(page,seed,mode):
    k=(page,seed,mode)
    if k not in FILL:
        if STACK-0x100000<=page<STACK+0x10000: FILL[k]=bytes(0x1000)
        else: FILL[k]=b''.join(struct.pack('>I',fillword(page+i,seed,mode)) for i in range(0,0x1000,4))
    return FILL[k]
def run(path,fn,sig,seed,mode,limit=int(os.environ.get("DXLIMIT","120000"))):
    # Los ganchos de memoria de unicorn repiten una instruccion cuando saltan en un hueco de retardo: no se usan para
    # arreglar nada. Si falta una pagina se anota, se agrega y la funcion se corre otra vez desde cero.
    pages=set()
    for it in range(int(os.environ.get("DXPAGES","160"))):
        r,miss=run1(path,fn,sig,seed,mode,limit,pages)
        if miss is None: return r
        pages.add(miss)
    return dict(calls=[],writes={},v0=0,f0=0,err='demasiadas paginas',n=0,pc=0)
def run1(path,fn,sig,seed,mode,limit,pages):
    o=Obj(path,fn)
    uc=Uc(UC_ARCH_MIPS,UC_MODE_MIPS32|UC_MODE_BIG_ENDIAN)
    mapped={}
    def mp(page):
        page&=~0xfff
        if page in mapped: return
        d=fillpage(page,seed,mode)
        uc.mem_map(page,0x1000); uc.mem_write(page,d); mapped[page]=d
    o.load(uc)
    for b,d in o.img.items():
        for p in range(b&~0xfff,(b+len(d)+0xfff)&~0xfff,0x1000): mp(p)
        uc.mem_write(b,d)
        for p in range(b&~0xfff,(b+len(d)+0xfff)&~0xfff,0x1000): mapped[p]=bytes(uc.mem_read(p,0x1000))
    for b,n in o.bss:
        for p in range(b&~0xfff,(b+n+0xfff)&~0xfff,0x1000): mp(p)
    for p in range(STACK-0x40000,STACK+0x1000,0x1000): mp(p)
    mp(RA)
    for p in pages: mp(p)
    for nm,val in (('gDisplayListHead',0x50000000),):
        a=slot(nm); mp(a); uc.mem_write(a,struct.pack('>I',val)); mapped[a&~0xfff]=bytes(uc.mem_read(a&~0xfff,0x1000))
    sp=STACK-0x400
    uc.reg_write(UC_MIPS_REG_SP,sp); uc.reg_write(UC_MIPS_REG_RA,RA)
    ret,args=sig
    ireg=[UC_MIPS_REG_A0,UC_MIPS_REG_A1,UC_MIPS_REG_A2,UC_MIPS_REG_A3]
    allf=True
    for i,a in enumerate(args):
        isf=bool(re.match(r'^(f32|float)\b',a)) and '*' not in a
        isp='*' in a or '[' in a
        if isp: v=0x41000000+i*0x100000
        elif isf: v=struct.unpack('>I',struct.pack('>f',FL[h32('fa',i,seed)%8]))[0]
        elif re.match(r'^(Color_RGBA8)',a): v=h32('c',i,seed)
        else: v=(h32('ia',i,seed)%(1 if mode==2 else 60))*4
        if isf and allf and i<2: uc.reg_write(UC_MIPS_REG_F12 if i==0 else UC_MIPS_REG_F14,v)
        elif i<4: uc.reg_write(ireg[i],v)
        if i>=4: uc.mem_write(sp+i*4,struct.pack('>I',v))
        if not isf: allf=False
    calls=[]; st={'n':0,'miss':None}
    def hcode(uc,addr,size,ud):
        st['n']+=1
        if TEXT+o.fs<=addr<TEXT+o.fe: return
        nm=o.names.get(addr)
        if nm is None:
            off=addr-TEXT
            nm=o.funcs.get(off) if 0<=off<0x1000000 else None
            if nm is None: nm='ind_%08x'%addr
        a=[uc.reg_read(r) for r in ireg]; spn=uc.reg_read(UC_MIPS_REG_SP)
        try: sa=list(struct.unpack('>4I',uc.mem_read(spn+16,16)))
        except: sa=[0]*4
        fa=[uc.reg_read(UC_MIPS_REG_F12),uc.reg_read(UC_MIPS_REG_F14)]
        calls.append((nm,a+sa,fa))
        k=len(calls)
        rv=fillword(h32('rv',nm,k)&~3,seed,mode)
        uc.reg_write(UC_MIPS_REG_V0,rv); uc.reg_write(UC_MIPS_REG_V1,0)
        uc.reg_write(UC_MIPS_REG_F0,struct.unpack('>I',struct.pack('>f',FL[h32('rf',nm,k,seed)%8]))[0])
        uc.reg_write(UC_MIPS_REG_PC,uc.reg_read(UC_MIPS_REG_RA))
    def hun(uc,acc,addr,size,val,ud):
        st['miss']=addr&~0xfff
        if ((addr+size-1)&~0xfff) in mapped and (addr&~0xfff) in mapped: st['miss']=None
        elif (addr&~0xfff) in mapped: st['miss']=(addr+size-1)&~0xfff
        return False
    uc.hook_add(UC_HOOK_CODE,hcode)
    uc.hook_add(UC_HOOK_MEM_READ_UNMAPPED|UC_HOOK_MEM_WRITE_UNMAPPED|UC_HOOK_MEM_FETCH_UNMAPPED,hun)
    err=None
    try: uc.emu_start(TEXT+o.fs,RA,count=limit)
    except UcError as e: err=str(e)
    if st['miss'] is not None and err is not None and 'UNMAPPED' in err: return None,st['miss']
    if uc.reg_read(UC_MIPS_REG_PC)!=RA and err is None: err='limite'
    writes={}
    for p,d in mapped.items():
        if STACK-0x100000<=p<STACK+0x10000 or p==RA: continue
        if TEXT<=p<TEXT+0x1000000: continue
        cur=bytes(uc.mem_read(p,0x1000))
        if cur!=d:
            for k in range(0x1000):
                if cur[k]!=d[k]: writes[p+k]=cur[k]
    v0=uc.reg_read(UC_MIPS_REG_V0); f0=uc.reg_read(UC_MIPS_REG_F0)
    return dict(calls=calls,writes=writes,v0=v0,f0=f0,err=err,n=st['n'],pc=uc.reg_read(UC_MIPS_REG_PC)-TEXT-o.fs),None
def sigof(f,fn):
    s=open(os.path.join(R,f),errors='replace').read()
    m=re.search(r'^([A-Za-z_][\w\s\*]*?)\b'+fn+r'\s*\(([^{};]*)\)\s*\{',s,re.M)
    if not m: return ('s32',[])
    a=m.group(2).strip(); a=re.sub(r'\s+',' ',a)
    return (m.group(1).strip(),[] if a in('','void') else [re.sub(r'^(UNUSED|const) ','',x.strip()) for x in a.split(',')])
def nargs(nm):
    p=PROT.get(nm)
    return None if p is None else p
def cmp(A,B,sig):
    d=[]
    ca,cb=A['calls'],B['calls']
    if [c[0] for c in ca]!=[c[0] for c in cb]:
        k=0
        while k<min(len(ca),len(cb)) and ca[k][0]==cb[k][0]: k+=1
        d.append(f"llamadas distintas desde la {k}: orig {[c[0] for c in ca[k:k+4]]} C {[c[0] for c in cb[k:k+4]]} (total {len(ca)}/{len(cb)})")
    else:
        for k,(x,y) in enumerate(zip(ca,cb)):
            p=nargs(x[0])
            if p is None: n=1; fl=[]
            else: n=min(len(p),8); fl=[bool(re.match(r'^(f32|float)\b',q)) and '*' not in q and '[' not in q for q in p]
            if '...' in ' '.join(p or []): n=1
            allf=True
            for i in range(n):
                if fl and fl[i] and allf and i<2:
                    if x[2][i]!=y[2][i]: d.append(f"llamada {k} {x[0]} argf{i}: {x[2][i]:08x} / {y[2][i]:08x}")
                else:
                    if x[1][i]!=y[1][i] and not (STACK-0x100000<=x[1][i]<STACK+0x10000 and STACK-0x100000<=y[1][i]<STACK+0x10000): d.append(f"llamada {k} {x[0]} arg{i}: {x[1][i]:08x} / {y[1][i]:08x}")
                if not (fl and fl[i]): allf=False
    wa,wb=A['writes'],B['writes']
    if wa!=wb:
        ks=sorted(k for k in set(wa)|set(wb) if wa.get(k)!=wb.get(k))
        d.append(f"memoria distinta en {len(ks)} bytes: "+', '.join(f"{k:08x}:{wa.get(k,'--')}/{wb.get(k,'--')}" for k in ks[:6]))
    r=sig[0]
    if 'void' not in r or '*' in r:
        if re.match(r'^(static )?(f32|float)',r):
            if A['f0']!=B['f0']: d.append(f"regresa f0 {A['f0']:08x} / {B['f0']:08x}")
        elif A['v0']!=B['v0']: d.append(f"regresa {A['v0']:08x} / {B['v0']:08x}")
    return d
def one(row):
    s_,la,lb,n,f=row
    sig=sigof(f,n)
    oa=os.path.join(R,'build',f[:-2]+'.o'); ob=ALT.get(n,WORK+'/'+f.replace('/','_')[:-2]+'.o')
    res=[]; ok=0; inc=0
    import time; t0=time.time()
    for seed in (0,8,10,1,2,9,3,4,5,6,7,11):
        if time.time()-t0>float(os.environ.get('DXTIME','100')): break
        mode=0 if seed<8 else (1 if seed<10 else 2)
        try:
            A=run(oa,n,sig,seed,mode); B=run(ob,n,sig,seed,mode)
        except Exception as e:
            res.append(f"s{seed}: error {e}"); continue
        d=cmp(A,B,sig)
        fin=(A['err'] is None and B['err'] is None)
        if d: res.append(f"s{seed}{'' if fin else ' ('+str(A['err'])+'/'+str(B['err'])+')'} n={A['n']}/{B['n']} c={len(A['calls'])}: "+' | '.join(d[:3]))
        elif fin: ok+=1
        else: inc+=1
    out=f"== {n} {f.split('/')[-1]} ({s_}): iguales {ok}, a medias {inc}, distintas {len(res)}\n"
    for r in res[:4]: out+='    '+r[:420]+'\n'
    return out
ALT={}
if __name__=='__main__':
    import multiprocessing
    protos()
    rows=[l.split() for l in open(WORK+'/nm.txt') if l[0].isdigit()]
    args=sys.argv[1:]
    for a in list(args):
        if '=' in a: k,v=a.split('=',1); ALT[k]=v; args.remove(a); args.append(k)
    if args: rows=[r for r in rows if r[3] in args]
    with multiprocessing.Pool(2) as p:
        for o in p.imap_unordered(one,rows): sys.stdout.write(o); sys.stdout.flush()
