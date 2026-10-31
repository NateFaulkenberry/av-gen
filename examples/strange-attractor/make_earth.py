"""Writes assets/space/earth.glb: a UV sphere (radius 1) carrying NASA's Blue Marble (public domain; Visible Earth,
'world.topo.bathy.200412.3x5400x2700.jpg', downloaded into assets/space/, gitignored, never committed).
The Earth in the Strange Attractor's lunar sky (dimensional portals, Gate B).

    python3 examples/strange-attractor/make_earth.py
"""
import json, math, os, struct

HERE = os.path.dirname(os.path.abspath(__file__))
SPACE = os.path.join(HERE, '..', '..', 'assets', 'space')
IMAGE = os.path.join(SPACE, 'blue-marble-5400x2700.jpg')


def main():
    seg_u, seg_v = 96, 48
    pos, nor, uv, idx = [], [], [], []
    for j in range(seg_v + 1):
        v = j / seg_v
        th = v * math.pi
        for i in range(seg_u + 1):
            u = i / seg_u
            ph = u * 2 * math.pi
            x, y, z = math.sin(th) * math.cos(ph), math.cos(th), -math.sin(th) * math.sin(ph)
            pos += [x, y, z]
            nor += [x, y, z]
            uv += [u, v]
    for j in range(seg_v):
        for i in range(seg_u):
            a = j * (seg_u + 1) + i
            b, c, d = a + 1, a + seg_u + 1, a + seg_u + 2
            idx += [a, c, b, b, c, d]
    img = open(IMAGE, 'rb').read()
    blobs = [struct.pack(f'<{len(pos)}f', *pos), struct.pack(f'<{len(nor)}f', *nor), struct.pack(f'<{len(uv)}f', *uv),
             struct.pack(f'<{len(idx)}I', *idx), img]
    views, off, binary = [], 0, b''
    for b_ in blobs:
        pad = (-len(b_)) % 4
        views.append({'buffer': 0, 'byteOffset': off, 'byteLength': len(b_)})
        binary += b_ + b'\0' * pad
        off += len(b_) + pad
    n = len(pos) // 3
    gl = {'asset': {'version': '2.0'}, 'scene': 0, 'scenes': [{'nodes': [0]}], 'nodes': [{'name': 'earth', 'mesh': 0}],
          'meshes': [{'name': 'earth', 'primitives': [{'attributes': {'POSITION': 0, 'NORMAL': 1, 'TEXCOORD_0': 2},
                                                       'indices': 3, 'material': 0}]}],
          'materials': [{'name': 'earth', 'pbrMetallicRoughness': {'baseColorTexture': {'index': 0}, 'metallicFactor': 0.0,
                                                                   'roughnessFactor': 0.7}}],
          'textures': [{'source': 0}], 'images': [{'bufferView': 4, 'mimeType': 'image/jpeg'}],
          'accessors': [{'bufferView': 0, 'componentType': 5126, 'count': n, 'type': 'VEC3', 'min': [-1, -1, -1], 'max': [1, 1, 1]},
                        {'bufferView': 1, 'componentType': 5126, 'count': n, 'type': 'VEC3'},
                        {'bufferView': 2, 'componentType': 5126, 'count': n, 'type': 'VEC2'},
                        {'bufferView': 3, 'componentType': 5125, 'count': len(idx), 'type': 'SCALAR'}],
          'bufferViews': views, 'buffers': [{'byteLength': len(binary)}]}
    js = json.dumps(gl).encode()
    js += b' ' * ((-len(js)) % 4)
    out = os.path.join(SPACE, 'earth.glb')
    with open(out, 'wb') as f:
        f.write(struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(binary)))
        f.write(struct.pack('<II', len(js), 0x4E4F534A) + js)
        f.write(struct.pack('<II', len(binary), 0x004E4942) + binary)
    print(out)


if __name__ == '__main__':
    main()
