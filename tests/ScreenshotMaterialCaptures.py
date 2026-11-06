"""Official qrenderdoc --python script; delayed callbacks capture actual UI widgets."""
import pathlib, json, os, traceback
import renderdoc as rd
import qrenderdoc as qrd
from PySide2 import QtCore, QtWidgets
root=pathlib.Path(os.environ.get('WHISP_MATERIAL_EVIDENCE','out/lab3-evidence')).resolve()/'captures'
captures=iter(json.loads((root/'replay.json').read_text()))
active=None
def next_capture():
 global active
 try:
  active=next(captures)
  pyrenderdoc.LoadCapture(active['path'],rd.ReplayOptions(),active['path'],False,True)
  QtCore.QTimer.singleShot(2000,save)
 except StopIteration:os._exit(0)
 except Exception:
  (root/'screenshot-error.txt').write_text(traceback.format_exc());os._exit(1)
def save():
 try:
  if not pyrenderdoc.IsCaptureLoaded():raise RuntimeError('Capture UI not loaded')
  eid=active['meshEvent'];pyrenderdoc.SetEventID([],eid,eid,True)
  pyrenderdoc.ShowEventBrowser();pyrenderdoc.ShowPipelineViewer()
  pyrenderdoc.GetPipelineViewer().SelectPipelineStage(qrd.PipelineStage.PixelShader)
  QtCore.QTimer.singleShot(1500,grab)
 except Exception:
  (root/'screenshot-error.txt').write_text(traceback.format_exc());os._exit(1)
def grab():
 try:
  stem=pathlib.Path(active['path']).stem
  for name,widget in [('events',pyrenderdoc.GetEventBrowser().Widget()),('pipeline',pyrenderdoc.GetPipelineViewer().Widget()),('window',pyrenderdoc.GetMainWindow().Widget())]:
   if not widget.grab().save(str(root/(stem+'-'+name+'.png'))):raise RuntimeError('Screenshot save failed')
  pyrenderdoc.CloseCapture();QtCore.QTimer.singleShot(500,next_capture)
 except Exception:
  (root/'screenshot-error.txt').write_text(traceback.format_exc());os._exit(1)
QtCore.QTimer.singleShot(500,next_capture)
