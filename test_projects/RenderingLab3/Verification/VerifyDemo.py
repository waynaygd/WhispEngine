"""Build a demo-only verifier against the existing Release Engine libraries.

Run from an x64 VS Developer shell. Does not edit CMake or production sources.
Uses compile/link flags of the configured MaterialEditorUXRegression target.
"""
from pathlib import Path
import argparse
import os
import subprocess

repo = Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser()
parser.add_argument('--build', type=Path, default=repo / 'out/build/x64-release')
args = parser.parse_args()
build = args.build.resolve()
if os.name != 'nt':
    raise SystemExit('DX12 verification requires Windows and an x64 VS Developer shell')
cache = (build / 'CMakeCache.txt').read_text(encoding='utf-8')
if 'CMAKE_BUILD_TYPE:STRING=Release' not in cache or 'ENABLE_DX12:BOOL=ON' not in cache:
    raise SystemExit('Use a configured DX12 Release build')
ninja = next(line.split('=', 1)[1] for line in cache.splitlines() if line.startswith('CMAKE_MAKE_PROGRAM:'))
commands = subprocess.check_output([ninja, '-C', str(build), '-t', 'commands', 'MaterialEditorUXRegression'], encoding='utf-8').splitlines()
compile_command = next(c for c in commands if 'MaterialEditorUXRegression.cpp' in c and ' -c ' in c)
link_command = next(c for c in reversed(commands) if '/out:material-editor-ux-regression' in c)
source = str(repo / 'tests/MaterialEditorUXRegression.cpp')
verifier = str(Path(__file__).with_name('VerifyDemo.cpp'))
compile_command = compile_command.replace(source, verifier).replace(source.replace('\\', '/'), verifier.replace('\\', '/'))
compile_command = compile_command.replace(' /nologo', f' /I"{repo / "tests"}" /nologo', 1)
def isolate(command):
    return command.replace('MaterialEditorUXRegression', 'RenderingLab3Verify').replace('material-editor-ux-regression', 'renderinglab3-verification')
(build / 'CMakeFiles/RenderingLab3Verify.dir/tests').mkdir(parents=True, exist_ok=True)
output = build / 'renderinglab3-verification'
output.mkdir(exist_ok=True)
for name, command in [('compile', compile_command), ('link', link_command)]:
    with (output / (name + '.log')).open('w', encoding='utf-8') as log:
        result = subprocess.run(isolate(command), cwd=build, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        raise SystemExit(f'{name} failed; see {output / (name + ".log")}')
environment = os.environ.copy()
environment['WHISP_DX12_DEBUG'] = '1'
with (output / 'verification.log').open('w', encoding='utf-8') as log:
    result = subprocess.run([str(output / 'RenderingLab3Verify.exe')], cwd=build, env=environment, stdout=log, stderr=subprocess.STDOUT)
print((output / 'verification.log').read_text(encoding='utf-8', errors='replace')[-5000:])
raise SystemExit(result.returncode)
