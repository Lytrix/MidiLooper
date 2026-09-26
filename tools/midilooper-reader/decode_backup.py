"""Validate a verified folder copy with production parsers and Teensy event ABI.

Host-only: runs a native test, never serial/upload. Output must be outside backup.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from audit_backup import audit, require


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('backup', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    backup, output = args.backup.resolve(), args.output.resolve()
    require(not output.is_relative_to(backup), 'Output must not modify backup')
    require(not output.exists(), 'Output directory must be new')
    audit(backup)  # Abort on any transfer mismatch before consuming slot bodies.
    output.mkdir(parents=True)
    project = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix='midilooper-decode-') as temporary:
        work = Path(temporary)
        headers = work / 'headers'
        subprocess.run([sys.executable, str(Path(__file__).with_name('prepare_native_decode_headers.py')),
                        str(headers)], check=True, capture_output=True)
        config = work / 'platformio.ini'
        config.write_text(f'''[platformio]
include_dir = {headers}
build_dir = {work / 'build'}
[env:native]
platform = native
test_framework = unity
build_flags = -std=gnu++17 -DPIO_UNIT_TEST_NATIVE=1 -DRECOVERY_TEENSY_ABI=1 -I{headers / 'EditStates'}
''')
        env = dict(os.environ)
        env.pop('PLATFORMIO_BUILD_FLAGS', None)
        env['MIDILOOPER_RECOVERY_SLOTS'] = str(backup / 'MidiLooper/current/slots')
        command = ['pio', 'test', '-c', str(config), '-e', 'native', '-f', 'test_offline_recovery_decode', '-v']
        result = subprocess.run(command, cwd=project, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, timeout=180)
        log = result.stdout
        (output / 'native.log').write_text(log)
        rows = [json.loads(line.split('RECOVERY_SLOT,', 1)[1]) for line in log.splitlines()
                if line.startswith('RECOVERY_SLOT,')]
        identities = {(r['track'], r['slot']) for r in rows}
        passed = (result.returncode == 0 and 'RECOVERY_ABI,event=20,data=12' in log
                  and len(rows) == 64 and identities == {(t,s) for t in range(8) for s in range(8)}
                  and all(r['decoded'] for r in rows))
        report = {'passed': passed, 'scope': 'decoder acceptance, not playback parity or restore readiness',
                  'manifest_sha256': hashlib.sha256((backup/'manifest.json').read_bytes()).hexdigest(),
                  'abi': 'Teensy event=20,data=12; opaque SysEx pointer; SysEx content refused',
                  'slots': rows, 'native_exit': result.returncode,
                  'raw_events': sum(r['raw_events'] for r in rows),
                  'snapshot_slots': sum(r['snapshot'] for r in rows),
                  'snapshot_notes': sum(r['snapshot_notes'] for r in rows)}
        (output / 'decode.json').write_text(json.dumps(report, indent=2)+'\n')
        print(json.dumps({k:v for k,v in report.items() if k != 'slots'}, indent=2))
        return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
