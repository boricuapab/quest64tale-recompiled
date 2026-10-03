"""Generate original app icons from geometric primitives; no game imagery."""
from pathlib import Path
from PIL import Image,ImageDraw
import argparse
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1]);a=p.parse_args()
dest=a.root/'icons';dest.mkdir(exist_ok=True)
img=Image.new('RGBA',(512,512),(22,31,49,255));d=ImageDraw.Draw(img)
d.rounded_rectangle((62,62,450,450),radius=75,outline=(230,187,85,255),width=24)
d.ellipse((136,117,376,357),outline=(245,236,216,255),width=35)
d.line((275,297,379,403),fill=(245,236,216,255),width=35)
img.save(dest/'512.png')
for size in (16,32,64,128,256,512):img.save(dest/f'{size}.ico',format='ICO',sizes=[(size,size)])
(dest/'app.rc').write_text('\n'.join(f'{i+1} ICON DISCARDABLE "{size}.ico"' for i,size in enumerate((512,256,128,64,32,16)))+'\n')
print('Generated app icons')
