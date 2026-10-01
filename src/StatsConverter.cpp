#include <AYResource/Converter/StatsConverter.h>
#include <AYStats/RecipeAsset.h>
#include <AYStorage/Guid.h>
#include <AYIO/File.h>
#include <filesystem>
#include <stdexcept>

namespace ayt::resource {
ConversionResult StatsConverter::convert() {
    namespace fs=std::filesystem;
    auto source=stats::inspectRecipeAssetSource(_source,_root);
    if(!source) throw std::runtime_error(source.error().message);
    auto encoded=stats::encodeRecipeAsset(source->asset);
    if(!encoded) throw std::runtime_error(encoded.error().message);
    if(!_output.empty()) {
        const auto path=fs::path(_output)/source->logicalPath;
        fs::create_directories(path.parent_path());
        if(!io::File::atomicWrite(path.string(),encoded->data(),encoded->size()))
            throw std::runtime_error("Stats recipe output write failed");
    }
    ConversionResult result;
    result.importerContractTag="stats-recipe-asset-v1";
    ConversionResult::ConvertedResource resource;
    resource.path=source->logicalPath;
    resource.type="StatsRecipes";
    resource.size=encoded->size();
    resource.guid=storage::Guid::computeFromData(encoded->data(),encoded->size());
    result.resources.push_back(std::move(resource));
    return result;
}
}
