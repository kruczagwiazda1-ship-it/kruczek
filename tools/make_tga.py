#!/usr/bin/env python3
"""Convert assets/*.png into the TGA files a Wii U title expects in meta/ (uncompressed)."""
import sys, os
from PIL import Image
here = os.path.dirname(os.path.abspath(__file__)); assets = os.path.join(here, '..', 'assets')
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, '..', 'build', 'tga')
os.makedirs(out, exist_ok=True)
def save(src, name, size, mode):
    im = Image.open(os.path.join(assets, src)).convert('RGBA').resize(size, Image.LANCZOS)
    if mode == 'RGB': im = im.convert('RGB')
    im.save(os.path.join(out, name), format='TGA')   # uncompressed
save('icon.png',  'iconTex.tga',    (128, 128),  'RGBA')   # 32-bit
save('tv.png',    'bootTvTex.tga',  (1280, 720), 'RGB')    # 24-bit
save('drc.png',   'bootDrcTex.tga', (854, 480),  'RGB')    # 24-bit
logo = Image.new('RGBA', (170, 42), (0, 0, 0, 0)); logo.save(os.path.join(out, 'bootLogoTex.tga'), format='TGA')
print('wrote TGA files to', out)
