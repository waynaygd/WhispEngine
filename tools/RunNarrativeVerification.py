"""Run the narrative/related regressions with isolated editor preferences.

Original preferences/workspace bytes are backed up and restored even on failure.
Run with the bundled Python, or any Python 3.10+. No third-party packages needed.
"""
from pathlib import Path
import argparse, subprocess, shutil

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--build',type=Path,default=root/'out/build/x64-release')
parser.add_argument('--installed',type=Path)
args=parser.parse_args()
build=args.build.resolve()
ctest=shutil.which('ctest') or 'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe'
files=[build/'editor-preferences.json',build/'imgui.ini']
saved={p:p.read_bytes() if p.exists() else None for p in files}
backup=root/'out/narrative-preferences-before';backup.mkdir(parents=True,exist_ok=True)
for p,data in saved.items():
 if data is not None:(backup/p.name).write_bytes(data)
pattern=r'^(NarrativePrototype.*|CameraSystem.*|ProjectCoreRegression|ProjectSystem.*|PlayerWorkflow.*|PrefabInheritance.*|LuaScriptingRegression|RuntimeUIRegression|WidgetDesignerRegression|CharacterMovement.*|AssetBrowserUX.*)$'
result=0
try:
 for p in files:
  if p.exists():p.unlink()
 with (root/'out/narrative-verified-tests.log').open('w',encoding='utf-8') as log:
  run=subprocess.run([ctest,'--test-dir',str(build),'-R',pattern,'--output-on-failure'],stdout=log,stderr=subprocess.STDOUT)
  result=run.returncode
 print('Related CTest result:',result)
 if args.installed:
  with (root/'out/narrative-installed-tests.log').open('w',encoding='utf-8') as log:
   run=subprocess.run([str(build/'NarrativePrototypeRegression.exe'),'--installed',str(args.installed.resolve())],cwd=build,stdout=log,stderr=subprocess.STDOUT)
   result=result or run.returncode
  print('Installed project result:',run.returncode)
finally:
 for p,data in saved.items():
  if data is None:
   if p.exists():p.unlink()
  else:p.write_bytes(data)
 for p,data in saved.items():assert (p.read_bytes() if p.exists() else None)==data
 print('Original editor preferences and workspace bytes restored and verified')
raise SystemExit(result)
