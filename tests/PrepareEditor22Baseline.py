"""Reconstruct the saved Editor 2.1 working tree, with measurement-only switches.
Run before-tree.zip must exist; this never resets the user's working tree.
"""
from pathlib import Path
import subprocess, re, zipfile, io
root=Path(__file__).resolve().parents[1]
out=root/'out/diagnostics/editor22'; target=out/'baseline-source'; target.mkdir(parents=True,exist_ok=True)
snapshot_ref=(out/'before-head.txt').read_text(encoding='utf-8').strip()
archive=subprocess.check_output(['git','archive','--format=zip',snapshot_ref,'engine','WhispEngine.cpp'],cwd=root)
with zipfile.ZipFile(io.BytesIO(archive)) as z:z.extractall(target)
with zipfile.ZipFile(out/'before-tree.zip') as z:z.extractall(target)
def patch(name,old,new):
    p=target/name;s=p.read_bytes().decode('cp1251').replace('\r\n','\n');assert old in s,(name,old[:100]);p.write_bytes(s.replace(old,new).replace('\n','\r\n').encode('cp1251'))
patch('engine/editor/EditorLayer.h','double uiMs=0,hierarchyMs=0,inspectorMs=0;','double uiMs=0,hierarchyMs=0,inspectorMs=0,assetBrowserMs=0;')
patch('engine/editor/EditorAssets.cpp','#include <cstring>','#include <cstring>\n#include <chrono>')
patch('engine/editor/EditorAssets.cpp','void EditorLayer::DrawAssetBrowser(Application& app){','void EditorLayer::DrawAssetBrowser(Application& app){\n    const auto start=std::chrono::steady_clock::now();struct Timer {double& ms;std::chrono::steady_clock::time_point start;~Timer(){ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}} timer{m_Statistics.assetBrowserMs,start};')
patch('engine/editor/EditorLayer.cpp','m_Statistics.hierarchyMs=m_Statistics.inspectorMs=0;','m_Statistics.hierarchyMs=m_Statistics.inspectorMs=m_Statistics.assetBrowserMs=0;')
patch('engine/render/EditorVisuals.h','bool active=false,play=false,showGrid=true;','bool active=false,play=false,showGrid=true,backgroundEnabled=true;')
patch('engine/editor/EditorLayer.h','    void SetGridVisible(bool show){m_ShowGrid=show;}','    int m_DiagnosticWidth=0,m_DiagnosticHeight=0;bool m_SkyDiagnosticEnabled=true;\n    void SetDiagnosticViewport(int w,int h){m_DiagnosticWidth=w;m_DiagnosticHeight=h;}\n    void SetSkyDiagnosticEnabled(bool enabled){m_SkyDiagnosticEnabled=enabled;}\n    void SetGridVisible(bool show){m_ShowGrid=show;}')
patch('engine/editor/EditorLayer.cpp','    m_ViewportMinX = contentMin.x;','    if(m_DiagnosticWidth>1&&m_DiagnosticHeight>1){m_ViewportPixelWidth=m_DiagnosticWidth;m_ViewportPixelHeight=m_DiagnosticHeight;}\n    m_ViewportMinX = contentMin.x;')
patch('engine/editor/EditorVisualSettings.cpp','f.settings=m_VisualSettings;','f.settings=m_VisualSettings;f.backgroundEnabled=m_SkyDiagnosticEnabled;')
patch('engine/render/backends/dx12/Dx12EditorVisuals.cpp','||f.play||(grid&&!EditorGridVisible(f))','||f.play||(!grid&&!f.backgroundEnabled)||(grid&&!EditorGridVisible(f))')
patch('WhispEngine.cpp','        if(std::strcmp(argv[i],"--no-editor-visuals")==0)', '''        if(std::strcmp(argv[i],"--editor-benchmark")==0&&i+1<argc)app.SetRenderBenchmarkOutput(argv[++i]);
        else if(std::strcmp(argv[i],"--sky-off")==0)app.GetEditorLayer().SetSkyDiagnosticEnabled(false);
        else if(std::strcmp(argv[i],"--diagnostic-viewport")==0&&i+2<argc){int w=std::atoi(argv[++i]);int h=std::atoi(argv[++i]);app.GetEditorLayer().SetDiagnosticViewport(w,h);}
        else if(std::strcmp(argv[i],"--shadow-quality")==0&&i+1<argc){int q=std::atoi(argv[++i]);auto& s=app.GetShadowSettings();s.pcf=q!=0;s.resolution=q==0?512:q==1?1024:2048;s.filterRadius=q==3?2.f:1.5f;}
        else if(std::strcmp(argv[i],"--select-first")==0){auto entities=app.GetWorld().Entities();if(!entities.empty())app.GetEditorLayer().SelectEntity(entities.front());}
        else if(std::strcmp(argv[i],"--no-editor-visuals")==0)''')
# Same appended CSV fields; constant 3 records the original filter.
(target/'engine/core/RenderDiagnosticSample.h').write_bytes((root/'engine/core/RenderDiagnosticSample.h').read_bytes())
patch('engine/core/Application.cpp','m_Windows.front().renderer->GetEditorVisualDiagnostics()});','m_Windows.front().renderer->GetEditorVisualDiagnostics(),m_EditorLayer.GetStatistics().assetBrowserMs,3});')
sources=[]
for source in re.findall(r'[\w/]+\.cpp',(target/'engine/CMakeLists.txt').read_bytes().decode('cp1251')):
    if source.startswith(('render/backends/vulkan','external/')) or not (target/'engine'/source).is_file():continue
    if source not in sources:sources.append(source)
deps=root/'out/build/x64-release/_deps'; build=root/'out/build/shadows-dx12'
includes=[target/'engine',target/'engine/external',target/'engine/third_party',root/'external/imgui',root/'external/imgui/backends',root/'external/imguizmo/src',root/'external/dxc/include',deps/'glfw-src/include',deps/'assimp-src/include',build/'_deps/assimp-build/include',deps/'enkits-src/src',deps/'tracy-src/public',deps/'stb-src']
libraries=[build/p for p in ['_deps/assimp-build/lib/assimp-vc143-mt.lib','_deps/assimp-build/contrib/zlib/zlibstatic.lib','StbImage.lib','engine/ImGuizmo.lib','engine/DearImGui.lib','_deps/glfw-build/src/glfw3.lib','_deps/enkits-build/enkiTS.lib','_deps/tracy-build/TracyClient.lib']]+[root/'external/dxc/lib/x64/dxcompiler.lib']
quote=lambda p:'"'+p.as_posix()+'"'
cm='cmake_minimum_required(VERSION 3.20)\nproject(Editor22Baseline LANGUAGES CXX)\nset(CMAKE_CXX_STANDARD 20)\nadd_library(BaselineEngine STATIC\n'+''.join(' engine/'+p+'\n' for p in sources)+')\n'
cm+='target_compile_definitions(BaselineEngine PUBLIC ENABLE_DX12=1 TRACY_ENABLE TRACY_ON_DEMAND)\n'
cm+='target_include_directories(BaselineEngine PUBLIC '+' '.join(map(quote,includes))+')\n'
cm+='target_link_libraries(BaselineEngine PUBLIC '+' '.join(map(quote,libraries))+' d3d12 dxgi dxguid d3dcompiler)\nadd_executable(EditorBaseline WhispEngine.cpp)\ntarget_link_libraries(EditorBaseline PRIVATE BaselineEngine)\n'
(target/'CMakeLists.txt').write_text(cm,encoding='utf-8')
(out/'baseline-instrumentation.txt').write_text('Editor 2.1 = HEAD plus before-tree.zip. Only CSV/browser timer and diagnostic viewport/CLI/profile controls added. Original renderer/shaders/theme/Inspector/assets retained; dependencies shared unchanged. Run with baseline-runtime cwd (original assets/shaders).\n',encoding='utf-8')
print(target)
