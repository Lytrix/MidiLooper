#!/usr/bin/env python3
"""Reboot the engine firmware again and again, reopening its ports each time; report every hang.

    .venv/bin/python scripts/engine_reboot_soak.py [--cycles 25] [--fast] [--no-midi] [--port /dev/cu.usbmodemXXXX]

Each cycle, as the host scripts that hung the engine on 2026-11-08/09: `!REBOOT` over USB serial, close the serial and
'Teensy MIDI' ports, wait for the board to come back, reopen serial and the MIDI ports, send a few notes on channel 1
while the board boots (thru output on DIN and USB device), then require a `!ENGINE status` reply and read the boot's
reset rows with `!ENGINE boot` (the boot's own rows are lost when the port opens after the boot printed them).
`--fast` closes the port right after `!REBOOT` and reopens after a fixed 1.5 s, without waiting for the reply.

A hang is a board that answers nothing for 30 s (it needs the uploader or a PROGRAM press; exit 1 at once), a boot
whose reset cause is the watchdog (`watchdog=1`: the firmware hung and the watchdog reset it), or one whose previous
boot ended in stage 12 (the USB device wedged and the firmware reset itself). Every
watchdog boot's `prev_crumbs` row places the hang: `phase` how far the hung boot came, `stage` the main-loop stage
when it reached the loop. Exit 1 when any cycle hung.
"""
import argparse
import glob
import re
import subprocess
import sys
import time

import serial

try:
    import mido
except ImportError:  # serial only
    mido = None

MIDI_NAME = 'Teensy MIDI'


def find_port(explicit):
    if explicit:
        return explicit if glob.glob(explicit) else None
    return next(iter(sorted(glob.glob('/dev/cu.usbmodem*'))), None)


def open_serial(explicit, deadline):
    while time.time() < deadline:
        port = find_port(explicit)
        if port is not None:
            try:
                return serial.Serial(port, 115200, timeout=0.05, write_timeout=2)
            except (serial.SerialException, OSError):
                pass
        time.sleep(0.02)
    return None


def read_until(s, want, timeout):
    """Lines read until one contains `want` (or the timeout); (found, lines). A dead port ends it early."""
    deadline = time.time() + timeout
    buf, lines = b'', []
    while time.time() < deadline:
        try:
            chunk = s.read(4096)
        except (serial.SerialException, OSError):
            return False, lines
        buf += chunk
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            text = line.decode(errors='replace').strip()
            lines.append(text)
            if want in text:
                return True, lines
    return False, lines


def ask(s, explicit, command, want, total):
    """Sends `command` until a line with `want` comes back, reopening the port if it dies; (port, lines or None)."""
    deadline = time.time() + total
    while time.time() < deadline:
        if s is None:
            s = open_serial(explicit, deadline)
            if s is None:
                break
        try:
            s.write(command)
        except (serial.SerialException, OSError):
            close_all(s)
            s = None
            continue
        ok, lines = read_until(s, want, 2.0)
        if ok:
            return s, lines
    return s, None


MIDI_CHILD = r"""
import sys, time, mido
notes = int(sys.argv[1])
deadline = time.time() + 5.0
while time.time() < deadline:
    ins = [n for n in mido.get_input_names() if 'Teensy MIDI' in n]
    outs = [n for n in mido.get_output_names() if 'Teensy MIDI' in n]
    if ins and outs:
        i, o = mido.open_input(ins[0]), mido.open_output(outs[0])
        for k in range(notes):
            o.send(mido.Message('note_on', channel=0, note=60 + k % 12, velocity=40))
            o.send(mido.Message('note_off', channel=0, note=60 + k % 12, velocity=0))
        for _ in i.iter_pending():
            pass
        i.close()
        o.close()
        sys.exit(0)
    time.sleep(0.05)
sys.exit(3)
"""
MIDI_HANGS = []  # cycles whose MIDI child hung in CoreMIDI (a host hang, killed; not the board's)


def touch_midi(n, notes):
    """Opens the 'Teensy MIDI' ports, sends `notes` notes and closes them in a child process with a hard timeout:
    python-rtmidi's `MidiIn.close_port` can block forever in CoreMIDI (2026-11-09, with the board healthy)."""
    try:
        subprocess.run([sys.executable, '-c', MIDI_CHILD, str(notes)], timeout=10, capture_output=True)
    except subprocess.TimeoutExpired:
        MIDI_HANGS.append(n)
        print(f'  [{n}] host: the MIDI child hung in CoreMIDI and was killed')


def close_all(*ports):
    # No `flush()` / drain anywhere: a wedged board takes no data, and macOS then blocks a drain (and a close that
    # drains) for good; a serial port gets 1 s to send what it holds, the rest is discarded before it closes.
    for p in ports:
        try:
            if p is not None:
                if isinstance(p, serial.Serial):
                    deadline = time.time() + 1.0
                    while p.out_waiting and time.time() < deadline:  # a bounded drain
                        time.sleep(0.005)
                    p.reset_output_buffer()
                p.close()
        except Exception:
            pass


def cycle(n, args):
    """None, 'watchdog' (hung, reset itself), 'usb' (USB wedged, reset itself) or why the board is gone."""
    s = open_serial(args.port, time.time() + 15.0)
    if s is None:
        return 'dead: no serial port before the reboot'
    # The MIDI ports open and close while the board is there: CoreMIDI's close of a port whose device has gone can
    # block forever (seen 2026-11-09 in `MidiInCore::closePort`), a host hang that is not the board's.
    if not args.no_midi:
        touch_midi(n, 0)
    s.reset_input_buffer()
    try:
        s.write(b'!REBOOT\n')
    except serial.SerialException:
        close_all(s)
        return 'dead: !REBOOT could not be written'
    if args.fast:
        close_all(s)
        t0 = time.time()
        time.sleep(1.5)
    else:
        ok, _ = read_until(s, 'host-requested reboot', 2.0)
        close_all(s)
        if not ok:
            return 'dead: no reboot acknowledgement (clock running?)'
        t0 = time.time()
        while time.time() - t0 < 2.0 and find_port(args.port) is not None:  # the port goes, then comes back
            time.sleep(0.01)
    s = open_serial(args.port, t0 + 15.0)
    if s is None:
        return 'dead: the serial port did not come back within 15 s'
    try:
        if not args.no_midi:
            touch_midi(n, args.notes)
        s, status = ask(s, args.port, b'!ENGINE status\n', '#ENGINE,status', 30.0)
        if status is None:
            return 'dead: no !ENGINE status reply in 30 s'
        s, rows = ask(s, args.port, b'!ENGINE boot\n', 'DIAG,prev_crumbs', 10.0)
        if rows is None:
            return 'dead: no !ENGINE boot reply'
        reset = next((l for l in rows if 'DIAG,boot_reset' in l), '-')
        crumbs = rows[-1]
        m = re.search(r'watchdog=(\d)', reset)
        print(f'  [{n}] {time.time() - t0:.2f} s  {reset.split("DIAG,", 1)[-1]}  {crumbs.split("DIAG,", 1)[-1]}')
        if m and m.group(1) == '1':
            return 'watchdog'
        if 'stage=12,' in crumbs:
            return 'usb'
        return None
    finally:
        close_all(s)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--cycles', type=int, default=25)
    ap.add_argument('--port', help='serial port (default: the first /dev/cu.usbmodem*)')
    ap.add_argument('--fast', action='store_true', help='close at once after !REBOOT, reopen after 1.5 s')
    ap.add_argument('--no-midi', action='store_true', help='leave the MIDI ports closed')
    ap.add_argument('--notes', type=int, default=16, help='notes sent while the board boots')
    args = ap.parse_args()
    if not args.no_midi and mido is None:
        print('mido is not installed; use --no-midi', file=sys.stderr)
        return 2
    watchdog, usb = [], []
    for n in range(1, args.cycles + 1):
        why = cycle(n, args)
        if why == 'watchdog':
            print(f'  [{n}] HANG: the watchdog reset the board (prev_crumbs above places it)')
            watchdog.append(n)
        elif why == 'usb':
            print(f'  [{n}] USB WEDGED: the board reset itself (prev_crumbs: USBSTS & USBINTR, ENDPTSTATUS, flags)')
            usb.append(n)
        elif why is not None:
            print(f'HANG at cycle {n}: {why}')
            return 1
    print(f'{args.cycles} reboot cycles, ports reopened each time; watchdog resets in cycles {watchdog or "none"}; '
          f'USB-wedge resets in cycles {usb or "none"}; host MIDI hangs (killed) in cycles {MIDI_HANGS or "none"}')
    return 1 if watchdog or usb else 0


if __name__ == '__main__':
    sys.exit(main())
