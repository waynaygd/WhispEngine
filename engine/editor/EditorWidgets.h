#pragma once
#include <imgui.h>
#include "../ecs/ComponentRegistry.h"
#include "../render/ColorPipeline.h"
namespace editor {
inline bool LinearColorEdit(const char* label,float* linear,int count){
    float display[4]={1,1,1,1};for(int i=0;i<3;++i)display[i]=LinearToSrgb(linear[i]);if(count==4)display[3]=linear[3];
    bool changed=count==4?ImGui::ColorEdit4(label,display):ImGui::ColorEdit3(label,display);
    if(changed){for(int i=0;i<3;++i)linear[i]=SrgbToLinear(display[i]);if(count==4)linear[3]=display[3];}return changed;
}
struct AssetPayload {ecs::AssetKind kind;char key[1024];};
inline bool AcceptAssetDrop(ecs::AssetKind kind,std::string& key){
    bool changed=false;
    if(ImGui::BeginDragDropTarget()){
        if(auto* payload=ImGui::AcceptDragDropPayload("WHISP_ASSET"))
            if(payload->DataSize==sizeof(AssetPayload)){
                auto* data=static_cast<const AssetPayload*>(payload->Data);
                const auto end=std::find(data->key,data->key+sizeof(data->key),'\0');
                if(data->kind==kind&&end!=data->key+sizeof(data->key)){
                    std::string value(data->key,end);
                    if(ecs::ComponentRegistry::ValidateAssetKey(value,kind)){key=std::move(value);changed=true;}
                }
            }
        ImGui::EndDragDropTarget();
    }
    return changed;
}
}
