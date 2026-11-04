"""Sample colour stats from the nocturne baseline shots. Usage: python3 -I sample.py"""
import json, os, sys
import numpy as np
from PIL import Image
B = os.path.dirname(os.path.abspath(__file__))
D = os.path.join(B, sys.argv[1]) if len(sys.argv)>1 else B
R = {
 "nocturne-1-portal.jpg": {"sky":(400,40,900,200),"floor_lit":(300,540,900,700),"moon":(60,290,200,380)},
 "nocturne-2-axis.jpg": {"sky":(300,80,1000,250),"floor_lit":(400,430,900,520),"black_marble":(100,548,500,595),"moon":(590,330,670,400),"pillar_dark":(20,100,180,400)},
 "nocturne-3-terrace.jpg": {"sky":(400,50,900,200),"floor_lit":(400,560,900,700),"floor_shadow":(0,520,300,600),"terrace_wall":(310,400,520,440),"pillar_lit":(1085,350,1140,420)},
 "nocturne-4-colonnade.jpg": {"sky":(100,50,500,200),"floor_lit":(300,600,900,700),"black_inlay":(150,565,250,590),"pillar":(600,250,680,450),"haze":(0,415,550,430)},
 "nocturne-5-overview.jpg": {"sky":(300,20,900,120),"moon":(100,100,210,190),"clouds":(0,250,380,330),"floor_lit":(400,560,700,700),"floor_shadow":(1140,570,1260,700),"pillar_warm":(10,340,90,520)},
}
def lum(a): return 0.2126*a[...,0]+0.7152*a[...,1]+0.0722*a[...,2]
def hsv(rgb):
    import colorsys; r,g,b=[x/255 for x in rgb]; h,s,v=colorsys.rgb_to_hsv(r,g,b); return round(h*360),round(s,2),round(v,2)
out={}
for f,regs in R.items():
    a=np.asarray(Image.open(os.path.join(D,f)).convert("RGB")).astype(float)
    L=lum(a)/255
    e={"global":{"mean_rgb":[round(x) for x in a.reshape(-1,3).mean(0)],"mean_luma":round(L.mean(),3),
      "p5":round(np.percentile(L,5),3),"p50":round(np.percentile(L,50),3),"p95":round(np.percentile(L,95),3),
      "pct_crushed(<0.03)":round(100*(L<0.03).mean(),1),"pct_clipped(any ch>=250)":round(100*(a.max(-1)>=250).mean(),1),
      "mean_sat":round(float(np.mean((a.max(-1)-a.min(-1))/np.maximum(a.max(-1),1))),3)},"regions":{}}
    for n,(x0,y0,x1,y1) in regs.items():
        p=a[y0:y1,x0:x1].reshape(-1,3); m=p.mean(0)
        e["regions"][n]={"rgb":[round(x) for x in m],"hex":"#%02x%02x%02x"%tuple(round(x) for x in m),"hsv(deg,s,v)":hsv(m),"luma":round(float(lum(m)/255),3),"clip%":round(100*float((p.max(-1)>=250).mean()),1)}
    out[f]=e
json.dump(out,open(os.path.join(D,"baseline.json"),"w"),indent=1)
for f,e in out.items():
    print(f,e["global"])
    for n,r in e["regions"].items(): print("   ",n,r["hex"],r["hsv(deg,s,v)"],"luma",r["luma"],"clip%",r["clip%"])
