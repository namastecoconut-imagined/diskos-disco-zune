#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Zune theme fonts: Open Sans (SIL OFL 1.1) cut to fixed weights, converted with lv_font_conv 1.5.3.
Output: ui/font_zune_<weight>_<px>.c, LVGL name font_zune_<weight>_<px>.
Usage: python3 ui/tools/gen_zune_fonts.py --src ../fonts/OpenSans-VF.ttf"""
import argparse, subprocess
from pathlib import Path
UI = Path(__file__).resolve().parent.parent
TEXT = '0x20-0x7E,0xA0-0xFF,0x2018-0x201D,0x2026,0x2022,0x203A'
DIGITS = '0x20,0x25,0x2D,0x2E,0x30-0x3A,0xB0'          # space % - . 0-9 : °
WORDMARK = '0x63,0x64,0x69,0x73'                       # "disc" (the boot splash)
GHOST = '0x20,0x26,0x27,0x2D,0x2E,0x30-0x39,0x61-0x7A,0xDF-0xFF'   # lowercase only: the faint artist name behind Now Playing
CUTS = {  # weight name -> (wght, [(px, range)])
    'light':    (300, [(18, TEXT), (21, TEXT), (24, TEXT), (32, TEXT), (46, TEXT), (50, TEXT), (28, TEXT), (40, TEXT), (72, DIGITS), (88, DIGITS), (96, DIGITS), (150, DIGITS), (64, WORDMARK), (80, GHOST)]),
    'regular':  (400, [(12, TEXT), (13, TEXT), (14, TEXT), (15, TEXT), (16, TEXT), (17, TEXT), (19, TEXT)]),
    'semibold': (600, [(10, TEXT), (11, TEXT), (12, TEXT), (22, TEXT), (24, TEXT)]),
}
def main():
    p = argparse.ArgumentParser(); p.add_argument('--src', type=Path, required=True)
    p.add_argument('--converter', type=Path, default=UI.parent / 'node_modules/lv_font_conv/lv_font_conv.js'); a = p.parse_args()
    from fontTools.ttLib import TTFont; from fontTools.varLib import instancer
    tmp = UI.parent / '.fontcache'; tmp.mkdir(exist_ok=True)
    for name, (w, sizes) in CUTS.items():
        ttf = tmp / f'OpenSans-{name}.ttf'
        if not ttf.exists(): instancer.instantiateVariableFont(TTFont(a.src), {'wght': w, 'wdth': 100}).save(ttf)
        for px, rng in sizes:
            fn = f'font_zune_{name}_{px}'
            subprocess.run(['node', str(a.converter), '--font', str(ttf), '--size', str(px), '--bpp', '4', '--format', 'lvgl',
                            '--range', rng, '--no-compress', '--force-fast-kern-format', '--lv-font-name', fn,
                            '-o', str(UI / f'{fn}.c')], check=True)
            print('made', fn)
if __name__ == '__main__': main()
