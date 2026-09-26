"""Reconstruct and exercise undo from a verified HOST folder copy; never uses a device.

Writes an empty-history recovery bundle; undo must derive from slot content.
Host acceptance is not authorization for device installation. Exit 2 means a roundtrip
or reload failed. Each slot runs in a fresh process to isolate failed publication.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from audit_backup import audit, require


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--automatic-recovery',action='store_true')
    parser.add_argument('backup', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    backup, output = args.backup.resolve(), args.output.resolve()
    require(not output.exists(), 'Output must be new')
    require(not output.is_relative_to(backup), 'Output must be outside backup')
    audit(backup)
    original = {str(p.relative_to(backup)): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in backup.rglob('*') if p.is_file()}
    output.mkdir(parents=True)
    project = Path(__file__).resolve().parents[2]
    env = dict(os.environ)
    for key in list(env):
        if key.startswith('MIDILOOPER_UNDO_') or key.startswith('MIDILOOPER_RECOVERY_'):
            env.pop(key)
    env.pop('PLATFORMIO_BUILD_FLAGS', None)
    if args.automatic_recovery: env['MIDILOOPER_AUTOMATIC_RECOVERY']='1'
    suite='test_offline_boot_recovery' if args.automatic_recovery else 'test_offline_undo_recovery'
    env['MIDILOOPER_RECOVERY_CURRENT'] = str(backup/'MidiLooper/current')
    env['MIDILOOPER_RECOVERY_BUNDLE_OUTPUT'] = str(output/'empty-history.bundle.bin')
    with tempfile.TemporaryDirectory(prefix='midilooper-undo-') as temp:
        work = Path(temp)
        headers = work/'headers'
        subprocess.run([sys.executable, str(Path(__file__).with_name('prepare_native_decode_headers.py')), str(headers)], check=True, capture_output=True)
        flag = '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections'
        (work/'link.py').write_text("Import('env')\nenv.Append(LINKFLAGS=["+repr(flag)+"])\n")
        config = work/'platformio.ini'
        config.write_text(f'''[platformio]
include_dir = {headers}
build_dir = {work/'build'}
[env:native]
platform = native
test_framework = unity
build_flags = -std=gnu++17 -DPIO_UNIT_TEST_NATIVE=1 -DRECOVERY_TEENSY_ABI=1 -I{headers/'EditStates'} -Itest/test_support/firmware -ffunction-sections -fdata-sections
extra_scripts = pre:{work/'link.py'}
''')
        buildenv = dict(env, MIDILOOPER_UNDO_TARGET='0:0', MIDILOOPER_UNDO_BUNDLE_OUTPUT=str(output/'candidate-runtime.bundle.bin'))
        run = subprocess.run(['pio', 'test', '-c', str(config), '-e', 'native', '-f', suite, '-v'], cwd=project, env=buildenv, capture_output=True, text=True, timeout=180)
        (output/'build.log').write_text(run.stdout+run.stderr)
        require(run.returncode == 0, 'Build/first-slot test failed; inspect build.log')
        entries = []
        for line in run.stdout.splitlines():
            if line.startswith('RECOVERED_UNDO,'):
                entries.append({k:int(v) for k,v in (field.split('=') for field in line.split(',')[1:])})
        require(bool(entries), 'No reconstructed entries')
        (output/'reconstructed-actions.json').write_text(json.dumps(entries, indent=2)+'\n')
        expected = json.loads((project/'openspec/changes/runtime-working-set-ownership/evidence/2026-09-26-workspace-loss/reconstructed-actions.json').read_text())
        key = lambda e: (e['track'],e['slot'],e['kind'],e['pass'],e['group_passes'],e['companion_edits'])
        require(sorted(map(key,entries))==sorted(map(key,expected)), 'Loaded action inventory differs from preserved offline audit')
        binary = work/'build/native/program' 
        results = []
        for t in range(8):
            for s in range(8):
                run = subprocess.run([str(binary)], cwd=project, env=dict(env, MIDILOOPER_UNDO_TARGET=f'{t}:{s}'), capture_output=True, text=True, timeout=180 if args.automatic_recovery else 40)
                text = run.stdout+run.stderr
                (output/f'slot-{t+1}-{s+1}.log').write_text(text)
                results.append({'track':t+1, 'slot':s+1, 'passed':run.returncode==0,
                    'entries':sum(e['track']==t and e['slot']==s for e in entries),
                    'findings':[l for l in text.splitlines() if ':FAIL:' in l or l.startswith(('UNDO_STALL,', 'UNDO_NOTE_'))]})
        run = subprocess.run([str(binary)], cwd=project, env=dict(env,
            MIDILOOPER_RECOVERY_BUNDLE_OUTPUT=str(output/'reloaded-history.bundle.bin'),
            MIDILOOPER_UNDO_RELOAD_ONLY='1', MIDILOOPER_RECOVERY_EXISTING_BUNDLE=str(output/'candidate-runtime.bundle.bin')),
            capture_output=True, text=True, timeout=180 if args.automatic_recovery else 40)
        (output/'reload.log').write_text(run.stdout+run.stderr)
        match = re.search(r'UNDO_RELOAD,total=(\d+)', run.stdout)
        retained = int(match.group(1)) if match else None
        reloaded = []
        for line in run.stdout.splitlines():
            if line.startswith('RECOVERED_UNDO,'):
                reloaded.append({k:int(v) for k,v in (field.split('=') for field in line.split(',')[1:])})
        reload_pass = run.returncode==0 and retained==len(entries) and sorted(map(key,reloaded))==sorted(map(key,entries))
        hydration_results = {}
        for mode in ('before','after'):
            run = subprocess.run([str(binary)], cwd=project, env=dict(env,MIDILOOPER_UNDO_HYDRATION=mode),
                                 capture_output=True,text=True,timeout=180 if args.automatic_recovery else 40)
            (output/f'hydration-{mode}.log').write_text(run.stdout+run.stderr)
            hydration_results[mode] = run.returncode==0
    audit(backup)
    require(original == {str(p.relative_to(backup)): hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in backup.rglob('*') if p.is_file()}, 'Source backup changed')
    report = {'device_access':False, 'automatic_recovery':args.automatic_recovery, 'original_bytes_unchanged':True,
              'reconstructed_actions':len(entries), 'reload_retained':retained,
              'reload_preserves_all_actions':reload_pass, 'hydration_order':hydration_results, 'slots':results,
              'safe_to_install':False, 'scope':'Host diagnostic reconstruction; original chronology, prior redo branch and clear snapshots are not recovered',
              'bundle_sha256':hashlib.sha256((output/'candidate-runtime.bundle.bin').read_bytes()).hexdigest()}
    (output/'undo-reconstruction.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='slots'}, indent=2))
    print('Slot roundtrips:', sum(r['passed'] for r in results), '/64')
    return 0 if reload_pass and all(hydration_results.values()) and all(r['passed'] for r in results) else 2


if __name__ == '__main__':
    sys.exit(main())
