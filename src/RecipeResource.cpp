#include <AYStats/RecipeResource.h>
#include <AYAssetFormat/AssetFormat.h>
#include <AYResource/IResourceLoader.h>
#include <AYResource/ResourceRegistry.h>
#include <mutex>

namespace ayt::stats {
namespace {
class Loader final : public ayt::resource::IResourceLoader {
public:
    bool canLoad(const std::string& path) const override { return ayt::asset_format::matchesPath(path, ayt::asset_format::Id::StatsRecipes); }
    const char* getResourceType() const override { return "StatsRecipes"; }
    std::shared_ptr<ayt::resource::IResource> load(const std::string& path) override {
        auto resource=std::make_shared<RecipeResource>();return resource->load(path)?resource:nullptr;
    }
    std::shared_ptr<ayt::resource::IResource> loadFromBinary(const void* data,size_t size) override {
        auto resource=std::make_shared<RecipeResource>();return resource->decode(data,size)?resource:nullptr;
    }
    std::shared_ptr<ayt::resource::IResource> loadAsync(const std::string& path,
        std::function<void(std::shared_ptr<ayt::resource::IResource>)> callback) override {
        auto resource=load(path);if(callback)callback(resource);return resource;
    }
};
std::unique_ptr<ayt::resource::IResourceLoader> createLoader(){return std::make_unique<Loader>();}
}
void registerRecipeResourceLoader() {
    static std::once_flag once;
    std::call_once(once,[]{
        const auto& format = ayt::asset_format::describe(ayt::asset_format::Id::StatsRecipes);
        ayt::resource::ResourceRegistry::registerExtension(std::string(format.suffix), "StatsRecipes");
        for (const auto alias : format.readAliases)
            ayt::resource::ResourceRegistry::registerExtension(std::string(alias), "StatsRecipes");
        ayt::resource::ResourceRegistry::registerLoader("StatsRecipes",&createLoader);
    });
}
bool RecipeResource::load(const std::string& path) {
    auto text=readText(path);
    if(!text)return false;
    if(!decode(text->data(),text->size()))return false;
    _path=path;return true;
}
bool RecipeResource::decode(const void* data,size_t size) {
    if(!data || size>16*1024*1024)return false;
    auto asset=decodeRecipeAsset(std::string_view(static_cast<const char*>(data),size));
    if(!asset)return false;
    _asset=std::make_shared<RecipeAsset>(std::move(*asset));
    _bytes=size;_type="StatsRecipes";_loaded=true;return true;
}
} // namespace ayt::stats
