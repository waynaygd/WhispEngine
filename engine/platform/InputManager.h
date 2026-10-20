#pragma once
#include "../ecs/MathTypes.h"
#include <GLFW/glfw3.h>
#include <string>
#include <unordered_map>
#include <array>
#include <functional>
#include <vector>

class InputManager
{
public:
    using Bindings=std::unordered_map<std::string,int>;
    Bindings CaptureBindings()const{return m_ActionMap;}
    void RestoreBindings(Bindings bindings){m_ActionMap=std::move(bindings);m_Held.fill(false);m_Pressed.fill(false);m_Released.fill(false);ResetMouseDelta();}
    void SetWindow(GLFWwindow* window) { m_Window = window; }
    void BindAction(const std::string& action, int glfwKey) { m_ActionMap[action] = glfwKey; }
    bool BindNamedAction(const std::string& action,const std::string& key);
    void BeginFrame();
    void SampleFrame(const std::function<bool(int)>& keys,const std::function<bool(int)>& buttons,double x,double y);
    bool FrameDown(const std::string&)const;
    bool IsMouseAction(const std::string& action)const{auto it=m_ActionMap.find(action);return it!=m_ActionMap.end()&&it->second<=-2;}
    bool FramePressed(const std::string&)const;
    bool FrameReleased(const std::string&)const;
    std::vector<std::string> Actions()const{std::vector<std::string> names;for(const auto& [name,key]:m_ActionMap)names.push_back(name);return names;}
    ecs::Vec3 MouseDelta()const{return m_MouseDelta;}
    void ResetMouseDelta(){m_MouseValid=false;m_MouseDelta={};}
    bool IsActionActive(const std::string& action) const;
    bool IsKeyPressed(int glfwKey) const;
    ecs::Vec3 GetMovementAxis() const;
private:
    GLFWwindow* m_Window = nullptr;
    std::unordered_map<std::string, int> m_ActionMap;
    std::array<bool,GLFW_KEY_LAST+1+GLFW_MOUSE_BUTTON_LAST+1> m_Held{},m_Pressed{},m_Released{};
    bool m_MouseValid=false;double m_MouseX=0,m_MouseY=0;ecs::Vec3 m_MouseDelta{};
    int ActionIndex(const std::string&)const;
};
