"""Fetch hash-checked open fonts and generate non-game app icons."""
from pathlib import Path
import argparse,hashlib,json,shutil,urllib.request
import subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--font-source',type=Path);a=p.parse_args()
for item in json.loads((ROOT/'tools/ui-fonts.json').read_text()):
    target=ROOT/'assets'/item['path'];target.parent.mkdir(parents=True,exist_ok=True)
    if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest()==item['sha256']:continue
    data=(a.font_source/item['path']).read_bytes() if a.font_source else urllib.request.urlopen(item['url']).read()
    if hashlib.sha256(data).hexdigest()!=item['sha256']:raise SystemExit('Font hash mismatch: '+item['path'])
    target.write_bytes(data)
subprocess.run([sys.executable,ROOT/'tools/generate_public_ui.py'],check=True)
