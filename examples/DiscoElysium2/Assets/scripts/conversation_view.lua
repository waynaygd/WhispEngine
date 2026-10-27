local Dialogue=Script.require('dialogue')
local M={}
local function text(id,parent,value,y,height,color,size)
 return {id=id,parent=parent,type='Text',text=value,position={12,y},size={380,height},fontSize=size or 19,color=color or {.83,.82,.75,1}}
end
function M.history(self,speaker,value,player)
 self.transcript[#self.transcript+1]={speaker=speaker,text=value,player=player}
 if #self.transcript>400 then table.remove(self.transcript,1) end
end
function M.rebuild(self)
 local nodes={};local y=0
 for i,entry in ipairs(self.transcript) do
  local wrapped,height=ui.measure_text(entry.text,19,380)
  nodes[#nodes+1]=text('HistorySpeaker'..i,'HistoryList',entry.speaker,y,27,entry.player and {.54,.72,.72,1} or {.82,.64,.37,1},16)
  nodes[#nodes+1]=text(i==#self.transcript and not entry.player and 'Line' or 'HistoryLine'..i,'HistoryList',wrapped,y+29,height)
  y=y+height+52
 end
 self.hud:replace_children('HistoryList',nodes);self.hud:set_scroll('HistoryList',self.hud:scroll_limit('HistoryList'))
 if not self.dialogue then return end
 local n=self.dialogue.nodes[self.dialogue.current]
 local speaker=ui.measure_text(n.speaker,21,270);self.hud:set_text('Speaker',speaker);self.hud:set_image('Portrait',n.portrait)
 local feedback=ui.measure_text(self.state.feedback,16,404);self.hud:set_text('Feedback',feedback)
 self.responses=Dialogue.choices(self.dialogue,self.state.flags)
 local available={};for _,c in ipairs(self.responses) do available[c]=true end
 local options={};for i,c in ipairs(self.responses) do options[#options+1]={c=c,index=i,enabled=true} end
 for _,c in ipairs(n.choices) do if not available[c] then options[#options+1]={c=c,index=#options+1,enabled=false} end end
 nodes={};y=0
 for _,option in ipairs(options) do
  local c=option.c;local id='Choice'..option.index;local check=c.check~='';local conditional=c.requires~='' or c.excludes~=''
  local prefix=not option.enabled and '[НЕДОСТУПНО] ' or check and '[ПРОВЕРКА] ' or conditional and '[УЛИКА] ' or ''
  local value,textHeight=ui.measure_text(prefix..c.text,18,380);local height=textHeight+20
  nodes[#nodes+1]={id=id,parent='Responses',type='Button',position={0,y},size={398,height},enabled=option.enabled,transitionDuration=.12,
   color=check and {.14,.23,.21,1} or conditional and {.25,.22,.16,1} or {.15,.18,.18,1},
   hoverColor={.31,.32,.26,1},pressedColor={.35,.29,.18,1},disabledColor={.10,.12,.12,1}}
  nodes[#nodes+1]=text('ChoiceText'..option.index,id,value,10,height-12,option.enabled and (check and {.64,.84,.74,1} or {.89,.83,.69,1}) or {.40,.45,.44,1},18)
  y=y+height+8
 end
 self.hud:replace_children('Responses',nodes);self.hud:set_scroll('Responses',0)
end
function M.show(self)
 local node=self.dialogue.nodes[self.dialogue.current];M.history(self,node.speaker,node.text,false)
 self.hud:set_visible('Dialogue',true);M.rebuild(self)
end
function M.close(self) self.dialogue=nil;self.responses={};self.hud:set_visible('Dialogue',false) end
return M
