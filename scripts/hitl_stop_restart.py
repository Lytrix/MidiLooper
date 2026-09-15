#!/usr/bin/env python3
"""Verify content surviving a completed hitl_stop_interruption fixture.

Does not upload, clear, or record. Requires exclusive ownership of the serial
port. Sends one Start and always sends Stop on exit after a successful Start.
"""
import argparse
import json
from pathlib import Path
import threading
import time

import mido
import serial

from hitl.verify.stop_restart_survival import verify_restart_survival


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out-dir', type=Path, required=True)
    parser.add_argument('--serial-port', required=True)
    parser.add_argument('--midi-port', default='Teensy MIDI')
    parser.add_argument('--track-number', type=int, choices=range(1, 9), required=True)
    args = parser.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=False)
    rows, actions = [], []
    lock = threading.Lock()
    with (args.out_dir / 'midi-output.jsonl').open('w', buffering=1) as midi_log, \
         (args.out_dir / 'actions.jsonl').open('w', buffering=1) as action_log, \
         (args.out_dir / 'serial.log').open('wb', buffering=0) as serial_log:
        def receive(message):
            row = {'host_ns': time.monotonic_ns(), 'message': message.dict()}
            with lock:
                rows.append(row)
                midi_log.write(json.dumps(row) + '\n')

        def action(event):
            row = {'event': event, 'host_ns': time.monotonic_ns()}
            actions.append(row)
            action_log.write(json.dumps(row) + '\n')

        with serial.Serial(args.serial_port, 115200, timeout=.05) as device, \
             mido.open_input(args.midi_port, callback=receive), \
             mido.open_output(args.midi_port) as output:
            def observe(seconds):
                deadline = time.monotonic() + seconds
                while time.monotonic() < deadline:
                    data = device.read(device.in_waiting or 1)
                    if data:
                        serial_log.write(data)

            observe(1)
            output.send(mido.Message('start'))
            try:
                action('start')
                observe(20)
            finally:
                output.send(mido.Message('stop'))
                action('stop')
                observe(5)
    result = verify_restart_survival(rows, actions, channel=args.track_number - 1)
    (args.out_dir / 'restart-result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0 if result['result'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
