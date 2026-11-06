"""Run inside official qrenderdoc --python (embedded RenderDoc Python)."""
import renderdoc as rd
import pathlib, json, traceback, os, hashlib
root=pathlib.Path(os.environ.get('WHISP_MATERIAL_EVIDENCE','out/lab3-evidence')).resolve()/'captures'
def describe(value,depth=0):
 if value is None or isinstance(value,(str,int,float,bool)):return value
 if depth>3:return str(value)
 try:
  if not isinstance(value,dict):return [describe(v,depth+1) for v in value]
 except TypeError:pass
 fields={}
 for name in dir(value):
  if name.startswith('_'):continue
  try:
   v=getattr(value,name)
   if not callable(v):fields[name]=describe(v,depth+1)
  except Exception:pass
 return fields if fields else str(value)
try:
 results=[]
 captures=sorted(root.glob('*.rdc'))
 if len(captures)!=2:raise RuntimeError('Expected two actual RDCs, found '+str(len(captures)))
 for path in captures:
  result={'path':str(path)}
  cap=rd.OpenCaptureFile()
  opened=cap.OpenFile(str(path),'',None);result['open']=str(opened)
  if opened!=rd.ResultCode.Succeeded:raise RuntimeError(str(opened))
  result['localReplaySupport']=str(cap.LocalReplaySupport())
  status,controller=cap.OpenCapture(rd.ReplayOptions(),None);result['replay']=str(status)
  if status!=rd.ResultCode.Succeeded:raise RuntimeError(str(status))
  def walk(actions):
   for action in actions:
    yield action
    for child in walk(action.children):yield child
  draws=[a for a in walk(controller.GetRootActions()) if a.flags & rd.ActionFlags.Drawcall]
  result['drawCount']=len(draws)
  result['draws']=[{'eventId':a.eventId,'indices':a.numIndices,'instances':a.numInstances,'name':a.GetName(controller.GetStructuredFile())} for a in draws]
  mesh=next(a for a in draws if a.numIndices==36)
  controller.SetFrameEvent(mesh.eventId,True)
  pipe=controller.GetPipelineState();dx=controller.GetD3D12PipelineState()
  result['meshEvent']=mesh.eventId
  result['pipelineResourceId']=str(dx.pipelineResourceId)
  result['shaders']={str(s):str(pipe.GetShader(s)) for s in [rd.ShaderStage.Vertex,rd.ShaderStage.Pixel]}
  result['shaderBytecodeSha256']={str(s):hashlib.sha256(bytes(pipe.GetShaderReflection(s).rawBytes)).hexdigest() for s in [rd.ShaderStage.Vertex,rd.ShaderStage.Pixel]}
  result['readOnlyResources']={str(s):describe(pipe.GetReadOnlyResources(s)) for s in [rd.ShaderStage.Vertex,rd.ShaderStage.Pixel]}
  result['outputTargets']=describe(pipe.GetOutputTargets())
  result['rootSignature']=describe(dx.rootSignature)
  result['rasterizer']=describe(dx.rasterizer)
  result['outputMerger']=describe(dx.outputMerger)
  result['resources']=[{'id':str(r.resourceId),'name':r.name} for r in controller.GetResources()]
  result['textures']=[{'id':str(t.resourceId),'width':t.width,'height':t.height,'format':t.format.Name()} for t in controller.GetTextures()]
  controller.SetFrameEvent(draws[-1].eventId,True)
  save=rd.TextureSave();save.resourceId=controller.GetPipelineState().GetOutputTargets()[0].resource;save.destType=rd.FileType.PNG
  result['viewportExport']=str(controller.SaveTexture(save,str(root/(path.stem+'-viewport.png'))))
  result['pipelineMembers']=dir(dx)
  controller.Shutdown();cap.Shutdown();results.append(result)
 (root/'replay.json').write_text(json.dumps(results,indent=2))
except Exception:
 (root/'replay-error.txt').write_text(traceback.format_exc())
os._exit(0)
