#include "FontCache.h"
#include "../resources/ResourceManager.h"
#include "../render/IRenderAdapter.h"
#include "../core/Logger.h"
namespace ui {
void FontCache::Clear(IRenderAdapter* gpu){for(auto& [key,e]:entries)if(e.texture.IsValid()){if(gpu)gpu->DestroyTexture(e.texture);++counters.retired;}entries.clear();epoch=~0ull;limitReported=false;}
FontView FontCache::Get(const std::string& key,ResourceManager& rm,IRenderAdapter* gpu){
 if(key.empty())return {};
 if(epoch!=rm.GetCacheEpoch()){Clear(gpu);epoch=rm.GetCacheEpoch();}
 if(!entries.contains(key)&&entries.size()>=MaxFonts){if(!limitReported){Logger::Get().Warn("Font cache limit exceeded; using Default Font");limitReported=true;}return {nullptr,{},"Font cache limit exceeded; using Default Font"};}
 auto [it,added]=entries.try_emplace(key);auto& e=it->second;
 if(added)try{e.resource=rm.Load<FontResource>(key);rm.WatchForHotReload<FontResource>(key);}catch(const std::exception& failure){e.error=failure.what();}
 if(!e.resource){if(!e.reported){Logger::Get().Diagnostic("WARN","Runtime UI Font","Load",key+": "+e.error);e.reported=true;}return {nullptr,{},e.error};}
 if(e.revision!=e.resource->GetVersion()){
  if(e.texture.IsValid()){if(gpu)gpu->DestroyTexture(e.texture);++counters.retired;e.texture={};}
  e.revision=e.resource->GetVersion();e.error=e.resource->GetErrorMessage();e.reported=false;
 }
 auto atlas=e.resource->HasError()?nullptr:e.resource->GetData().atlas.get();
 // A failed upload is cached until revision/device invalidation, avoiding per-frame retries.
 if(atlas&&gpu&&!e.texture.IsValid()&&e.error.empty()){e.texture=gpu->CreateTexture2D(atlas->texture);if(e.texture.IsValid())++counters.uploads;else e.error="Font atlas GPU allocation failed: "+key;}
 if(!e.error.empty()&&!e.reported){Logger::Get().Diagnostic("WARN","Runtime UI Font","Load",key+": "+e.error);e.reported=true;}
 return {gpu&&!e.texture.IsValid()?nullptr:atlas,e.texture,e.error};
}
}
