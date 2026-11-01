"""robot_textures.py -- pulls a .glb's textures out as the game's images."""
import io, json, struct, sys
from PIL import Image

path, out = sys.argv[1:3]
with open(path, 'rb') as f:
    f.read(12); n = struct.unpack('<I', f.read(4))[0]; f.read(4); j = json.loads(f.read(n))
    n = struct.unpack('<I', f.read(4))[0]; f.read(4); bin_ = f.read(n)
for im in j['images']:
    v = j['bufferViews'][im['bufferView']]; o = v.get('byteOffset', 0)
    i = Image.open(io.BytesIO(bin_[o:o + v['byteLength']])).convert('RGB')
    print(im['name'], i.size)
    if 'basecolor' in im['name']:
        i.resize((1024, 1024)).save(out + '/robot.jpg', quality=92)
    elif 'normal' in im['name']:
        i.resize((1024, 1024)).save(out + '/robot_n.jpg', quality=92)
