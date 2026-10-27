"""Reproduce P1 ABI/allocator observations only; never build/upload firmware."""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--out-dir', default=os.environ.get('PI_SCRATCH_DIR'))
args = parser.parse_args()
if not args.out_dir:
    parser.error('provide --out-dir (use session scratch, not the workspace)')
base = Path(__file__).resolve().parent
repo = base.parents[4]
out = Path(args.out_dir).resolve()
if out == repo or repo in out.parents:
    parser.error('outputs must stay outside the workspace (session scratch recommended)')
out.mkdir(parents=True, exist_ok=True)
framework = Path.home() / '.platformio/packages/framework-arduinoteensy'
tools = Path.home() / '.platformio/packages/toolchain-gccarmnoneeabi-teensy/bin'
flags = json.loads((base / 'shipping-flags.json').read_text())

def run(label, command):
    (out / (label + '-command.txt')).write_text(shlex.join(map(str, command)) + '\n')
    result = subprocess.run(list(map(str, command)), cwd=repo, capture_output=True, text=True)
    (out / (label + '.log')).write_text(result.stdout + result.stderr)
    print(label, 'exit', result.returncode)
    if result.returncode:
        raise SystemExit(result.returncode)
    return result.stdout

arm = [tools / 'arm-none-eabi-g++'] + flags['CCFLAGS'] + flags['CXXFLAGS']
for define in flags['CPPDEFINES']:
    arm.append('-D' + (define[0] + '=' + str(define[1]) if isinstance(define, list) else define))
for path in flags['CPPPATH']:
    arm.append('-I' + path.format(repo=repo, framework=framework))
for path in sorted((framework / 'libraries').glob('*')) + sorted((repo / '.pio/libdeps/teensy41-capture-serial').glob('*')):
    if path.is_dir():
        arm += ['-I' + str(path), '-I' + str(path / 'src')]
arm += ['-I' + str(repo), '-fno-access-control', '-c', base / 'custody-size.cpp', '-o', out / 'custody-arm.o']
run('custody-arm', arm)
symbols = run('custody-arm-nm', [tools / 'arm-none-eabi-nm', '-S', '--size-sort', '--radix=d', out / 'custody-arm.o'])
target = {name: int(size) for size, name in re.findall(r'^\d+\s+(\d+)\s+B\s+size_(\w+)$', symbols, re.M)}
host = ['c++', '-std=gnu++17', '-DPIO_UNIT_TEST_NATIVE=1', '-fno-access-control',
        '-I' + str(repo / 'test/test_support/firmware'), '-I' + str(repo / 'include'), '-I' + str(repo / 'include/EditStates')]
source = '#include "' + str(base / 'custody-size.cpp') + '"\n#include <cstdio>\nint main(){\n'
for name, ctype in re.findall(r'^SZ\((\w+),\s*(.+)\)$', (base / 'custody-size.cpp').read_text(), re.M):
    source += 'std::printf("' + name + '=%zu\\n",sizeof(' + ctype + '));\n'
source += '}\n'
(out / 'custody-host-main.cpp').write_text(source)
run('custody-host-build', host + [out / 'custody-host-main.cpp', '-o', out / 'custody-host'])
host_sizes = dict(line.split('=') for line in run('custody-host', [out / 'custody-host']).splitlines())
assert target and set(host_sizes) == set(target), 'missing size symbols'
table = '| Type | Host bytes | Shipping ARM bytes |\n|---|---:|---:|\n'
table += ''.join('| ' + name + ' | ' + value + ' | ' + str(target[name]) + ' |\n' for name, value in host_sizes.items())
(out / 'custody-sizes.md').write_text(table)
run('custody-allocator-build', host + ['-ffunction-sections', '-fdata-sections', '-I' + str(repo),
    base / 'custody-allocator.cpp', '-Wl,-dead_strip', '-o', out / 'custody-allocator'])
run('custody-allocator', [out / 'custody-allocator'])
print('Measured layouts and native reserve assertions only; no supported-capacity or device-timing claim.')
