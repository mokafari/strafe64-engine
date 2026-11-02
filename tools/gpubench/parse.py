import sys,re,os,statistics as st
MAP=os.environ.get('MAP','strafe64_1337')
t=open(sys.argv[1]).read()
if MAP not in t: print(sys.argv[1],'NOT IN GAME'); sys.exit()
rows=[]
for l in re.findall(r'gpu:.*',t):
    rows.append({k:float(v) for k,v in re.findall(r'([a-z]+) ([0-9.]+)',l)})
rows=rows[1:]
if not rows: print('NO GPU DATA'); sys.exit()
keys=['scene','tonemap','bloom','dof','hud','present','total']
print(f"{sys.argv[1].split('/')[-1][:-4]:22s} n={len(rows):3d} "+' '.join(f"{k}={st.median(r[k] for r in rows):6.3f}" for k in keys))
