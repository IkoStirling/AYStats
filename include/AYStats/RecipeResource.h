#pragma once
#include <AYStats/RecipeAsset.h>
#include <AYResource/IResource.h>

namespace ayt::stats {
/// AYResource L2 snapshot for a self-contained .aystats payload.
class RecipeResource final : public ayt::resource::IResource {
public:
    bool load(const std::string& path) override;
    bool reload(const std::string& path) override { return load(path); }
    bool unload() override { _asset.reset();_loaded=false;return true; }
    size_t sizeInBytes() const override { return sizeof(*this)+_bytes; }
    bool decode(const void* data,size_t size);
    std::shared_ptr<const RecipeAsset> asset() const noexcept { return _asset; }
private:
    std::shared_ptr<const RecipeAsset> _asset;
    size_t _bytes=0;
};
/// Register before loading through ResourceManager; safe to call repeatedly.
void registerRecipeResourceLoader();
} // namespace ayt::stats
