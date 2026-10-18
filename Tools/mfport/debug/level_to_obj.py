# Combine a converted map (work/out/Maps/<map>) into one OBJ for inspection.
# Usage: python3 level_to_obj.py work/out/Maps/mf-warehouse out/level.obj /abs/path/work/out/Textures
import json, struct, math, sys
from pathlib import Path
root=Path(sys.argv[1]); out=Path(sys.argv[2]); texdir=sys.argv[3]
lvl=json.loads((root/'level.json').read_text())
def load(name):
    m=json.loads((root/(name+'.mfmesh.json')).read_text())
    b=(root/m['bin']).parent/m['bin'] if False else (root/name).parent/m['bin']
    data=b.read_bytes(); n=m['vertex_count']
    v=[struct.unpack_from('<8f',data,i*32) for i in range(n)]
    idx=struct.unpack_from(f"<{m['index_count']}I",data,n*32)
    return m,v,idx
L=['mtllib level.mtl']; mats={}; vo=1
def rot(p,r):
    # UE rotator: yaw about Z, pitch about Y, roll about X  (approx for debug)
    y,pi,ro=[math.radians(r[k]) for k in('yaw','pitch','roll')]
    x,yy,z=p
    # roll (X)
    yy,z=yy*math.cos(ro)-z*math.sin(ro), yy*math.sin(ro)+z*math.cos(ro)
    # pitch (Y)
    x,z=x*math.cos(pi)-z*math.sin(pi), x*math.sin(pi)+z*math.cos(pi)
    # yaw (Z)
    x,yy=x*math.cos(y)-yy*math.sin(y), x*math.sin(y)+yy*math.cos(y)
    return (x,yy,z)
def emit(name,loc,r=None,s=1.0):
    global vo
    m,v,idx=load(name)
    for sec in m['sections']:
        key=m['materials'][sec['material']]; mats.setdefault(key,len(mats))
        L.append(f'usemtl m{mats[key]}')
        for t in range(sec['num_triangles']):
            for k in range(3):
                vv=v[idx[sec['first_index']+t*3+k]]
                p=(vv[0]*s,vv[1]*s,vv[2]*s)
                if r: p=rot(p,r)
                # OBJ: write UE coords as x, z, -y (Y-up right-handed for Blender import with default axes)
                L.append(f'v {(p[0]+loc[0])/100:.4f} {(p[2]+loc[2])/100:.4f} {(p[1]+loc[1])/100:.4f}')
                L.append(f'vt {vv[6]:.5f} {1-vv[7]:.5f}')
            L.append(f'f {vo}/{vo} {vo+2}/{vo+2} {vo+1}/{vo+1}'); vo+=3
for w in lvl['world']:
    if w['render']: emit(w['mesh'],w['location'])
for a in lvl['actors']:
    if a.get('static_mesh') and a['static_mesh'] in lvl['static_meshes']:
        s=float(a['props'].get('DrawScale','1'))
        emit(lvl['static_meshes'][a['static_mesh']]['mesh'],a['location'],a['rotation'],s)
out.write_text('\n'.join(L))
M=[]
for key,i in mats.items():
    tex=key.split('|')[0]
    M.append(f'newmtl m{i}\nKd 1 1 1\nmap_Kd {texdir}/{"/".join(tex.split("."))}.png')
(out.parent/'level.mtl').write_text('\n'.join(M))
print(len(mats),'materials', vo//3,'tris')
