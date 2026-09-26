"""Build and test a NEW host workspace candidate. Never accesses the device.

Copies current files verbatim, adds the C++-generated missing runtime bundle,
then checks production restore results against independently decoded inventory.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from audit_backup import audit, epoch, require


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('backup',type=Path)
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    backup,output=args.backup.resolve(),args.output.resolve()
    require(not output.exists(),'Output directory must be new')
    require(not output.is_relative_to(backup),'Output must be outside the backup')
    audit(backup)
    require(not (backup/'MidiLooper/current/temp/runtime.bundle.bin').exists(),
            'This procedure is only for a missing runtime bundle')
    output.mkdir(parents=True)
    project=Path(__file__).resolve().parents[2]
    decoder=Path(__file__).with_name('decode_backup.py')
    subprocess.run([sys.executable,str(decoder),str(backup),str(output/'source-decode')],check=True)
    expected=json.loads((output/'source-decode/decode.json').read_text())
    candidate=output/'candidate/MidiLooper/current'
    shutil.copytree(backup/'MidiLooper/current',candidate)
    (candidate/'temp').mkdir(exist_ok=True)
    bundle=candidate/'temp/runtime.bundle.bin'
    with tempfile.TemporaryDirectory(prefix='midilooper-reconstruct-') as temp:
        work=Path(temp);headers=work/'headers'
        subprocess.run([sys.executable,str(Path(__file__).with_name('prepare_native_decode_headers.py')),str(headers)],check=True,capture_output=True)
        link=work/'link.py'
        flag='-Wl,-dead_strip' if sys.platform=='darwin' else '-Wl,--gc-sections'
        link.write_text("Import('env')\nenv.Append(LINKFLAGS=["+repr(flag)+"])\n")
        config=work/'platformio.ini'
        config.write_text(f'''[platformio]
include_dir = {headers}
build_dir = {work/'build'}
[env:native]
platform = native
test_framework = unity
build_flags = -std=gnu++17 -DPIO_UNIT_TEST_NATIVE=1 -DRECOVERY_TEENSY_ABI=1 -I{headers/'EditStates'} -Itest/test_support/firmware -ffunction-sections -fdata-sections
extra_scripts = pre:{link}
''')
        env=dict(os.environ);env.pop('PLATFORMIO_BUILD_FLAGS',None)
        env['MIDILOOPER_RECOVERY_CURRENT']=str(candidate)
        env['MIDILOOPER_RECOVERY_BUNDLE_OUTPUT']=str(bundle)
        result=subprocess.run(['pio','test','-c',str(config),'-e','native','-f','test_offline_workspace_recovery','-v'],cwd=project,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=180)
        (output/'restore-native.log').write_text(result.stdout)
        require(result.returncode==0,'Native restore failed; inspect restore-native.log. Candidate NOT approved.')
    actual={}
    for line in result.stdout.splitlines():
        if line.startswith('RESTORED_SLOT,'):
            row={k:int(v) for k,v in (item.split('=') for item in line.split(',')[1:])}
            key=(row['track'],row['slot']);require(key not in actual,'duplicate restored identity')
            actual[key]=row
    require(len(actual)==64,'Missing restored slot results')
    for item in expected['slots']:
        restored=actual[item['track'],item['slot']]
        require(restored['length']==item['length'] and restored['events']==item['raw_events'],
                f'Restored inventory differs: {item["track"]}/{item["slot"]}')
        if item['snapshot']:
            require(restored['snapshot']==1 and restored['notes']==item['snapshot_notes'],
                    'Restored snapshot inventory differs')
    originals=[]
    for file in sorted((backup/'MidiLooper/current').rglob('*')):
        if not file.is_file():continue
        relative=file.relative_to(backup/'MidiLooper/current')
        require((candidate/relative).read_bytes()==file.read_bytes(),'Original bytes changed')
        originals.append({'path':str(relative),'sha256':hashlib.sha256(file.read_bytes()).hexdigest()})
    bundle_bytes=bundle.read_bytes();body=epoch(bundle_bytes)
    report={'passed':True,'device_access':False,'capture_event_bytes_equal':True,'candidate':str(candidate),
            'scope':'host reconstruction/load acceptance; not device validation or playback parity',
            'defaults':{'tempo':120,'transport':'idle/stopped','selection':'first nonempty slot per track',
                        'enabled':'only selected slot per track','muted':False,'selected_track':1,
                        'undo':'fresh stack; original global undo history not recovered'},
            'bundle':{'bytes':len(bundle_bytes),'sha256':hashlib.sha256(bundle_bytes).hexdigest(),**body},
            'unchanged_originals':originals,'restored_slots':list(actual.values()),
            'raw_events':sum(r['events'] for r in actual.values())}
    report['source_sha256']={str(p.relative_to(project)):hashlib.sha256(p.read_bytes()).hexdigest()
        for p in (project/'tools/midilooper-reader/RecoveryRuntimeBundle.h',
                  project/'test/test_offline_workspace_recovery/test_offline_workspace_recovery.cpp',
                  project/'src/StorageManager/CurrentSetBootLoad.cpp',
                  project/'src/StorageManager/RuntimeBundleFooter.cpp')}
    (output/'reconstruction.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('unchanged_originals','restored_slots')},indent=2))


if __name__=='__main__':main()
