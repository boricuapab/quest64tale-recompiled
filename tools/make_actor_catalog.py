import json, html
from pathlib import Path
from verify_glb import preview
from PIL import Image
root=Path(__file__).resolve().parents[1]
report=json.loads((root/'extracted-assets/actors-extraction-report.json').read_text())
cards=[]
for model in report['models']:
    path=Path(model['path']);rel=path.relative_to(root/'extracted-assets').as_posix()
    thumb=path.parent/'preview.png'
    if not thumb.exists():preview(path,thumb)
    im=Image.open(thumb);im.thumbnail((240,320));im.save(thumb)
    cards.append(f'<article data-name="{model["name"]}"><img loading="lazy" src="{html.escape(str(thumb.relative_to(root/"extracted-assets").as_posix()))}"><h3>{model["name"]}</h3><p>{model["animations"]} animations · {model["textures"]} textures</p><a href="{rel}">GLB</a> · <a href="{rel.rsplit("/",1)[0]}/textures/manifest.json">Texture manifest</a></article>')
page='''<!doctype html><meta charset="utf-8"><title>Quest 64 Actor Exports</title><style>body{font:16px system-ui;background:#171b23;color:#eee;margin:24px}input{padding:12px;width:350px}main{display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:20px}article{background:#252b36;padding:12px;border-radius:10px}img{width:100%;height:320px;object-fit:contain}h3{font-size:14px;overflow-wrap:anywhere}a{color:#9bccff}</style><h1>Quest 64 characters, enemies and bosses</h1><p>GLBs contain textures, UVs, vertex colors and all animations found for that model. PNGs are in the adjacent textures folder. Unnamed actors use stable ROM-bank IDs.</p><input placeholder="Search: boss name or bank ID" oninput="document.querySelectorAll('article').forEach(x=>x.hidden=!x.dataset.name.includes(this.value.toLowerCase()))"><main>'''+''.join(cards)+'</main>'
(root/'extracted-assets/actor-catalog.html').write_text(page,encoding='utf-8')
print('Catalog written',len(cards),'models')
