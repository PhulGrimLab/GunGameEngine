"""glb에 내장된 첫 번째 이미지를 파일로 꺼낸다(엔진은 재질/텍스처를 가져오지 않으므로 씬 노드에 텍스처를 따로 지정해야 한다).

사용: python -I tools/extract_glb_texture.py <입력.glb> <출력 경로(확장자 없이)>
  -> 이미지의 mimeType에 따라 .png 또는 .jpg를 붙여 쓰고 경로와 크기를 출력한다.
예:   python -I tools/extract_glb_texture.py assets/external/Fox/Fox.glb assets/external/Fox/Fox_baseColor
assets/external의 *_baseColor.* 파일은 이 스크립트로 원본 glb에서 꺼낸 것이다(assets/external/README.md 참고).
"""
import json
import struct
import sys

if len(sys.argv) != 3:
    sys.exit(__doc__)
path, out = sys.argv[1], sys.argv[2]
data = open(path, 'rb').read()
offset, doc, binary = 12, None, None
while offset < len(data):
    length, kind = struct.unpack_from('<II', data, offset)
    body = data[offset + 8: offset + 8 + length]
    if kind == 0x4E4F534A:    # JSON
        doc = json.loads(body)
    elif kind == 0x004E4942:  # BIN
        binary = body
    offset += 8 + length
if doc is None or binary is None or not doc.get('images'):
    sys.exit(f'{path}: no embedded image found')
image = doc['images'][0]
if 'bufferView' not in image:
    sys.exit(f'{path}: the image is not embedded in the glb (uri={image.get("uri")})')
view = doc['bufferViews'][image['bufferView']]
start = view.get('byteOffset', 0)
extension = {'image/png': '.png', 'image/jpeg': '.jpg'}.get(image.get('mimeType'))
if extension is None:
    sys.exit(f'{path}: unsupported image type {image.get("mimeType")}')
open(out + extension, 'wb').write(binary[start:start + view['byteLength']])
print(out + extension, view['byteLength'], 'bytes', image['mimeType'])
