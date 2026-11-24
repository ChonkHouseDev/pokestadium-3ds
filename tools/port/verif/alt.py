#!/usr/bin/env python3
# alt.py <archivo.c relativo> <funcion>: compila con IDO la version del port de ese archivo (con sus arreglos) dentro
# del arbol del decomp y la compara contra el original con dx.py.
import subprocess, sys, os, re, shutil
WORK=os.environ.get('PS_VERIF','/tmp/ps_verif'); os.makedirs(WORK,exist_ok=True)
R=os.environ.get('DECOMP','/opt/src/pokestadium'); P=os.path.abspath(os.path.join(os.path.dirname(__file__),'..','..','..'))
f=sys.argv[1]; fns=[a for a in sys.argv[2:] if not a.startswith('+')]; hdrs=[a[1:] for a in sys.argv[2:] if a.startswith('+')]
for h in hdrs: shutil.copy(os.path.join(R,h),os.path.join(R,h)+'.zzbak'); shutil.copy(os.path.join(P,h),os.path.join(R,h))
tmp=f[:-2]+'_zzfix.c'
src=open(os.path.join(P,f),errors='replace').read()
open(os.path.join(R,tmp),'w').write(src)
cmd=subprocess.run(f"make -n -W {f} NON_MATCHING=1 build/{f[:-2]}.o",shell=True,cwd=R,capture_output=True,text=True).stdout
line=[l for l in cmd.splitlines() if 'tools/ido' in l][0]
o=WORK+'/fix_'+f.replace('/','_')[:-2]+'.o'
if os.path.exists(o): os.remove(o)
c=line.replace(f"-o build/{f[:-2]}.o",f"-o {o}").replace(f" {f}"," "+tmp).replace("-DNON_MATCHING","-DNON_MATCHING -DPORT")
r=subprocess.run(c,shell=True,cwd=R,capture_output=True,text=True)
os.remove(os.path.join(R,tmp))
for h in hdrs: shutil.move(os.path.join(R,h)+'.zzbak',os.path.join(R,h))
if not os.path.exists(o): print('no compilo:',r.stderr[-1500:], r.stdout[-500:]); sys.exit(1)
subprocess.run(['python3',os.path.join(os.path.dirname(os.path.abspath(__file__)),'dx.py')]+[f"{n}={o}" for n in fns])
