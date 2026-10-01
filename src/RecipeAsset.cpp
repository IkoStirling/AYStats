#include <AYStats/RecipeAsset.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <map>
#include <stdexcept>

namespace ayt::stats {
namespace {
using Json=nlohmann::json;
template<class T> Result<T> bad(Error code,std::string message,std::string path={}) {
    return std::unexpected(Diagnostic{code,std::move(message),std::move(path)});
}
void keys(const Json& value,std::initializer_list<std::string_view> fields) {
    if(!value.is_object() || value.size()!=fields.size())throw std::runtime_error("unexpected or missing stats asset field");
    for(const auto field:fields)if(!value.contains(std::string(field)))throw std::runtime_error("missing stats asset field");
}
Result<std::filesystem::path> child(const std::filesystem::path& root,const std::string& name) {
    if(name.empty())return bad<std::filesystem::path>(Error::Invalid,"empty stats asset reference");
    const auto relative=std::filesystem::u8path(name);
    if(relative.is_absolute() || relative.has_root_path() || relative.has_root_name())
        return bad<std::filesystem::path>(Error::Invalid,"stats asset reference must be Assets-relative",name);
    for(const auto& part:relative)if(part=="..")
        return bad<std::filesystem::path>(Error::Invalid,"stats asset reference escapes Assets",name);
    std::error_code error;
    const auto path=std::filesystem::weakly_canonical(root/relative,error);
    if(error)return bad<std::filesystem::path>(Error::Invalid,error.message(),name);
    const auto inside=path.lexically_relative(root);
    if(inside.empty() || inside=="." || *inside.begin()=="..")
        return bad<std::filesystem::path>(Error::Invalid,"stats asset reference escapes Assets",name);
    if(!std::filesystem::is_regular_file(path,error))
        return bad<std::filesystem::path>(Error::Missing,"stats asset dependency missing",name);
    return path;
}
}

Result<RecipeAssetSource> inspectRecipeAssetSourceText(const std::filesystem::path& manifest,
    const std::filesystem::path& assetsRoot,std::string_view text) {
    try {
        std::error_code error;
        const auto root=std::filesystem::canonical(assetsRoot,error);
        if(error)return bad<RecipeAssetSource>(Error::Missing,error.message(),assetsRoot.generic_string());
        const auto source=std::filesystem::weakly_canonical(manifest,error);
        if(error)return bad<RecipeAssetSource>(Error::Invalid,error.message(),manifest.generic_string());
        const auto relative=source.lexically_relative(root);
        if(relative.empty() || relative=="." || *relative.begin()==".." ||
           !relative.generic_string().ends_with(".aystats.json"))
            return bad<RecipeAssetSource>(Error::Invalid,"manifest must be .aystats.json under Assets",manifest.generic_string());
        const auto document=Json::parse(text);
        keys(document,{"version","kind","targetDefinitions","sourceDefinitions","recipeBook"});
        if(document.at("version")!=1 || document.at("kind")!="stats-recipe-source")
            return bad<RecipeAssetSource>(Error::Invalid,"unsupported stats recipe source version",relative.generic_string());
        RecipeAssetSource result;result.manifest=source;
        auto target=child(root,document.at("targetDefinitions").get<std::string>());
        if(!target)return std::unexpected(target.error());result.targetDefinitions=*target;
        const auto sourceName=document.at("sourceDefinitions").get<std::string>();
        if(!sourceName.empty()){
            auto sourceFile=child(root,sourceName);
            if(!sourceFile)return std::unexpected(sourceFile.error());result.sourceDefinitions=*sourceFile;
        }
        auto book=child(root,document.at("recipeBook").get<std::string>());
        if(!book)return std::unexpected(book.error());result.recipeBook=*book;
        auto targetText=readText(result.targetDefinitions);
        if(!targetText)return std::unexpected(targetText.error());
        auto targetSchema=decodeDefinitions(*targetText);
        if(!targetSchema)return std::unexpected(targetSchema.error());
        result.asset.targetSchema=*targetSchema;
        if(!result.sourceDefinitions.empty()) {
            auto sourceText=readText(result.sourceDefinitions);
            if(!sourceText)return std::unexpected(sourceText.error());
            auto sourceSchema=decodeDefinitions(*sourceText);
            if(!sourceSchema)return std::unexpected(sourceSchema.error());
            result.asset.sourceSchema=*sourceSchema;
        }
        auto bookText=readText(result.recipeBook);
        if(!bookText)return std::unexpected(bookText.error());
        auto decoded=decodeRecipeBook(*bookText,*result.asset.targetSchema,result.asset.sourceSchema.get());
        if(!decoded)return std::unexpected(decoded.error());
        result.asset.book=*decoded;
        result.logicalPath=relative.generic_string();
        result.logicalPath.resize(result.logicalPath.size()-5); // remove trailing .json
        return result;
    }catch(const std::exception& exception){
        return bad<RecipeAssetSource>(Error::Invalid,exception.what(),manifest.generic_string());
    }
}

Result<RecipeAssetSource> inspectRecipeAssetSource(const std::filesystem::path& manifest,
                                                   const std::filesystem::path& assetsRoot) {
    auto text=readText(manifest);
    if(!text)return std::unexpected(text.error());
    return inspectRecipeAssetSourceText(manifest,assetsRoot,*text);
}

Result<std::string> encodeRecipeAsset(const RecipeAsset& asset) {
    try {
        if(!asset.targetSchema || !asset.book)
            return bad<std::string>(Error::Invalid,"incomplete stats recipe asset");
        auto target=encodeDefinitions(*asset.targetSchema);
        if(!target)return std::unexpected(target.error());
        auto recipes=encodeRecipeBook(*asset.book,*asset.targetSchema,asset.sourceSchema.get());
        if(!recipes)return std::unexpected(recipes.error());
        Json source=nullptr;
        if(asset.sourceSchema) {
            auto encoded=encodeDefinitions(*asset.sourceSchema);
            if(!encoded)return std::unexpected(encoded.error());
            source=Json::parse(*encoded);
        }
        auto text=Json{{"version",1},{"kind","stats-recipe-asset"},
                       {"targetDefinitions",Json::parse(*target)},
                       {"sourceDefinitions",std::move(source)},
                       {"recipeBook",Json::parse(*recipes)}}.dump();
        if(text.size()>16*1024*1024)return bad<std::string>(Error::Limit,"stats recipe asset byte limit");
        return text;
    }catch(const std::exception& exception){return bad<std::string>(Error::Invalid,exception.what());}
}

Result<RecipeAsset> decodeRecipeAsset(std::string_view bytes) {
    try {
        if(bytes.size()>16*1024*1024)return bad<RecipeAsset>(Error::Limit,"stats recipe asset byte limit");
        const auto document=Json::parse(bytes);
        keys(document,{"version","kind","targetDefinitions","sourceDefinitions","recipeBook"});
        if(document.at("version")!=1 || document.at("kind")!="stats-recipe-asset")
            return bad<RecipeAsset>(Error::Invalid,"unsupported stats recipe asset version");
        auto target=decodeDefinitions(document.at("targetDefinitions").dump());
        if(!target)return std::unexpected(target.error());
        RecipeAsset result;result.targetSchema=*target;
        if(!document.at("sourceDefinitions").is_null()) {
            auto source=decodeDefinitions(document.at("sourceDefinitions").dump());
            if(!source)return std::unexpected(source.error());
            result.sourceSchema=*source;
        }
        auto book=decodeRecipeBook(document.at("recipeBook").dump(),*result.targetSchema,result.sourceSchema.get());
        if(!book)return std::unexpected(book.error());
        result.book=*book;return result;
    }catch(const std::exception& exception){return bad<RecipeAsset>(Error::Invalid,exception.what());}
}

Result<std::filesystem::path> cookRecipeAssetSource(const std::filesystem::path& manifest,
    const std::filesystem::path& assetsRoot) {
    auto source=inspectRecipeAssetSource(manifest,assetsRoot);
    if(!source)return std::unexpected(source.error());
    auto cooked=encodeRecipeAsset(source->asset);
    if(!cooked)return std::unexpected(cooked.error());
    const auto output=std::filesystem::canonical(assetsRoot)/source->logicalPath;
    auto saved=writeTextAtomic(output,*cooked);
    if(!saved)return std::unexpected(saved.error());
    return output;
}

Result<RecipeAssetSource> saveRecipeAssetSource(const std::filesystem::path& manifest,
    const std::filesystem::path& assetsRoot,std::string_view targetDefinitions,
    std::string_view sourceDefinitions,std::string_view recipeBook,
    std::optional<std::string_view> expectedText) {
    try {
        Json document={{"version",1},{"kind","stats-recipe-source"},
            {"targetDefinitions",targetDefinitions},{"sourceDefinitions",sourceDefinitions},
            {"recipeBook",recipeBook}};
        const auto text=document.dump(2)+"\n";
        auto source=inspectRecipeAssetSourceText(manifest,assetsRoot,text);
        if(!source)return source;
        const auto cooked=encodeRecipeAsset(source->asset);
        if(!cooked)return std::unexpected(cooked.error());
        std::optional<std::string> old;
        if(std::filesystem::exists(source->manifest)) {
            auto read=readText(source->manifest);
            if(!read)return std::unexpected(read.error());
            old=*read;
        }
        if(old.has_value()!=expectedText.has_value() ||
           (old && *old!=*expectedText))
            return bad<RecipeAssetSource>(Error::Stale,"recipe manifest changed on disk",source->manifest.generic_string());
        std::filesystem::create_directories(source->manifest.parent_path());
        auto saved=writeTextAtomic(source->manifest,text);
        if(!saved)return std::unexpected(saved.error());
        const auto output=std::filesystem::canonical(assetsRoot)/source->logicalPath;
        auto result=writeTextAtomic(output,*cooked);
        if(!result) {
            if(old)(void)writeTextAtomic(source->manifest,*old);
            else {std::error_code ignored;std::filesystem::remove(source->manifest,ignored);}
            return std::unexpected(result.error());
        }
        return source;
    }catch(const std::exception& error){return bad<RecipeAssetSource>(Error::Invalid,error.what(),manifest.generic_string());}
}

Result<std::vector<std::filesystem::path>> saveRecipeBookAndRecook(
    const std::filesystem::path& recipePath,const std::filesystem::path& assetsRoot,
    std::string_view expectedText,std::string_view replacement) {
    try {
        const auto root=std::filesystem::canonical(assetsRoot);
        const auto path=std::filesystem::canonical(recipePath);
        const auto relative=path.lexically_relative(root);
        if(relative.empty() || relative=="." || *relative.begin()=="..")
            return bad<std::vector<std::filesystem::path>>(Error::Invalid,"recipe file leaves Assets",path.generic_string());
        auto original=readText(path);
        if(!original)return std::unexpected(original.error());
        if(*original!=expectedText)
            return bad<std::vector<std::filesystem::path>>(Error::Stale,"recipe file changed on disk",path.generic_string());
        struct Prepared {std::filesystem::path output;std::string cooked;std::optional<std::string> previous;};
        std::vector<Prepared> outputs;
        for(const auto& entry:std::filesystem::recursive_directory_iterator(root)) {
            if(!entry.is_regular_file() || !entry.path().filename().string().ends_with(".aystats.json"))continue;
            auto text=readText(entry.path());
            if(!text)continue;
            auto manifest=Json::parse(*text,nullptr,false);
            if(!manifest.is_object() || !manifest.contains("recipeBook") || !manifest["recipeBook"].is_string())continue;
            auto dependency=child(root,manifest["recipeBook"].get<std::string>());
            if(!dependency || *dependency!=path)continue;
            auto source=inspectRecipeAssetSource(entry.path(),root);
            if(!source)return std::unexpected(source.error());
            auto book=decodeRecipeBook(replacement,*source->asset.targetSchema,source->asset.sourceSchema.get());
            if(!book)return std::unexpected(book.error());
            source->asset.book=*book;
            auto cooked=encodeRecipeAsset(source->asset);
            if(!cooked)return std::unexpected(cooked.error());
            Prepared item;item.output=root/source->logicalPath;item.cooked=*cooked;
            if(std::filesystem::exists(item.output)) {
                auto prior=readText(item.output);
                if(!prior)return std::unexpected(prior.error());
                item.previous=*prior;
            }
            outputs.push_back(std::move(item));
        }
        auto saved=writeTextAtomic(path,replacement);
        if(!saved)return std::unexpected(saved.error());
        for(auto& item:outputs) {
            auto cooked=writeTextAtomic(item.output,item.cooked);
            if(cooked)continue;
            bool restored=writeTextAtomic(path,*original).has_value();
            for(auto& rollback:outputs) {
                if(rollback.previous)restored=writeTextAtomic(rollback.output,*rollback.previous).has_value() && restored;
                else {std::error_code error;std::filesystem::remove(rollback.output,error);restored=!error && restored;}
            }
            return bad<std::vector<std::filesystem::path>>(Error::Invalid,
                cooked.error().message+(restored?"; previous files restored":"; rollback incomplete"),
                item.output.generic_string());
        }
        std::vector<std::filesystem::path> paths;
        paths.reserve(outputs.size());
        for(const auto& item:outputs)paths.push_back(item.output);
        return paths;
    }catch(const std::exception& error){return bad<std::vector<std::filesystem::path>>(Error::Invalid,error.what(),recipePath.generic_string());}
}
} // namespace ayt::stats
