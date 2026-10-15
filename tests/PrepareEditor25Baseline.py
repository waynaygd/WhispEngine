from pathlib import Path
import subprocess,zipfile,io,re,shutil
root=Path(__file__).resolve().parents[1]
out=root/'out/diagnostics/editor25';target=out/'baseline-source';target.mkdir(parents=True,exist_ok=True)
ref=(out/'before-head.txt').read_text(encoding='utf-8-sig').strip()
archive=(out/'before-source.zip').read_bytes()
with zipfile.ZipFile(io.BytesIO(archive)) as z:z.extractall(target)
sources=[]
for source in re.findall(r'[\w/]+\.cpp',(target/'engine/CMakeLists.txt').read_bytes().decode('cp1251')):
    if source.startswith(('render/backends/vulkan','external/')) or not (target/'engine'/source).is_file():continue
    if source not in sources:sources.append(source)
deps=root/'out/build/x64-release/_deps'; build=root/'out/build/shadows-dx12'
includes=[target/'engine',target/'engine/external',target/'engine/third_party',root/'external/imgui',root/'external/imgui/backends',root/'external/imguizmo/src',root/'external/dxc/include',deps/'glfw-src/include',deps/'assimp-src/include',build/'_deps/assimp-build/include',deps/'enkits-src/src',deps/'tracy-src/public',deps/'stb-src']
libraries=[build/p for p in ['_deps/assimp-build/lib/assimp-vc143-mt.lib','_deps/assimp-build/contrib/zlib/zlibstatic.lib','StbImage.lib','engine/ImGuizmo.lib','engine/DearImGui.lib','_deps/glfw-build/src/glfw3.lib','_deps/enkits-build/enkiTS.lib','_deps/tracy-build/TracyClient.lib']]+[root/'external/dxc/lib/x64/dxcompiler.lib']
quote=lambda p:'"'+p.as_posix()+'"'
cm='cmake_minimum_required(VERSION 3.20)\nproject(Editor25Baseline LANGUAGES CXX)\nset(CMAKE_CXX_STANDARD 20)\nadd_library(BaselineEngine STATIC\n'+''.join(' engine/'+p+'\n' for p in sources)+')\n'
cm+='target_compile_definitions(BaselineEngine PUBLIC ENABLE_DX12=1 TRACY_ENABLE TRACY_ON_DEMAND)\n'
cm+='target_include_directories(BaselineEngine PUBLIC '+' '.join(map(quote,includes))+')\n'
cm+='target_link_libraries(BaselineEngine PUBLIC '+' '.join(map(quote,libraries))+' d3d12 dxgi dxguid d3dcompiler comdlg32)\nadd_executable(EditorBaseline WhispEngine.cpp)\ntarget_link_libraries(EditorBaseline PRIVATE BaselineEngine)\n'
(target/'CMakeLists.txt').write_text(cm,encoding='utf-8')

print(target)

runtime=out/'baseline-runtime';runtime.mkdir(parents=True,exist_ok=True)
for folder in ['assets','shaders']:shutil.copytree(target/'engine'/folder,runtime/folder,dirs_exist_ok=True)
for p in build.glob('*.dll'):shutil.copy2(p,runtime/p.name)
for p in (build/'shaders/dx12').glob('*.dxil'):shutil.copy2(p,runtime/'shaders/dx12'/p.name)
print('After building, copy baseline-build/EditorBaseline.exe to baseline-runtime.')
