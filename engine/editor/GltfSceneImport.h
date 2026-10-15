#pragma once
#include "AssetImport.h"
#include "TransformMath.h"
#include <nlohmann/json.hpp>
namespace editor {
// Called only inside an owned unpublished package. Produces independent glTF mesh assets
// plus a versioned flat scene manifest; source buffers/images remain shared and unchanged.
class GltfSceneImport {
public:
 static std::string Prepare(const std::filesystem::path& stage,const ImportPlan&);
 static nlohmann::json ReadManifest(const std::filesystem::path&);
};
}
