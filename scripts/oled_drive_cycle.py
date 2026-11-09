"""Cycles SSD1322 drive settings on the engine firmware (`!ENGINE oled-reg`) so the owner can compare the panel by eye.

Owner 2026-11-09: a long selection bar dims toward its right end although the frame is even (current-induced row
drop at contrast 0xFF). 70 % current evened it but was too dim; the default (full current) stays for now. Run with the
browser open: `.venv/bin/python scripts/oled_drive_cycle.py`. Ends on the defaults (the boot values in SSD1322_API.cpp).
"""
import glob, time, serial
s = serial.Serial(glob.glob('/dev/cu.usbmodem*')[0], 115200, timeout=0.05)
def reg(*a):
    s.write(('!ENGINE oled-reg ' + ' '.join(hex(x) for x in a) + '\n').encode()); s.flush(); time.sleep(0.15)
def defaults():
    reg(0xC1, 0xFF); reg(0xD1, 0x82, 0x20); reg(0xB6, 0x08); reg(0xBB, 0x1F); reg(0xBE, 0x07)
C = [("1  DEFAULT (full current)", []),
     ("2  current 80%", [(0xC1, 0xCF)]),
     ("3  current 70%", [(0xC1, 0xAF)]),
     ("4  normal drive (D1 A2)", [(0xD1, 0xA2, 0x20)]),
     ("5  longer 2nd precharge (B6 0F)", [(0xB6, 0x0F)]),
     ("6  lower VCOMH (BE 04)", [(0xBE, 0x04)])]
for rnd in range(2):
    for name, regs in C:
        defaults()
        for r in regs: reg(*r)
        print(f"\n>>> {name}", flush=True)
        for t in range(8, 0, -1): print(f"   {t}", end="\r", flush=True); time.sleep(1)
defaults(); print("\n>>> back to DEFAULT")
