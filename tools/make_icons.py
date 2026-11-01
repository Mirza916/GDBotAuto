# Generates the app icon (purple tile, yellow cube with play arrow) for Windows + Android.
import os, sys
from PIL import Image, ImageDraw
root = sys.argv[1]
def icon(size):
    s = size * 4
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, s - 1, s - 1], radius=s // 5, fill=(76, 52, 140, 255))
    d.rounded_rectangle([s * 0.08, s * 0.08, s * 0.92, s * 0.92], radius=s // 6, outline=(139, 92, 246, 255), width=max(2, s // 40))
    m = s * 0.24
    d.rounded_rectangle([m, m, s - m, s - m], radius=s // 14, fill=(250, 204, 21, 255))
    c = s / 2
    d.polygon([(c - s * 0.09, c - s * 0.13), (c - s * 0.09, c + s * 0.13), (c + s * 0.13, c)], fill=(28, 20, 48, 255))
    return im.resize((size, size), Image.LANCZOS)
icon(256).save(os.path.join(root, "windows/resources/app.ico"), sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
dens = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}
for k, v in dens.items():
    p = os.path.join(root, f"android/app/src/main/res/mipmap-{k}")
    os.makedirs(p, exist_ok=True)
    icon(v).save(os.path.join(p, "ic_launcher.png"))
print("ok")
