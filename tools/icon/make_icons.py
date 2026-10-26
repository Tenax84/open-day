#!/usr/bin/env python3
"""Derives the program icons from the 1024x1024 master (assets/icon/openday.png).

Writes next to the master:
  openday.ico      Windows (exe resource, installer), 16-256 px
  openday.icns     macOS app bundle, 16-1024 px
  openday-256.png  window icon on Linux (embedded in the program)

Needs Pillow. Run again after changing the master:
  python tools/icon/make_icons.py [assets/icon/openday.png]
"""
import os
import sys

from PIL import Image


def main():
    master_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(__file__), '..', '..', 'assets', 'icon', 'openday.png')
    out_dir = os.path.dirname(os.path.abspath(master_path))
    master = Image.open(master_path).convert('RGBA')

    def scaled(n):
        # Premultiplied while resampling, so the transparent surround does
        # not darken the edge.
        return master.convert('RGBa').resize((n, n), Image.LANCZOS).convert('RGBA')

    ico_sizes = [16, 24, 32, 48, 64, 128, 256]
    ico = [scaled(n) for n in ico_sizes]
    ico[-1].save(os.path.join(out_dir, 'openday.ico'), sizes=[(n, n) for n in ico_sizes],
                 append_images=ico[:-1])
    master.save(os.path.join(out_dir, 'openday.icns'))
    ico[-1].save(os.path.join(out_dir, 'openday-256.png'), optimize=True)


if __name__ == '__main__':
    main()
