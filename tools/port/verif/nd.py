#!/usr/bin/env python3
# nd.py <archivo.c relativo> <funcion> [-r]: diff del original contra el C compilado con IDO, sin nombres de registros
import subprocess, re, os, sys, difflib
WORK=os.environ.get('PS_VERIF','/tmp/ps_verif'); os.makedirs(WORK,exist_ok=True)
R=os.environ.get('DECOMP','/opt/src/pokestadium')
f,n=sys.argv[1],sys.argv[2]
raw='-r' in sys.argv
def dis(o):
    t=subprocess.run(['mips-linux-gnu-objdump','-d','-r','-z','--no-show-raw-insn',o],capture_output=True,text=True).stdout
    out=[];on=False;pend=None
    for l in t.splitlines():
        m=re.match(r'^[0-9a-f]+ <(\w+)>:',l)
        if m: on=(m.group(1)==n); continue
        if not on: continue
        m=re.match(r'^\s+[0-9a-f]+:\s+R_MIPS_(\w+)\s+(\S+)',l)
        if m:
            if out: out[-1]+=' {'+m.group(2)+'}'
            continue
        m=re.match(r'^\s+([0-9a-f]+):\s+(\S+)\s*(.*)$',l)
        if m:
            a,b=m.group(2),m.group(3)
            b=re.sub(r'<.*?>','',b)
            if not raw:
                b=re.sub(r'\b(at|v[01]|a[0-3]|t[0-9]|s[0-8]|fp|ra|k[01]|gp)\b','R',b)
                b=re.sub(r'\$?f\d+','F',b)
                b=re.sub(r'-?\d+\(sp\)','S(sp)',b)
                if a.startswith('b') or a in('j','jal'): b=re.sub(r'[0-9a-f]+\s*$','L',b)
            out.append(a+' '+b)
    return out
a=dis(os.path.join(R,'build',f[:-2]+'.o')); b=dis(WORK+'/'+f.replace('/','_')[:-2]+'.o')
for l in difflib.unified_diff(a,b,'original','C',n=2,lineterm=''): print(l)
