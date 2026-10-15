#include "InputManager.h"
#include <cmath>
#include <algorithm>
bool InputManager::BindNamedAction(const std::string& action,const std::string& key){
 if(action.empty()||action.size()>128||m_ActionMap.size()>=256&&!m_ActionMap.contains(action))return false;
 int code=-1;if(key.size()==1&&((key[0]>='A'&&key[0]<='Z')||(key[0]>='0'&&key[0]<='9')))code=key[0];
 else {static const std::unordered_map<std::string,int> names={{"Space",GLFW_KEY_SPACE},{"Tab",GLFW_KEY_TAB},{"Escape",GLFW_KEY_ESCAPE},{"Shift",GLFW_KEY_LEFT_SHIFT},{"Ctrl",GLFW_KEY_LEFT_CONTROL},{"Up",GLFW_KEY_UP},{"Down",GLFW_KEY_DOWN},{"Left",GLFW_KEY_LEFT},{"Right",GLFW_KEY_RIGHT},{"MouseLeft",-2-GLFW_MOUSE_BUTTON_LEFT},{"MouseRight",-2-GLFW_MOUSE_BUTTON_RIGHT}};auto i=names.find(key);if(i==names.end())return false;code=i->second;}
 m_ActionMap[action]=code;return true;
}
int InputManager::ActionIndex(const std::string& action)const{auto i=m_ActionMap.find(action);if(i==m_ActionMap.end())return -1;int code=i->second;return code<=-2?GLFW_KEY_LAST+1+(-2-code):code;}
bool InputManager::FrameDown(const std::string& a)const{int i=ActionIndex(a);return i>=0&&i<int(m_Held.size())&&m_Held[i];}
bool InputManager::FramePressed(const std::string& a)const{int i=ActionIndex(a);return i>=0&&i<int(m_Pressed.size())&&m_Pressed[i];}
bool InputManager::FrameReleased(const std::string& a)const{int i=ActionIndex(a);return i>=0&&i<int(m_Released.size())&&m_Released[i];}
void InputManager::SampleFrame(const std::function<bool(int)>& keys,const std::function<bool(int)>& buttons,double x,double y){
 for(int i=0;i<int(m_Held.size());++i){bool now=i<=GLFW_KEY_LAST?keys(i):buttons(i-GLFW_KEY_LAST-1);m_Pressed[i]=now&&!m_Held[i];m_Released[i]=!now&&m_Held[i];m_Held[i]=now;}
 m_MouseDelta={};if(std::isfinite(x)&&std::isfinite(y)){if(m_MouseValid){double dx=x-m_MouseX,dy=y-m_MouseY;if(std::isfinite(dx)&&std::isfinite(dy))m_MouseDelta={float(std::clamp(dx,-1e6,1e6)),float(std::clamp(dy,-1e6,1e6)),0};}m_MouseX=x;m_MouseY=y;m_MouseValid=true;}else m_MouseValid=false;
}
void InputManager::BeginFrame(){double x=0,y=0;if(m_Window)glfwGetCursorPos(m_Window,&x,&y);SampleFrame([&](int k){return m_Window&&k>=GLFW_KEY_SPACE&&glfwGetKey(m_Window,k)==GLFW_PRESS;},[&](int b){return m_Window&&glfwGetMouseButton(m_Window,b)==GLFW_PRESS;},x,y);}

bool InputManager::IsActionActive(const std::string& action) const
{
    const auto it = m_ActionMap.find(action);
    return it != m_ActionMap.end() && (it->second<=-2?m_Window&&glfwGetMouseButton(m_Window,-2-it->second)==GLFW_PRESS:IsKeyPressed(it->second));
}

bool InputManager::IsKeyPressed(int glfwKey) const
{
    return m_Window != nullptr && glfwGetKey(m_Window, glfwKey) == GLFW_PRESS;
}

ecs::Vec3 InputManager::GetMovementAxis() const
{
    ecs::Vec3 move{};
    if (IsActionActive("MoveForward")) move.z += 1.0f;
    if (IsActionActive("MoveBackward")) move.z -= 1.0f;
    if (IsActionActive("MoveRight")) move.x += 1.0f;
    if (IsActionActive("MoveLeft")) move.x -= 1.0f;
    if (IsActionActive("MoveUp")) move.y += 1.0f;
    if (IsActionActive("MoveDown")) move.y -= 1.0f;
    return move;
}
