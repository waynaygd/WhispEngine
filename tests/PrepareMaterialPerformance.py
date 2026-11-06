"""Prepare a NEW isolated HEAD snapshot/build without touching git HEAD/index.
Run from an x64 Visual Studio Developer shell. Reuses cached dependency sources.
"""
import argparse, pathlib, subprocess, tarfile, io, shutil, json
p=argparse.ArgumentParser()
p.add_argument('--root',type=pathlib.Path,default=pathlib.Path('out/lab3-evidence-repro'))
p.add_argument('--cmake',default='cmake')
p.add_argument('--renderdoc',default='C:/Program Files/RenderDoc')
a=p.parse_args();repo=pathlib.Path(__file__).resolve().parents[1];root=a.root.resolve()
if root.exists():raise SystemExit('Choose a new --root; existing evidence is never overwritten')
root.mkdir(parents=True);source=root/'current-source';source.mkdir()
archive=subprocess.check_output(['git','-C',str(repo),'archive','HEAD'])
with tarfile.open(fileobj=io.BytesIO(archive)) as t:t.extractall(source,filter='data')
shutil.copy2(repo/'tests/MaterialPerformanceProbe.cpp',source/'tests/MaterialPerformanceProbe.cpp')
deps=['assimp','glfw','enkits','lua','sol2','stb','tracy']
for dep in deps:
 shutil.copytree(repo/'out/build/x64-release/_deps'/(dep+'-src'),source/'vendor'/(dep+'-src'),ignore=shutil.ignore_patterns('.git'))
with (source/'CMakeLists.txt').open('a',encoding='utf-8') as f:
 f.write('\nadd_executable(MaterialPerformanceProbe tests/MaterialPerformanceProbe.cpp)\n'
 'target_link_libraries(MaterialPerformanceProbe PRIVATE Engine Tracy::TracyClient)\n'
 'target_compile_definitions(MaterialPerformanceProbe PRIVATE ENABLE_DX12=1)\n'
 'add_dependencies(MaterialPerformanceProbe MaterialShaderSources)\n'
 'target_include_directories(MaterialPerformanceProbe PRIVATE "'+a.renderdoc.replace('\\','/')+'")\n')
command=[a.cmake,'-S',str(source),'-B',str(root/'local-build'),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DENABLE_DX12=ON','-DENABLE_VULKAN=ON','-DENABLE_TRACY=ON','-DWHISP_BUILD_PHYSICS_TESTS=ON','-DWHISP_STAGE_TEST_PROJECTS=ON','-DFETCHCONTENT_FULLY_DISCONNECTED=ON']
command+=['-DFETCHCONTENT_SOURCE_DIR_'+d.upper()+'='+str(source/'vendor'/(d+'-src')) for d in deps]
(root/'prepare-command.json').write_text(json.dumps(command,indent=2))
with (root/'configure.log').open('w') as f:subprocess.run(command,stdout=f,stderr=subprocess.STDOUT,check=True)
with (root/'build.log').open('w') as f:subprocess.run([a.cmake,'--build',str(root/'local-build'),'--parallel','8','--target','MaterialPerformanceProbe','WhispEngine','MaterialSystemRegression','MaterialEditorUXRegression','GpuInstancingRegression','HdrOutputRegression','PbrCoreRegression','PrefabInheritanceRegression'],stdout=f,stderr=subprocess.STDOUT,check=True)
print(root)
