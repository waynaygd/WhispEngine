return {
 fields = {},
 on_start = function(self)
  self.hud=ui.get(self.entity);self.elapsed=0;self.menu=false;self.closing=false
  Input.bind("OpenRuntimeMenu","M")
  self.hud:set_visible("Menu",false);self.hud:play_animation("PromptIn")
 end,
 on_update = function(self,dt)
  if not self.hud:valid() then self.hud=ui.get(self.entity);self.closing=false;self.menu=self.hud:get_visible("Menu") end
  self.elapsed=self.elapsed+dt
  self.hud:set_progress("SanityBar",0.5+0.4*math.sin(self.elapsed*0.2))
  self.hud:set_text("InteractPrompt",self.menu and "Выберите действие" or "[E] Нажмите E  |  [M] Menu")
  if Input.was_pressed("OpenRuntimeMenu") then
   self.menu=true;self.closing=false;self.hud:set_visible("Menu",true)
   self.hud:play_animation("MenuFadeIn");self.hud:play_animation("MenuSlide")
   ui.set_input_mode("UIOnly")
  end
 end,
 on_ui_event = function(self,event)
  self.hud=ui.get(self.entity)
  if event.type=="click" and not self.closing and (event.widget=="Resume" or event.widget=="Back") then
   self.closing=true;self.hud:play_animation("MenuFadeOut")
  elseif event.type=="animation_complete" and event.animation=="MenuFadeOut" then
   self.menu=false;self.closing=false;self.hud:set_visible("Menu",false);ui.set_input_mode("GameOnly")
  end
 end
}
