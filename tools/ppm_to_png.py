"""Convert PPM previews from firmware/test_host/render_preview to PNG (needs Pillow)."""
import sys
from PIL import Image

for path in sys.argv[1:]:
    out = path.rsplit(".", 1)[0] + ".png"
    Image.open(path).save(out)
    print("wrote", out)
