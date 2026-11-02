"""Make a reversible polished GLB and a Quake PBR material from its packed map.
Usage: python3 tools/modelgen/polish_robot.py input.glb output-directory
The source geometry, skeleton, texture atlas and cloth materials are preserved.
"""
import io
import json
from pathlib import Path
import struct
import sys
import numpy as np
from PIL import Image


def polish(source, destination):
    data = Path(source).read_bytes()
    magic, version, length = struct.unpack_from('<III', data)
    assert magic == 0x46546c67 and version == 2 and length == len(data)
    size, kind = struct.unpack_from('<II', data, 12)
    assert kind == 0x4e4f534a
    doc = json.loads(data[20:20 + size])
    offset = 20 + size
    size, kind = struct.unpack_from('<II', data, offset)
    assert kind == 0x004e4942
    binary = bytearray(data[offset + 8:offset + 8 + size])
    output = Path(destination)
    output.mkdir(parents=True, exist_ok=True)
    for index, material in enumerate(doc['materials']):
        pbr = material.get('pbrMetallicRoughness', {})
        texture = pbr.get('metallicRoughnessTexture')
        if texture is None:
            continue
        image = doc['images'][doc['textures'][texture['index']]['source']]
        view = doc['bufferViews'][image['bufferView']]
        start = view.get('byteOffset', 0)
        pixels = np.array(Image.open(io.BytesIO(binary[start:start + view['byteLength']])).convert('RGB'))
        metal = pixels[:, :, 2].astype(float) / 255
        rough = pixels[:, :, 1].astype(float) / 255
        # Only polish metallic regions; retain rough fabric and nonmetal joints.
        rough = rough * (1 - 0.55 * metal)
        rough = np.maximum(rough, 0.18 * metal)
        pixels[:, :, 1] = np.round(rough * 255).astype(np.uint8)
        packed = io.BytesIO()
        Image.fromarray(pixels).save(packed, format='PNG')
        binary.extend(b'\0' * (-len(binary) % 4))
        image['bufferView'] = len(doc['bufferViews'])
        image['mimeType'] = 'image/png'
        doc['bufferViews'].append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(packed.getvalue())})
        binary.extend(packed.getvalue())
        # renderergl2 PBR reads gloss from red and metallicness from green.
        game = np.zeros_like(pixels)
        game[:, :, 0] = np.round((1 - rough) * 255).astype(np.uint8)
        game[:, :, 1] = pixels[:, :, 2]
        Image.fromarray(game).resize((1024, 1024), Image.Resampling.LANCZOS).save(output / f'robot_pbr_{index}.png')
    doc['buffers'][0]['byteLength'] = len(binary)
    header = json.dumps(doc, separators=(',', ':')).encode()
    header += b' ' * (-len(header) % 4)
    binary.extend(b'\0' * (-len(binary) % 4))
    result = struct.pack('<III', magic, version, 12 + 8 + len(header) + 8 + len(binary))
    result += struct.pack('<II', len(header), 0x4e4f534a) + header
    result += struct.pack('<II', len(binary), 0x004e4942) + binary
    (output / 'robot-polished.glb').write_bytes(result)
    print(output / 'robot-polished.glb')


if __name__ == '__main__':
    polish(*sys.argv[1:3])
