import json,io
from pathlib import Path
from PIL import Image
from verify_glb import load
root=Path(__file__).resolve().parents[1]
report=json.loads((root/'extracted-assets/actors-extraction-report.json').read_text())
assert not report['failed_models'] and not report['rejected_descriptors']
counts={'models':0,'animations':0,'textures':0,'triangles':0}
for model in report['models']:
    path=Path(model['path']);doc,data,acc=load(path)
    assert len(doc['animations'])==model['animations']
    manifest=json.loads((path.parent/'textures/manifest.json').read_text())
    for img,external in zip(doc['images'],manifest):
        view=doc['bufferViews'][img['bufferView']];raw=data[view['byteOffset']:view['byteOffset']+view['byteLength']]
        assert raw==(path.parent/'textures'/external['file']).read_bytes()
        Image.open(io.BytesIO(raw)).verify()
    for mesh in doc['meshes']:
        for primitive in mesh['primitives']:
            mat=doc['materials'][primitive['material']]
            if 'baseColorTexture' in mat['pbrMetallicRoughness']:assert 'TEXCOORD_0' in primitive['attributes']
            counts['triangles']+=len(acc(primitive['attributes']['POSITION']))//3
    counts['models']+=1;counts['animations']+=len(doc['animations']);counts['textures']+=len(manifest)
(root/'extracted-assets/actor-validation.json').write_text(json.dumps(counts,indent=2))
print('PASS',counts)
