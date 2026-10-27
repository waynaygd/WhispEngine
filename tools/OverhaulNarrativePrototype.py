"""Update this existing example's presentation; preserve its authored story/data."""
from pathlib import Path
import json,math
from PIL import Image,ImageDraw
P=Path(__file__).resolve().parents[1]/'examples/DiscoElysium2'
def read(key):return json.loads((P/key).read_text(encoding='utf-8'))
def write(key,value):
 path=P/key;path.parent.mkdir(parents=True,exist_ok=True)
 path.write_text(json.dumps(value,ensure_ascii=False,indent=2) if not isinstance(value,str) else value,encoding='utf-8')
widgets=[]
def w(id,type='Panel',parent='Root',pos=(0,0),size=(100,40),text='',color=(.075,.095,.095,.98),**kw):
 if type=='Button':kw=dict(hoverColor=[.29,.31,.26,1],pressedColor=[.35,.29,.18,1],transitionDuration=.12)|kw
 widgets.append(dict(id=id,type=type,parent=parent,position=pos,size=size,text=text,color=color,**(dict(fontSize=18)|kw)))
w('Root',parent='',size=(0,0),anchorMax=[1,1],color=(0,0,0,0))
w('HUD',pos=(18,-18),size=(310,102),anchorMin=[0,1],anchorMax=[0,1],pivot=[0,1],hitTest=True)
w('PlayerPortrait','Image','HUD',(9,9),(68,84),texture='textures/player.png',color=(1,1,1,1))
w('PlayerName','Text','HUD',(90,10),(210,24),'ЛЕВ ОРИН',(.84,.70,.46,1),fontSize=19)
w('PlayerRole','Text','HUD',(90,36),(210,20),'Участковый следователь',(.52,.62,.60,1),fontSize=14)
for id,x,text in [('JournalToggle',90,'Журнал'),('SkillsToggle',194,'Навыки')]:
 w(id,'Button','HUD',(x,64),(98,28),color=(.17,.21,.20,1));w(id+'Text','Text',id,(9,5),(90,22),text,(.85,.81,.69,1),fontSize=16)
w('Status','Text',pos=(26,-132),size=(650,52),anchorMin=[0,1],anchorMax=[0,1],color=(.83,.79,.63,1),fontSize=17)
w('Destination','Image',size=(26,18),texture='textures/destination.png',color=(.72,.86,.76,1),visible=True,opacity=0,zOrder=-2)
w('Dialogue',pos=(-16,16),size=(440,-32),anchorMin=[1,0],anchorMax=[1,1],pivot=[1,0],visible=False,hitTest=True,clipChildren=True)
w('Portrait','Image','Dialogue',(18,18),(74,94),texture='textures/keeper.png',color=(1,1,1,1))
w('Speaker','Text','Dialogue',(108,22),(270,80),color=(.86,.70,.43,1),fontSize=21)
w('CloseDialogue','Button','Dialogue',(-10,10),(28,28),anchorMin=[1,0],anchorMax=[1,0],pivot=[1,0],color=(.18,.20,.18,1))
w('CloseLabel','Text','CloseDialogue',(8,3),(20,22),'×',(.82,.75,.60,1),fontSize=22)
w('ConversationLabel','Text','Dialogue',(18,120),(404,24),'РАЗГОВОР',(.48,.57,.53,1),fontSize=14)
w('HistoryList',parent='Dialogue',pos=(18,150),size=(-36,-166),anchorMax=[1,.60],clipChildren=True,scrollable=True,color=(.065,.085,.085,1))
w('Responses',parent='Dialogue',pos=(18,10),size=(-36,-86),anchorMin=[0,.60],anchorMax=[1,1],clipChildren=True,scrollable=True,color=(.065,.085,.085,1))
w('Feedback','Text','Dialogue',(20,-75),size=(404,54),anchorMin=[0,1],anchorMax=[0,1],color=(.60,.78,.68,1),fontSize=16)
w('ScrollHint','Text','Dialogue',(20,-20),size=(404,19),anchorMin=[0,1],anchorMax=[0,1],text='Колесо мыши — история / варианты',color=(.42,.50,.46,1),fontSize=13)
for id,title,h in [('Journal','ДЕЛО О ПРОПАВШЕМ СВЕТЕ',225),('Skills','НАВЫКИ',210)]:
 w(id,pos=(18,-138),size=(370,h),anchorMin=[0,1],anchorMax=[0,1],pivot=[0,1],visible=False,hitTest=True,zOrder=2)
 w(id+'Title','Text',id,(16,15),(340,29),title,(.84,.70,.46,1),fontSize=18)
 w('Quest' if id=='Journal' else 'SkillsText','Text',id,(16,60),(338,h-70),color=(.78,.81,.75,1),fontSize=18)
write('Assets/ui/harbor.ui.json',dict(version=1,name='Harbor Investigation',root='Root',referenceResolution=[1280,800],scaleMode=0,widgets=widgets))
# Original compact portrait and destination confirmation, matching the existing palette.
im=Image.new('RGBA',(128,160),(26,38,39,255));d=ImageDraw.Draw(im)
d.ellipse((10,10,118,139),fill=(53,65,62));d.polygon([(5,160),(23,119),(50,104),(78,104),(107,124),(124,160)],fill=(49,71,74))
d.polygon([(43,45),(55,29),(86,36),(94,66),(82,98),(59,101),(43,81)],fill=(177,147,113))
d.polygon([(40,57),(39,39),(58,21),(87,28),(98,48),(92,62),(79,43),(52,45)],fill=(53,48,42))
d.line((50,66,60,66),fill=(48,45,39),width=2);d.line((76,65,86,66),fill=(48,45,39),width=2);d.line((61,86,80,85),fill=(92,62,52),width=2)
im.save(P/'Assets/textures/player.png')
im=Image.new('RGBA',(64,40),(0,0,0,0));d=ImageDraw.Draw(im);d.ellipse((3,6,60,33),outline=(255,255,255,255),width=3);d.line((26,20,38,20),fill='white',width=2);d.line((32,15,32,25),fill='white',width=2);im.save(P/'Assets/textures/destination.png')
# Tapered garment and faceted head mesh, authored here rather than downloaded.
def rings_mesh(rings,sides):
 vertices=[];faces=[]
 for y,rx,rz in rings:
  for i in range(sides):
   a=2*math.pi*i/sides;vertices.append((math.sin(a)*rx,y,math.cos(a)*rz))
 for level in range(len(rings)-1):
  for i in range(sides):
   a=level*sides+i;b=level*sides+(i+1)%sides;faces.append((a,b,b+sides,a+sides))
 faces+=[tuple(reversed(range(sides))),tuple((len(rings)-1)*sides+i for i in range(sides))]
 # Independent flat normals per face, preserving the intended low-poly silhouette.
 out='';index=1
 for face in faces:
  points=[vertices[i] for i in face];a,b,c=points[:3];u=[b[i]-a[i] for i in range(3)];v=[c[i]-a[i] for i in range(3)];n=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]];length=math.sqrt(sum(x*x for x in n));n=[x/length for x in n]
  out+=''.join('v %.5f %.5f %.5f\n'%p for p in points)+'vn %.5f %.5f %.5f\n'%tuple(n)
  normal=faces.index(face)+1;out+='f '+' '.join(f'{index+i}//{normal}' for i in range(len(points)))+'\n';index+=len(points)
 return out
write('Assets/models/coat.obj',rings_mesh([(-.38,.31,.22),(.05,.23,.17),(.52,.35,.22),(.65,.20,.17)],8))
write('Assets/models/face.obj',rings_mesh([(-.22,.09,.08),(-.12,.17,.14),(.12,.17,.145),(.22,.10,.10)],8))
write('Assets/models/post.obj',rings_mesh([(-.5,.35,.35),(.4,.35,.35),(.5,.29,.29)],10))
scene=read('Scenes/Harbor.json');scene['entities']=[e for e in scene['entities'] if not e['id'].startswith('Paving') and not e['id'].startswith('Overhaul')]
byid={e['id']:e for e in scene['entities']}
byid['Camera']['components']['Transform']['position']=[-15,15,-20]
byid['Camera']['components']['Camera']['orthographicSize']=20
def part(id,parent,pos,scale,color,mesh='harbor_cube.obj',**extra):
 c=dict(Tag=dict(name=id),Transform=dict(position=pos,rotation=[0,0,0],scale=scale),MeshRenderer=dict(meshPath='models/'+mesh,visible=True),Material=dict(materialPath='materials/harbor.material.json',tint=list(color)+[1]));c.update(extra)
 e=dict(id=id,components=c)
 if parent:e['parent']=parent
 scene['entities'].append(e);return e
for tag in ['Detective','Keeper','Radio','Cashier']:
 actor=byid[tag]['components'];actor['Transform']['scale']=[1,1,1];actor['MeshRenderer']['meshPath']='models/coat.obj'
 head=byid[tag+'Head']['components'];head['Transform'].update(position=[0,.88,0],scale=[1,1,1]);head['MeshRenderer']['meshPath']='models/face.obj'
 for side in [-1,1]:
  part(f'Overhaul{tag}Leg{side}',tag,[side*.14,-.60,0],[.18,.50,.21],[.10,.14,.14])
  part(f'Overhaul{tag}Boot{side}',tag,[side*.14,-.83,.04],[.20,.12,.31],[.07,.09,.09])
  arm=part(f'Overhaul{tag}Sleeve{side}',tag,[side*.34,.24,0],[.16,.58,.20],actor['Material']['tint'][:3]);arm['components']['Transform']['rotation'][2]=side*.12
  part(f'Overhaul{tag}Hand{side}',tag,[side*.37,-.08,.02],[.13,.17,.14],[.65,.50,.36],mesh='face.obj')
 part(f'Overhaul{tag}Nose',tag,[0,.87,.15],[.09,.10,.12],[.68,.53,.40])
 part(f'Overhaul{tag}Collar',tag,[0,.66,.04],[.22,.15,.25],[.60,.58,.48])
 part(f'Overhaul{tag}Hat',tag,[0,1.07,0],[.40,.08,.35],[.13,.18,.17])
byid['Sea']['components']['Transform']['scale']=[250,.25,250]
byid['Quay']['components']['Material']['tint']=[.24,.28,.27,1]
byid['Office']['components']['Material']['tint']=[.31,.33,.30,1]
byid['OfficeRoof']['components']['Material']['tint']=[.18,.24,.23,1]
for e in scene['entities']:
 if e['id'].startswith('Bollard'):
  e['components']['MeshRenderer']['meshPath']='models/post.obj'
# One textured surface replaces dozens of decorative pavement cubes.
write('Assets/models/paving.obj','v -9.5 0 -8.5\nv -9.5 0 8.5\nv 9.5 0 8.5\nv 9.5 0 -8.5\nvt 0 0\nvt 0 8.5\nvt 9.5 8.5\nvt 9.5 0\nvn 0 1 0\nf 1/1/1 2/2/1 3/3/1 4/4/1\n')
im=Image.new('RGBA',(128,128));pixels=im.load()
for y in range(128):
 for x in range(128):
  grain=((x*17+y*31+x*y*3)%11)-5
  value=76 if x<2 or y<2 else 103+grain
  pixels[x,y]=(value,value+5,value-2,255)
im.save(P/'Assets/textures/paving.png')
write('Assets/materials/paving.material.json',dict(name='Worn harbor paving',shaderPath='dx12/textured.hlsl',texturePath='textures/paving.png',baseColor=[1,1,1,1],roughness=.94))
surface=part('OverhaulPaving',None,[0,.012,0],[1,1,1],[1,1,1],mesh='paving.obj')
surface['components']['Material']['materialPath']='materials/paving.material.json'
# Cargo bindings, bench slats and the radio's controls give the existing props a purpose.
for x in [-.8,.8]:part('OverhaulCrateBand'+str(x),None,[x,1.612,0],[.10,.025,2.4],[.20,.23,.19])
for z in [-.22,0,.22]:part('OverhaulBenchSlat'+str(z),None,[-3,.82,-1+z],[2,.07,.15],[.44,.32,.19])
part('OverhaulRadio',None,[5,1.28,3],[.72,.36,.36],[.13,.20,.19])
part('OverhaulRadioDial',None,[4.8,1.30,2.80],[.14,.12,.03],[.70,.59,.35])
part('OverhaulAntenna',None,[5.2,1.85,3],[.025,.85,.025],[.25,.30,.28])
for x,z in [(-5.5,4),(6,4)]:
 part(f'OverhaulLamp{x}',None,[x,1.8,z],[.10,3.6,.10],[.16,.21,.20])
 part(f'OverhaulLampHead{x}',None,[x,3.6,z],[.55,.22,.55],[.68,.55,.32])
 light=dict(id=f'OverhaulLight{x}',components=dict(Tag=dict(name='Harbor lamp'),Transform=dict(position=[x,3.4,z],rotation=[0,0,0],scale=[1,1,1]),PointLight=dict(color=[1,.75,.42],intensity=8,range=7)))
 scene['entities'].append(light)
scene['settings']['ambient']=dict(color=[.59,.68,.70],intensity=.38)
byid['HarborSun']['components']['DirectionalLight']['intensity']=1.7
write('Scenes/Harbor.json',scene)
# Six substantive questions form a real seven-choice node, preserving existing branch IDs.
graph=read('Assets/data/keeper.data.json');nodes=graph['value']['nodes'];ids={n['id'] for n in nodes}
def choice(text,next):return dict(text=text,next=next,requires='',excludes='',flag='',check='',difficulty=0,success='',failure='',quest='')
if 'harbor_questions' not in ids:
 nodes[0]['choices'].append(choice('Расскажите подробнее о причале.','harbor_questions'))
 topics=[('Когда погас буй?','Чуть после трёх. В журнале есть отметка, но я тогда проверяла склад.'),('Кто ждёт паром?','Рабочие с другого берега. Утром без света их рейс отменят.'),('Почему фонарь нельзя заменить?','Запасной отправили на ремонт. В кассе лежит заявка, которой уже месяц.'),('Что случилось с радиостанцией?','Она пережила этот причал. Илья каждый вечер чинит контакты заново.'),('Можно осмотреть контору?','Да. Но начните с мокрой квитанции: её нашли перед грузовыми ящиками.'),('Кому принадлежит лодка?','Рыбакам с восточного берега. Они шли домой в тумане.')]
 nodes.append(dict(id='harbor_questions',speaker='Марта Рейн',portrait='textures/keeper.png',text='Спрашивайте. Здесь всё связано с водой, долгами и плохо выполненной работой.',choices=[choice(q,'harbor_topic_'+str(i)) for i,(q,a) in enumerate(topics)]+[choice('Вернуться к делу.','start')]))
 for i,(q,a) in enumerate(topics):nodes.append(dict(id='harbor_topic_'+str(i),speaker='Марта Рейн',portrait='textures/keeper.png',text=a,choices=[choice('У меня ещё вопросы.','harbor_questions')]))
write('Assets/data/keeper.data.json',graph)
print('Updated existing scene, HUD, scrollable dialogue column and additional questions:',P)
