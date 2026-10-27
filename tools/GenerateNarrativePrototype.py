"""Reproducible original WhispEngine example; no downloads or user project edits."""
from pathlib import Path
import json, math, shutil
from PIL import Image, ImageDraw
ROOT=Path(__file__).resolve().parents[1]
P=ROOT/'examples/DiscoElysium2'
def write(name,value):
 p=P/name;p.parent.mkdir(parents=True,exist_ok=True)
 p.write_text(json.dumps(value,ensure_ascii=False,indent=2) if not isinstance(value,str) else value,encoding='utf-8')
def schema(t,**kw):return dict(type=t,**kw)
S=schema('String');N=schema('Number');B=schema('Boolean')
def obj(**fields):return schema('Object',fields=fields)
def arr(item):return schema('Array',items=item)
def asset(name,typ,sch,value,table=False):
 write('Assets/data/'+name+('.table.json' if table else '.data.json'),dict(version=1,kind='DataTable' if table else 'DataAsset',type=typ,schema=sch,**({'rows':value} if table else {'value':value})))
choice_schema=obj(text=S,next=S,requires=S,excludes=S,flag=S,check=S,difficulty=N,success=S,failure=S,quest=S)
node_schema=obj(id=S,speaker=S,portrait=schema('Resource',suffix='.png'),text=S,choices=arr(choice_schema))
def choice(text,next='',**kw):return dict(text=text,next=next,requires='',excludes='',flag='',check='',difficulty=0,success='',failure='',quest='',**{})|kw
def node(id,speaker,text,choices,portrait):return dict(id=id,speaker=speaker,text=text,choices=choices,portrait=f'textures/{portrait}.png')
def graph(name,nodes):asset(name,'DialogueGraph',obj(start=S,nodes=arr(node_schema)),dict(start='start',nodes=nodes))
graph('keeper',[
 node('start','Марта Рейн • смотрительница','Сигнальный фонарь исчез перед утренним рейсом. Без него паром не выйдет. Кто-то оставил на причале мокрую квитанцию. Найдите фонарь, пока туман не закрыл бухту.',[
  choice('Я разберусь. Кто был здесь ночью?','witnesses',flag='case_started',quest='Осмотрите квитанцию у ящиков. Поговорите с Ильёй и Ниной.'),
  choice('Мы нашли объяснение. Поговорим о результате.','report',requires='confession'),choice('Пока всё.')],'keeper'),
 node('witnesses','Марта Рейн','Илья чинил радиостанцию. Нина сторожила кассу. Я бы не торопилась с обвинениями: здесь каждый когда-нибудь спасался чужим светом.',[choice('Вернусь с доказательствами.')],'keeper'),
 node('report','Марта Рейн','Фонарь у Ильи? Значит, вы можете написать рапорт или дать ему шанс исправить ошибку.',[
  choice('Дать шанс: вернуть свет и починить аварийный буй.','mercy',flag='ending_mercy',quest='Завершено: свет возвращён. Илья восстановит аварийный буй.'),
  choice('Записать всё в рапорт. Ответственность должна быть общей.','report_end',flag='ending_report',quest='Завершено: официальный рапорт. Фонарь вернётся с первым патрулём.')],'keeper'),
 node('mercy','Марта Рейн','К полудню на воде снова появится свет. Иногда расследование заканчивается не именем виновного, а работой, которую он согласился сделать.',[choice('Закрыть дело.')],'keeper'),
 node('report_end','Марта Рейн','Я подпишу. Пусть город впервые оплатит ремонт, вместо того чтобы искать удобного виноватого. Это тоже способ вернуть свет.',[choice('Закрыть дело.')],'keeper')])
graph('radio',[
 node('start','Илья Ветров • радист','Я не спал всю ночь. Радио ловило только треск. Фонарь? Не видел. И не понимаю, зачем вам эта промокшая бумага.',[
  choice('Сопоставить номер на квитанции с записью в журнале. [Логика 9]','',requires='receipt',excludes='checked',flag='checked',check='radio_logic',difficulty=0,success='admit',failure='deny'),
  choice('Нина видела свет у аварийного буя. Я пришёл выслушать.','admit',requires='testimony'),choice('Ещё вернусь.')],'radio'),
 node('admit','Илья Ветров','Ладно. Буй погас, а в тумане шла лодка. Я взял фонарь и отнёс туда. Потом испугался, что меня уволят. Он в моей сумке. Помогите объяснить Марте.',[choice('Я передам ей правду.','',flag='confession',quest='Вернитесь к Марте. Решите, как завершить дело.')],'radio'),
 node('deny','Илья Ветров','Номер расплылся. Это мог быть любой рейс. Вы не уверены в своей версии. Спросите у Нины: она видела больше меня.',[choice('Поговорю с Ниной.','',quest='Проверка не удалась. Показания Нины дадут другой путь к правде.')],'radio')])
graph('cashier',[
 node('start','Нина Соль • кассир','Смена закончилась, а я всё считаю пустые билеты. Да, я видела Илью с фонарём. Он шёл к воде, а не к выходу.',[
  choice('Почему вы сразу не сказали?','truth'),choice('Мне нужно подумать.')],'cashier'),
 node('truth','Нина Соль','Потому что лодка вышла из тумана целой. Он нарушил правило и спас людей. Скажите ему: правда уже не страшнее молчания.',[choice('Спасибо. Это меняет дело.','',flag='testimony',quest='Нина подтверждает: фонарь спас лодку. Поговорите с Ильёй.')],'cashier')])
graph('receipt',[node('start','Квитанция','Синяя бумага прилипла к ящику. «Аварийный буй № 9. Выдача: И. Ветров». Чернила расплылись, но номер читается.',[choice('Забрать как улику.','',flag='receipt',quest='Квитанция указывает на аварийный буй. Спросите радиста Илью.')],'receipt')])
graph('ledger',[node('start','Сменный журнал','В журнале записано: «03:10 — буй № 9 не отвечает. 03:18 — лодка прошла канал». Вы отмечаете время: оно пригодится при проверке версии.',[choice('Запомнить время.','',flag='ledger',quest='Журнал даёт +2 к Логике. Сопоставьте его с квитанцией.')],'receipt')])
asset('skills','Skills',obj(id=S,name=S,value=schema('Number',min=0,max=10),description=S),[
 dict(id='logic',name='Логика',value=4,description='Сопоставлять факты; журнал даёт +2.'),dict(id='empathy',name='Эмпатия',value=5,description='Понимать причины поступков.'),dict(id='perception',name='Наблюдательность',value=3,description='Замечать следы в тумане.')],True)
asset('items','Items',obj(id=S,name=S,description=S),[dict(id='receipt',name='Мокрая квитанция',description='Улика с номером аварийного буя.'),dict(id='ledger',name='Запись в журнале',description='Уточняет время: +2 к Логике.')],True)
asset('checks','SkillChecks',obj(id=S,skill=S,difficulty=N,bonusFlag=S,bonus=N),[dict(id='radio_logic',skill='logic',difficulty=9,bonusFlag='ledger',bonus=2)],True)
asset('quest','QuestDefinition',obj(id=S,title=S,description=S),dict(id='lost_light',title='Дело о пропавшем свете',description='Найдите фонарь, выслушайте свидетелей и решите судьбу рапорта.'))
asset('stats','CharacterStats',obj(name=S,coat=schema('Color'),origin=schema('Vec3'),role=schema('Enum',values=['detective','witness']),active=B),dict(name='Следователь Лев Орин',coat=[.14,.25,.3,1],origin=[0,0,-4],role='detective',active=True))
for id,name,tag in [('keeper','Марта Рейн','Keeper'),('radio','Илья Ветров','Radio'),('cashier','Нина Соль','Cashier'),('receipt','Квитанция','Receipt'),('ledger','Журнал','Ledger')]:
 asset(id+'_npc','NPCDefinition',obj(id=S,name=S,tag=S,dialogue=schema('Resource',suffix='.data.json'),portrait=schema('Resource',suffix='.png'),range=N),dict(id=id,name=name,tag=tag,dialogue=f'data/{id}.data.json',portrait=f'textures/{id if id not in ["ledger"] else "receipt"}.png',range=1.8))
asset('interactables','InteractableCatalog',obj(definitions=arr(schema('Resource',suffix='.data.json'))),dict(definitions=[f'data/{n}_npc.data.json' for n in ['keeper','radio','cashier','receipt','ledger']]))
# Original geometric portrait illustrations, drawn from mathematical shapes.
for id,coat,hair in [('keeper',(133,92,68),(191,191,166)),('radio',(54,95,102),(48,44,39)),('cashier',(104,71,94),(52,37,44)),('receipt',(113,96,65),(105,102,83))]:
 im=Image.new('RGBA',(192,240));d=ImageDraw.Draw(im)
 for y in range(240):d.line((0,y,192,y),fill=(30+y//8,43+y//10,47+y//12,255))
 d.ellipse((15,22,175,182),fill=(64,78,78));d.polygon([(8,240),(30,168),(76,147),(115,147),(162,175),(185,240)],fill=coat)
 d.polygon([(68,130),(70,164),(95,189),(120,162),(120,127)],fill=(175,132,101));d.polygon([(53,65),(65,35),(119,31),(141,64),(132,126),(112,151),(83,146),(59,120)],fill=(193,155,119))
 d.polygon([(51,83),(47,53),(70,24),(123,26),(144,54),(141,83),(125,59),(97,47),(64,63)],fill=hair)
 d.line((68,91,82,89),fill=(41,45,45),width=3);d.line((108,89,124,92),fill=(41,45,45),width=3);d.line((97,96,92,116,101,117),fill=(135,92,76),width=2);d.line((83,129,109,129),fill=(103,68,60),width=2)
 if id=='receipt':d.rectangle((36,48,151,190),fill=(190,184,145));[d.line((52,y,136,y-4),fill=(83,102,107),width=3) for y in range(75,173,19)]
 path=P/f'Assets/textures/{id}.png';path.parent.mkdir(parents=True,exist_ok=True);im.save(path)
write('DiscoElysium2.whispproject',dict(formatVersion=1,name='DiscoElysium2 — Дело о пропавшем свете',startupScene='Scenes/Harbor.json',directories=dict(assets='Assets',scenes='Scenes')))
# A unit cube with explicit normals and independently authored harbor geometry.
vertices=[(-.5,-.5,-.5),(.5,-.5,-.5),(.5,.5,-.5),(-.5,.5,-.5),(-.5,-.5,.5),(.5,-.5,.5),(.5,.5,.5),(-.5,.5,.5)]
faces=[((1,4,3,2),(0,0,-1)),((5,6,7,8),(0,0,1)),((1,5,8,4),(-1,0,0)),((2,3,7,6),(1,0,0)),((1,2,6,5),(0,-1,0)),((4,8,7,3),(0,1,0))]
mesh='\n'.join('v %s %s %s'%v for v in vertices)+'\n'+'\n'.join('vn %s %s %s'%n for _,n in faces)+'\n'
for i,(indices,_) in enumerate(faces,1):mesh+='f '+' '.join(f'{v}//{i}' for v in indices)+'\n'
write('Assets/models/harbor_cube.obj',mesh)
write('Assets/materials/harbor.material.json',dict(name='Harbor painted surfaces',shaderPath='dx12/textured.hlsl',baseColor=[1,1,1,1],roughness=.9))
entities=[]
def entity(id,pos=(0,0,0),scale=(1,1,1),color=None,collider=False,**components):
 c=dict(Tag=dict(name=id),Transform=dict(position=pos,rotation=[0,0,0],scale=scale))
 if color is not None:c.update(MeshRenderer=dict(meshPath='models/harbor_cube.obj',visible=True),Material=dict(materialPath='materials/harbor.material.json',tint=list(color)+[1]))
 if collider:c.update(Collider=dict(halfExtents=[s/2 for s in scale],autoFitFromMesh=False,friction=.85,restitution=0),Rigidbody=dict(isStatic=True,useGravity=False))
 c.update(components);n=dict(id=id,components=c);entities.append(n);return n
entity('Quay',(0,-.25,0),(19,.5,17),(.28,.31,.31),True)
entity('Sea',(0,-.75,0),(55,.25,55),(.07,.18,.22))
for i in range(17):entity('Paving'+str(i),(0,.006,-8+i),(18,.018,.025),(.39,.4,.36))
entity('Office',(-7,1.65,5),(4,3.3,3),(.34,.34,.3),True)
entity('OfficeRoof',(-7,3.4,5),(4.5,.25,3.5),(.16,.24,.25))
entity('OfficeDoor',(-6,1,3.48),(1,2,.06),(.12,.2,.22))
for i in range(3):entity('OfficeWindow'+str(i),(-8+i,2.2,3.45),(.6,.7,.08),(.67,.55,.3))
entity('CargoA',(0,.8,0),(2.5,1.6,2.4),(.42,.33,.23),True)
entity('CargoB',(2,.55,1.2),(1.5,1.1,1.5),(.36,.3,.22),True)
entity('Bench',(-3,.4,-1),(2,.8,.7),(.33,.31,.25),True)
entity('RadioDesk',(5,.55,3),(1.5,1.1,1),(.29,.33,.31),True)
for x in [-8,8]:
 for z in [-6,0,6]:entity(f'Bollard{x}_{z}',(x,.35,z),(.45,.7,.45),(.16,.2,.2),True)
entity('Camera',(-13,15,-17),Camera=dict(enabled=True,projection=1,orthographicSize=22,nearClip=.1,farClip=150,priority=50))['components']['Transform']['rotation']=[math.atan(math.sqrt(.5)),math.pi/4,0]
player=entity('Detective',(0,.85,-5),(.65,1.7,.6),(.16,.29,.32),True,CharacterMovement=dict(moveSpeed=3.4,jumpSpeed=0),UICanvas=dict(document='ui/harbor.ui.json',enabled=True,sortOrder=1),LuaScript=dict(scriptPath='scripts/harbor.lua',enabled=True,inputEnabled=True,cameraEnabled=True,properties=dict(camera={'$entity':'Camera'})))
player['components']['Rigidbody']=dict(mass=75,useGravity=True,isStatic=False,lockRotation=True)
player['components']['Collider']['halfExtents']=[.3,.85,.3]
head=entity('DetectiveHead',(0,.64,0),(.6,.28,.6),(.69,.55,.4));head['parent']='Detective'
for tag,pos,col in [('Keeper',(-4,.85,3),(.49,.29,.2)),('Radio',(5,.85,1),(.16,.35,.36)),('Cashier',(-5,.85,-3),(.42,.24,.34))]:
 actor=entity(tag,pos,(.7,1.7,.65),col,True)
 actor['components']['Collider'].update(halfExtents=[.35,1.1,.325],offset=[0,.25,0])
 n=entity(tag+'Head',(0,.64,0),(.6,.28,.6),(.7,.55,.4));n['parent']=tag
entity('Receipt',(-1,.16,-2.2),(.65,.25,.5),(.78,.73,.5),True)
entity('Ledger',(4.7,1.15,3),(.65,.18,.5),(.49,.34,.24),True)
entity('HarborSun',DirectionalLight=dict(color=[1,.84,.63],intensity=2.1))['components']['Transform']['rotation']=[.9,-.5,0]
write('Scenes/Harbor.json',dict(version=4,settings=dict(ambient=dict(color=[.56,.69,.76],intensity=.65),legacyDirectional=dict(enabled=False),environment=dict(enabled=False),background=dict(mode=0,solid=[.055,.095,.115])),entities=entities))
# Reusable base and a real v3 inherited variant, also exercised by integration tests.
base=dict(version=2,root='Witness',entities=[dict(id='Witness',components=entities[next(i for i,e in enumerate(entities) if e['id']=='Keeper')]['components'])])
base['entities'][0]['components']['Transform']['position']=[0,0,0]
write('Assets/prefabs/witness.prefab.json',base)
write('Assets/prefabs/radio.prefab.json',dict(version=3,parentPrefab='prefabs/witness.prefab.json',overrides={'Witness':dict(components={'Tag':dict(properties={'/name':dict(value='Radio',type='string')}),'Material':dict(properties={'/tint':dict(value=[.16,.35,.36,1],type='array')})})},addedEntities=[],suppressedEntities=[]))
# UI document: one native Canvas and the existing Widget Designer format.
widgets=[]
def widget(id,type='Panel',parent='Root',pos=(0,0),size=(100,40),text='',color=(.08,.12,.14,.96),**kw):
 w=dict(id=id,type=type,parent=parent,position=pos,size=size,text=text,color=color,**(dict(fontSize=19)|kw));widgets.append(w);return w
widget('Root',parent='',size=(1280,800),color=(0,0,0,0))
widget('Top',pos=(18,16),size=(1244,74))
widget('Title','Text','Top',(18,8),(700,27),'ДЕЛО О ПРОПАВШЕМ СВЕТЕ',(.87,.75,.5,1),fontSize=23)
widget('Hint','Text','Top',(18,40),(900,24),'ЛКМ — идти / говорить   •   J — журнал   •   C — навыки   •   Z / X — масштаб',(.73,.8,.78,1),fontSize=16)
for id,x,text in [('JournalToggle',1000,'Журнал'),('SkillsToggle',1120,'Навыки')]:
 widget(id,'Button','Top',(x,18),(105,36),color=(.22,.3,.31,1));widget(id+'Text','Text',id,(9,7),(100,25),text,(.94,.87,.7,1),fontSize=17)
widget('Status','Text',pos=(26,100),size=(950,28),text='Причал Девять. Найдите смотрительницу Марту у конторы.',color=(.88,.85,.72,1),fontSize=18)
widget('Dialogue',pos=(270,335),size=(990,445),visible=False,color=(.065,.09,.105,.98))
widget('Portrait','Image','Dialogue',(18,20),(136,170),texture='textures/keeper.png',color=(1,1,1,1))
widget('Speaker','Text','Dialogue',(177,18),(760,32),color=(.89,.74,.46,1),fontSize=23)
widget('Line','Text','Dialogue',(177,62),(780,132),color=(.88,.89,.82,1),fontSize=21)
widget('Feedback','Text','Dialogue',(177,192),(780,30),color=(.65,.8,.72,1),fontSize=17)
for i in range(1,5):
 widget('Choice'+str(i),'Button','Dialogue',(177,224+(i-1)*46),(780,40),color=(.15,.21,.23,1),visible=False)
 widget('ChoiceText'+str(i),'Text','Choice'+str(i),(12,8),(752,27),color=(.89,.84,.69,1),fontSize=18)
widget('CloseDialogue','Button','Dialogue',(18,210),(136,37),color=(.29,.22,.2,1));widget('CloseLabel','Text','CloseDialogue',(10,9),(130,25),'Уйти',(.94,.84,.68,1),fontSize=17)
widget('Journal',pos=(20,145),size=(360,370),visible=False)
widget('JournalTitle','Text','Journal',(16,14),(330,30),'ЖУРНАЛ',(.87,.74,.5,1),fontSize=23)
widget('Quest','Text','Journal',(16,60),(330,140),color=(.85,.88,.83,1),fontSize=19)
widget('History','Text','Journal',(16,218),(330,140),color=(.63,.74,.74,1),fontSize=16)
widget('Skills',pos=(900,130),size=(360,186),visible=False)
widget('SkillsTitle','Text','Skills',(16,14),(330,30),'НАВЫКИ',(.87,.74,.5,1),fontSize=23)
widget('SkillsText','Text','Skills',(16,60),(330,115),color=(.85,.88,.83,1),fontSize=19)
write('Assets/ui/harbor.ui.json',dict(version=1,name='Harbor Investigation',root='Root',referenceResolution=[1280,800],scaleMode=0,widgets=widgets))
write('README.md','''# Дело о пропавшем свете

Откройте DiscoElysium2.whispproject в WhispEngine (ветка engine_workflow), нажмите Play.
ЛКМ: идти или подойти к свидетелю/улике. J: журнал. C: навыки. Z/X: масштаб.
Escape завершает Play (стандартное управление редактора). Кнопка «Уйти» закрывает разговор.

Марта у конторы начинает дело. Найдите квитанцию перед ящиками, прочтите журнал
на столе радиста. Поговорите с Ильёй. Проверка: 2d6 + Логика + бонус журнала.
При неудаче поговорите с Ниной и вернитесь к Илье: расследование не блокируется.
После признания вернитесь к Марте и выберите один из двух финалов.

Состояние расследования живёт только в Play; Stop / новый Play сбрасывает его.
Сцена плоская: навигация не поддерживает лестницы, движущиеся препятствия и несколько этажей.
Весь текст, геометрия и портреты созданы для этого примера; внешние игровые ассеты не используются.
''')
print(P)

