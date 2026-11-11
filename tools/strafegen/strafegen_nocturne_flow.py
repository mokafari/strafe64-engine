"""Nocturne Flow: separate, sealed movement-combat layout using installed materials.

Q3MAP2=/path/to/q3map2 python strafegen_nocturne_flow.py
Outputs source, compiled VIS/light/AAS package and structural metrics.
Never writes strafe64_nocturne.pk3 or the shared texture archive.
"""
import argparse
import json
import struct
import zipfile
from pathlib import Path

from strafegen_marble import Marble
from strafegen_geom import make_skybox, validate_spawns

NAME = "strafe64_nocturne_flow"
ROOT = Path(__file__).resolve().parents[2]


class NocturneFlow(Marble):
    def build(self):
        self.bake_vis = True
        self.bake_meta = True
        self.bake_sample_size = 16
        self.bake_scale = 1.0
        self.arena_types = "ffa team tourney"
        self.sections.append(("Nocturne Flow", {"width": 3200, "length": 3600}))
        self.entities.append({"classname": "worldspawn", "message": "NOCTURNE // FLOW"})
        self.box((-1600, -1800, -96), (1600, 1800, 0), "marble_tiles")
        # A clean border stops accidental walk-offs, with no small collision trim.
        for lo, hi in (((-1600,-1800,0),(1600,-1768,96)),
                       ((-1600,1768,0),(1600,1800,96)),
                       ((-1600,-1768,0),(-1568,1768,96)),
                       ((1568,-1768,0),(1600,1768,96))):
            self.box(lo, hi, "marble_black")

        # Rotationally symmetric ends: covered spawn exits, gate landmarks and
        # broad circulation around both sides. Tall opaque masses break aim lines.
        for sign in (-1, 1):
            y = sign * 1200
            for x in (-850, 850):
                self.box((x-180,y-64,0),(x+180,y+64,320),"marble_black")
                self.neon((x-120,y-65,260),(x+120,y-64,264))
            for x in (-400, 400):
                self.box((x-48,sign*1500-48,0),(x+48,sign*1500+48,448),"marble_black")
            self.box((-448,sign*1500-48,448),(448,sign*1500+48,496),"marble_white")
            self.neon((-352,sign*1500-24,444),(352,sign*1500+24,448))
            # Offset central screens leave the middle objective open, with
            # left/right approaches and a jumpable low cover alternative.
            self.box((-160,sign*400-64,0),(160,sign*400+64,256),"marble_black")
            for x in (-480,480):
                self.box((x-96,sign*720-32,0),(x+96,sign*720+32,40),"marble_white")
            for x in (-1100,-500,500,1100):
                self.place("info_player_deathmatch",x,sign*1600,40,angle=90 if sign<0 else 270)
            self.place("item_health", -sign*360, sign*1000, 32)
            self.place("weapon_rocketlauncher", sign*1000, sign*850, 32)

        # Two side galleries: accessible stairs for bots, flat uninterrupted
        # faces for wall runs/catches, lower passages and upper flanking routes.
        for side in (-1,1):
            x = side*1176
            self.box((x-136,-640,0),(x+136,640,128),"marble_white")
            for sy in (-1,1):
                for step in range(8):
                    y = sy*(640+(7-step)*32)
                    self.box((x-136,y-16,0),(x+136,y+16,(step+1)*16),"marble_white")
            wallx=side*1456
            self.box((wallx-32,-720,0),(wallx+32,720,448),"marble_black")
            # Inner wall split by a wide cross-route; roofs provide small
            # sheltered pockets rather than one globally visible sky volume.
            inner=side*864
            for y0,y1 in ((-640,-192),(192,640)):
                self.box((inner-32,y0,0),(inner+32,y1,352),"marble_black")
                self.box((min(inner,wallx)-32,y0,352),(max(inner,wallx)+32,y1,384),"marble_white")
            self.place("item_health",x,0,160)
            self.place("info_player_deathmatch",x,0,168,angle=0 if side<0 else 180)
            self.neon((x-136,-80,128),(x+136,-76,130))

        # Three objective-ready courts. These are landmarks, not fake capture
        # triggers: conquest rules/ownership/scoring will be a separate mode.
        for label,y in (("A",-1040),("B",0),("C",1040)):
            for lo,hi in (((-176,y-176,0),(176,y-168,1)),
                          ((-176,y+168,0),(176,y+176,1)),
                          ((-176,y-168,0),(-168,y+168,1)),
                          ((168,y-168,0),(176,y+168,1))):
                self.box(lo,hi,"brass")
            self.place("target_location",0,y,64,message=f"{label} COURT",count=0)
        # Authored movement graph: entry/contact/landing are player origins.
        def route(name, entry, contact, end, wallkick=False, speed=180, airjump=False):
            self.place("info_bot_route", *entry, message=name,
                       contact=" ".join(map(str,contact)), exit=" ".join(map(str,end)),
                       spawnflags=int(wallkick) | (2 if airjump else 0), speed=speed)
        for side in (-1,1):
            for sy in (-1,1):
                route(f"gallery_{side}_{sy}", (side*1176,sy*728,120),
                      (side*1176,sy*656,184), (side*1176,sy*620,152))
                route(f"wallkick_{side}_{sy}", (side*700,sy*350,24),
                      (side*815,sy*350,80), (side*660,sy*560,24), True)
                route(f"cover_{side}_{sy}", (side*480,sy*860,24),
                      (side*480,sy*720,88), (side*480,sy*600,24), speed=260, airjump=True)
        self.place("weapon_railgun",0,0,32)
        self.place("info_player_intermission",0,-1500,600,angles="20 90 0")
        hull=make_skybox(-1664,-1864,-128,1664,1864,960)
        for brush in hull:
            for face in brush.faces:
                if face.tex.endswith('/sky'):
                    face.tex='textures/strafe64/sky_ether_night'
        self.solids += hull
        return self


def write_route_graph(course, out):
    """Portable top-down graph over the actual authored brush footprints."""
    import html
    routes=[e for e in course.entities if e['classname']=='info_bot_route']
    (out/'routes.json').write_text(json.dumps(routes,indent=2),encoding='utf-8')
    svg=['<svg xmlns="http://www.w3.org/2000/svg" viewBox="-1700 -1900 3400 3800">',
         '<defs><marker id="arrow" markerWidth="7" markerHeight="7" refX="6" refY="3" orient="auto"><path d="M0,0 L6,3 L0,6" fill="#5de5ff"/></marker></defs>',
         '<rect x="-1700" y="-1900" width="3400" height="3800" fill="#101723"/>']
    for b in course.solids:
        if b.mins[2]<0 or b.maxs[2]>600: continue
        x,y,_=b.mins; X,Y,_=b.maxs
        svg.append(f'<rect x="{x}" y="{-Y}" width="{X-x}" height="{Y-y}" fill="#374457" stroke="#748198" stroke-width="2"/>')
    for i,e in enumerate(routes,1):
        pts=[list(map(float,e[k].split())) for k in ('origin','contact','exit')]
        coords=' '.join(f'{p[0]},{-p[1]}' for p in pts)
        svg.append(f'<g><title>{html.escape(e["message"])}: {"wall kick" if int(e["spawnflags"]) & 1 else "air jump" if int(e["spawnflags"]) & 2 else "jump"}</title><polyline points="{coords}" fill="none" stroke="#5de5ff" stroke-width="7" marker-end="url(#arrow)"/>')
        for x,y,z in pts: svg.append(f'<circle cx="{x}" cy="{-y}" r="11" fill="#5de5ff"/>')
        x,y,_=pts[0]
        svg.append(f'<text x="{x+20}" y="{-y-20}" font-size="40" fill="white">{i:02}</text></g>')
    svg.append('</svg>')
    (out/'routes.svg').write_text(''.join(svg),encoding='utf-8')


def bsp_metrics(data):
    assert data[:4] == b'IBSP' and struct.unpack_from('<i',data,4)[0] == 46
    lumps=[struct.unpack_from('<2i',data,8+i*8) for i in range(17)]
    for offset,size in lumps:
        assert offset >= 0 and offset+size <= len(data)
    offset,size=lumps[16]
    result=dict(bytes=len(data), surfaces=lumps[13][1]//104,
                brushes=lumps[8][1]//12, lightmap_bytes=lumps[14][1], vis_bytes=size)
    if size>=8:
        clusters,row=struct.unpack_from('<2i',data,offset)
        assert clusters>0 and row>0 and size==8+clusters*row
        bits=data[offset+8:offset+size]
        visible=sum(bool(bits[i*row+j//8] & (1<<(j%8)))
                    for i in range(clusters) for j in range(clusters))
        result.update(clusters=clusters, mean_visible_cluster_fraction=visible/(clusters*clusters))
    return result


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--out',type=Path,default=ROOT/'tools/strafegen/generated/nocturne_flow')
    args=parser.parse_args()
    import strafegen_config as cfg
    from strafegen_bsp import write_map
    from strafegen_bake import build_baked_pk3
    cfg.THEME='ethereal'
    course=NocturneFlow(64,night=True).build()
    validate_spawns(course)
    args.out.mkdir(parents=True,exist_ok=True)
    course.bake_logdir=str(args.out)
    write_map(course,str(args.out/(NAME+'.map')),baked=True,
              extra_world={'_ambient':course.bake_ambient[0],'_color':course.bake_ambient[1]})
    write_route_graph(course,args.out)
    stats=build_baked_pk3(NAME,course,str(args.out),bounce=1)
    if not stats['aas']:
        raise RuntimeError('Bot navigation is required; package is not ready')
    with zipfile.ZipFile(stats['pk3']) as pack:
        stats['bsp']=bsp_metrics(pack.read(f'maps/{NAME}.bsp'))
    classic=ROOT/'assets/openarena/baseoa/strafe64_nocturne.pk3'
    with zipfile.ZipFile(classic) as pack:
        stats['classic_bsp']=bsp_metrics(pack.read('maps/strafe64_nocturne.bsp'))
    (args.out/'metrics.json').write_text(json.dumps(stats,indent=2))
    print(json.dumps(stats,indent=2))


if __name__=='__main__':
    main()
