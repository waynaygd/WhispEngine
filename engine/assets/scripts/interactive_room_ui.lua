-- UI.3 demo: M opens a menu; Resume/Close Menu return to FPS controls.
-- This demonstrates UI state, not a health system or a simulation pause.
return {
    fields = {},
    on_start = function(self)
        self.hud = ui.get(self.entity)
        self.elapsed = 0
        self.menu = false
        Input.bind("OpenRuntimeMenu", "M")
        self.hud:set_visible("Menu", false)
    end,
    on_update = function(self, dt)
        if not self.hud:valid() then self.hud = ui.get(self.entity) end
        self.elapsed = self.elapsed + dt
        self.hud:set_progress("SanityBar", 0.5 + 0.4 * math.sin(self.elapsed * 0.2))
        self.hud:set_text("InteractPrompt", self.menu and "Выберите действие" or "[E] Нажмите E   |   [M] Menu")
        self.hud:set_visible("InteractPrompt", self.menu or math.sin(self.elapsed * 0.7) > -0.8)
        if Input.was_pressed("OpenRuntimeMenu") then
            self.menu = true
            self.hud:set_visible("Menu", true)
            ui.set_input_mode("UIOnly")
        end
    end,
    on_ui_event = function(self, event)
        if event.type == "click" and (event.widget == "Resume" or event.widget == "Back") then
            self.hud = ui.get(self.entity)
            self.menu = false
            self.hud:set_visible("Menu", false)
            ui.set_input_mode("GameOnly")
        end
    end
}
