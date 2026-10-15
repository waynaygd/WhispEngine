from pathlib import Path
import subprocess,re,argparse
parser=argparse.ArgumentParser();parser.add_argument("--ref",required=True);args=parser.parse_args()
root=Path(__file__).resolve().parents[1]; target=root/'out/diagnostics/editor2/baseline-source';target.mkdir(exist_ok=True)
for name in subprocess.check_output(['git','ls-tree','-r','--name-only',args.ref,'engine','WhispEngine.cpp'],cwd=root).decode().splitlines():
    p=target/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(subprocess.check_output(['git','show',args.ref+':'+name],cwd=root))
def patch(name,old,new):
    p=target/name;s=p.read_bytes().decode('cp1251').replace('\r\n','\n');assert old in s,name;p.write_bytes(s.replace(old,new).replace('\n','\r\n').encode('cp1251'))
patch('engine/editor/EditorLayer.h','public:','public:\n    struct Statistics {double uiMs=0,hierarchyMs=0,inspectorMs=0;};\n    Statistics statistics;\n    const Statistics& GetStatistics()const{return statistics;}')
patch('engine/editor/EditorLayer.cpp','#include "EditorLayer.h"','#include "EditorLayer.h"\n#include <chrono>\nnamespace { struct BaselineTimer {double& value;std::chrono::steady_clock::time_point begin=std::chrono::steady_clock::now();~BaselineTimer(){value=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();}}; }')
patch('engine/editor/EditorLayer.cpp','ZoneScopedN("EditorLayer");','ZoneScopedN("EditorLayer");\n    statistics.hierarchyMs=statistics.inspectorMs=0;BaselineTimer uiTimer{statistics.uiMs};')
for name,member in [('DrawSceneHierarchy','hierarchyMs'),('DrawInspector','inspectorMs')]:
    patch('engine/editor/EditorLayer.cpp',f'void EditorLayer::{name}(Application& app)\n{{',f'void EditorLayer::{name}(Application& app)\n{{\n    BaselineTimer panelTimer{{statistics.{member}}};')
# Identical CSV instrumentation, no baseline behavior changes.
(target/'engine/core/RenderDiagnosticSample.h').write_bytes((root/'engine/core/RenderDiagnosticSample.h').read_bytes())
patch('engine/core/Application.cpp','GetLightDiagnostics()});','GetLightDiagnostics(),m_EditorLayer.GetStatistics().uiMs,m_EditorLayer.GetStatistics().hierarchyMs,m_EditorLayer.GetStatistics().inspectorMs,unsigned(m_EditorLayer.GetViewportPixelWidth()),unsigned(m_EditorLayer.GetViewportPixelHeight())});')
cm=(target/'engine/CMakeLists.txt').read_bytes().decode('cp1251')
sources=[]
for s in re.findall(r'[\w/]+\.cpp',cm):
    if s.startswith('render/backends/vulkan') or s.startswith('external/') or not (target/'engine'/s).is_file():continue
    if s not in sources:sources.append(s)
deps=root/'out/build/x64-release/_deps';build=root/'out/build/shadows-dx12'
includes=[target/'engine',target/'engine/external',target/'engine/third_party',root/'external/imgui',root/'external/imgui/backends',root/'external/imguizmo/src',root/'external/dxc/include',deps/'glfw-src/include',deps/'assimp-src/include',build/'_deps/assimp-build/include',deps/'enkits-src/src',deps/'tracy-src/public',deps/'stb-src']
libraries=[build/x for x in ['_deps/assimp-build/lib/assimp-vc143-mt.lib','_deps/assimp-build/contrib/zlib/zlibstatic.lib','StbImage.lib','engine/ImGuizmo.lib','engine/DearImGui.lib','_deps/glfw-build/src/glfw3.lib','_deps/enkits-build/enkiTS.lib','_deps/tracy-build/TracyClient.lib']]+[root/'external/dxc/lib/x64/dxcompiler.lib']
quote=lambda p:'"'+p.as_posix()+'"'
text='cmake_minimum_required(VERSION 3.20)\nproject(EditorBaseline LANGUAGES CXX)\nset(CMAKE_CXX_STANDARD 20)\nadd_library(BaselineEngine STATIC\n'+''.join(' engine/'+s+'\n' for s in sources)+')\n'
text+='target_compile_definitions(BaselineEngine PUBLIC ENABLE_DX12=1 TRACY_ENABLE TRACY_ON_DEMAND)\n'
text+='target_include_directories(BaselineEngine PUBLIC '+ ' '.join(map(quote,includes))+')\n'
text+='target_link_libraries(BaselineEngine PUBLIC '+' '.join(map(quote,libraries))+' d3d12 dxgi dxguid d3dcompiler)\nadd_executable(EditorBaseline WhispEngine.cpp)\ntarget_link_libraries(EditorBaseline PRIVATE BaselineEngine)\n'
(target/'CMakeLists.txt').write_text(text,encoding='utf-8')
(root/'out/diagnostics/editor2/baseline-instrumentation.txt').write_text('HEAD snapshot '+subprocess.check_output(['git','rev-parse',args.ref],cwd=root).decode().strip()+'; only chrono UI/panel measurements and CSV fields added. Shared unchanged dependency libraries, original Engine/Application/World sources. Executed from normal DX12 build cwd to share exact assets/shaders.\n',encoding='utf-8')

patch("engine/scene/SceneSerializer.cpp", "    std::ofstream file(path);", "    if(path.empty())return true; // Measurement-only v3 JSON capture, excludes disk IO.\n    std::ofstream file(path);")
(target/"BaselineCpu.cpp").write_text('#include "core/Application.h"\n#include "scene/SceneSerializer.h"\n#include <fstream>\n#include <chrono>\nint main(int argc,char** argv){\n    if(argc<2)return 1;std::ofstream output(argv[1]);output<<"entities,iteration,serializeMs,cloneMs,stopMs,transactionMs\\n";\n    using Clock=std::chrono::steady_clock;auto ms=[](auto t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();};\n    for(int count:{100,500,1000}){\n        Application app;auto& ui=app.GetEditorLayer();for(int i=0;i<count;++i){auto e=app.GetWorld().CreateEntity();app.GetWorld().AddComponent<ecs::TagComponent>(e).name="Benchmark entity";app.GetWorld().AddComponent<ecs::TransformComponent>(e);app.GetWorld().AddComponent<ecs::MeshRendererComponent>(e).meshPath="models/validation_cube.obj";app.GetWorld().AddComponent<ecs::MaterialComponent>(e);}\n        ecs::Entity entity;app.GetWorld().ForEach<ecs::TransformComponent>([&](auto e,auto&){if(!entity.IsValid())entity=e;});ui.SelectEntity(entity);\n        for(int i=0;i<25;++i){auto start=Clock::now();if(!SceneSerializer::SaveWorld({},app.GetWorld()))return 2;auto serialization=ms(start);\n            start=Clock::now();app.SetEditorPlayMode(true);auto clone=ms(start);start=Clock::now();app.SetEditorPlayMode(false);auto stop=ms(start);\n            start=Clock::now();for(int step=0;step<100;++step){ui.RecordEdit(app,"Position movement");app.GetWorld().GetComponent<ecs::TransformComponent>(entity)->position.x=float(i*100+step);}auto transaction=ms(start);\n            output<<count<<\',\'<<i<<\',\'<<serialization<<\',\'<<clone<<\',\'<<stop<<\',\'<<transaction<<\'\\n\';\n        }app.Shutdown();\n    }\n}\n',encoding="utf-8")
with (target/"CMakeLists.txt").open("a",encoding="utf-8") as cm: cm.write("\nadd_executable(BaselineCpu BaselineCpu.cpp)\ntarget_link_libraries(BaselineCpu PRIVATE BaselineEngine)\n")
