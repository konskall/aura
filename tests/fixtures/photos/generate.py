"""Generate small synthetic decoder fixtures; no personal photographs."""
from pathlib import Path
from PIL import Image, ImageDraw
root = Path(__file__).resolve().parent
im = Image.new('RGB', (96, 48), '#16466a')
d = ImageDraw.Draw(im)
d.rectangle((48, 0, 95, 47), fill='#d29631')
d.text((5, 16), 'DEMO', fill='white')
im.save(root / 'DEMO-baseline.jpg', quality=95)
im.save(root / 'DEMO-progressive.jpg', quality=95, progressive=True)
exif = Image.Exif(); exif[274] = 6
im.save(root / 'DEMO-rotated.jpg', quality=95, exif=exif)
im.convert('RGBA').save(root / 'DEMO-image.png')
Image.new('RGBA', (2, 2), (255, 0, 0, 0)).save(root / 'DEMO-transparent.png')
large = Image.new('RGB', (3201, 2401), (20, 150, 90))
ImageDraw.Draw(large).text((100, 100), 'DEMO', fill='white')
large.save(root / 'DEMO-large.jpg', quality=80)
print('Synthetic JPG/PNG fixtures generated.')
