#!/usr/bin/env python3
# Compila con IDO cada archivo que tiene funciones NON_MATCHING y compara cada una contra el ensamblador original.
import subprocess, re, os, sys, difflib
WORK=os.environ.get('PS_VERIF','/tmp/ps_verif'); os.makedirs(WORK,exist_ok=True)
R=os.environ.get('DECOMP','/opt/src/pokestadium')
files=subprocess.run("grep -rl 'ifdef NON_MATCHING' src --include=*.c",shell=True,cwd=R,capture_output=True,text=True).stdout.split()
def dis(o):
    t=subprocess.run(['mips-linux-gnu-objdump','-d','-r','-z','--no-show-raw-insn',o],capture_output=True,text=True).stdout
    fn={};cur=None
    for l in t.splitlines():
        m=re.match(r'^[0-9a-f]+ <(\w+)>:',l)
        if m: cur=m.group(1); fn[cur]=[]; continue
        m=re.match(r'^\s+[0-9a-f]+:\s+(\S+)\s*(.*)$',l)
        if m and cur: fn[cur].append((m.group(1),m.group(2)))
    return fn
out=[]
for f in sorted(files):
    src=open(os.path.join(R,f),errors='replace').read()
    names=[]
    for m in re.finditer(r'#ifdef NON_MATCHING(.*?)#else(.*?)#endif',src,re.S):
        names+=re.findall(r'GLOBAL_ASM\("[^"]*/(\w+)\.s"\)',m.group(2))
    cmd=subprocess.run(f"make -n -W {f} NON_MATCHING=1 build/{f[:-2]}.o",shell=True,cwd=R,capture_output=True,text=True).stdout
    line=[l for l in cmd.splitlines() if 'tools/ido' in l]
    if not line: print('sin comando',f); continue
    o=WORK+'/'+f.replace('/','_')[:-2]+'.o'
    c=line[0].replace(f"-o build/{f[:-2]}.o",f"-o {o}")
    r=subprocess.run(c,shell=True,cwd=R,capture_output=True,text=True)
    if not os.path.exists(o): print('no compilo',f,r.stderr[-300:]); continue
    a=dis(os.path.join(R,'build',f[:-2]+'.o')); b=dis(o)
    for n in names:
        if n not in a or n not in b: out.append((0,f,n,-1,-1)); continue
        ma=[x[0] for x in a[n]]; mb=[x[0] for x in b[n]]
        s=difflib.SequenceMatcher(None,ma,mb,autojunk=False).ratio()
        out.append((s,f,n,len(ma),len(mb)))
for s,f,n,la,lb in sorted(out):
    print(f"{s:.3f} {la:5d} {lb:5d} {n} {f}")
