#pragma once
namespace editor {
struct ProjectileInput {
    bool held=false;
    bool Update(bool down,bool enabled,bool play,bool paused,bool focused,bool capture,bool camera){
        const bool edge=down&&!held;held=down;return edge&&enabled&&play&&!paused&&focused&&!capture&&!camera;
    }
};
inline bool EditorHotkeysAllowed(bool play,bool focused,bool capture,bool camera){return !play&&focused&&!capture&&!camera;}
enum class InputOwner {None,UI,TransformGizmo,LightGizmo,Camera,Selection,Runtime};
struct ViewportInput {
    bool focused=false,hovered=false,text=false,modal=false,transform=false,light=false;
    bool camera=false,right=false,left=false,play=false;
};
inline InputOwner RouteInput(const ViewportInput& s){
    if(!s.focused||s.text||s.modal)return InputOwner::UI;
    if(s.transform)return InputOwner::TransformGizmo;
    if(s.light)return InputOwner::LightGizmo;
    if((s.camera||s.right)&&!s.left&&s.hovered)return InputOwner::Camera;
    if(s.left&&s.hovered)return InputOwner::Selection;
    return s.play?InputOwner::Runtime:InputOwner::None;
}
}
