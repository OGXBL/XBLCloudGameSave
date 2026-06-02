#!/usr/bin/env python3
"""Convert assets/ogxbl_logo.png (or argv[1]) into src/ogxbl_logo.h for the nxdk app."""
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("pip install pillow", file=sys.stderr)
    sys.exit(1)

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PNG = ROOT / "assets" / "ogxbl_logo.png"
OUT_H = ROOT / "src" / "ogxbl_logo.h"
SIZE = 128


def main():
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_PNG
    if not src.is_file():
        print(f"Missing {src}", file=sys.stderr)
        sys.exit(1)
    im = Image.open(src).convert("RGBA").resize((SIZE, SIZE), Image.Resampling.LANCZOS)
    px = list(im.getdata())
    lines = [
        "#ifndef OGXBL_LOGO_DATA_H",
        "#define OGXBL_LOGO_DATA_H",
        "#include <stdint.h>",
        f"#define OGXBL_LOGO_W {SIZE}",
        f"#define OGXBL_LOGO_IMG_H {SIZE}",
        "static const uint32_t ogxbl_logo_rgba[OGXBL_LOGO_W * OGXBL_LOGO_IMG_H] = {",
    ]
    for i in range(0, len(px), 8):
        chunk = px[i : i + 8]
        vals = ", ".join(
            "0x%08X" % ((a << 24) | (r << 16) | (g << 8) | b) for r, g, b, a in chunk
        )
        lines.append("    " + vals + ",")
    lines.append("};")
    lines.append("#endif")
    OUT_H.write_text("\n".join(lines) + "\n")
    nz = sum(1 for r, g, b, a in px if a > 8)
    print(f"Wrote {OUT_H} ({nz} non-transparent pixels)")


if __name__ == "__main__":
    main()
