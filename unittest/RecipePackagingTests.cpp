#include <AYStats/RecipeAsset.h>
#include <AYStats/RecipeResource.h>
#include <AYResource/ProjectBuild.h>
#include <AYResource/CookShip.h>
#include <AYResource/ResourceManager.h>
#include <AYResource/ResourceBootstrap.h>
#include <AYResource/AssetPath.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <iostream>

namespace fs=std::filesystem;
using namespace ayt;
void check(bool value,const std::string& message) {if(!value)throw std::runtime_error(message);}
void write(const fs::path& path,const std::string& value) {
    std::ofstream stream(path,std::ios::binary);stream<<value;
    check(static_cast<bool>(stream),"write "+path.string());
}
int main() {
    const auto root=fs::temp_directory_path()/("AYStats_ResourceTests_"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root/"Assets/recipes");
        const fs::path examples=AY_STATS_EXAMPLE_ASSETS;
        for(const char* file:{"target.stats.json","source.stats.json","combat.stats-recipes.json"})
            fs::copy_file(examples/file,root/"Assets/recipes"/file);
        const auto manifest=root/"Assets/recipes/combat.aystats.json";
        const std::string manifestText=R"({"version":1,"kind":"stats-recipe-source","targetDefinitions":"recipes/target.stats.json","sourceDefinitions":"recipes/source.stats.json","recipeBook":"recipes/combat.stats-recipes.json"})";
        write(manifest,manifestText);
        write(manifest,R"({"version":1,"kind":"stats-recipe-source","targetDefinitions":"recipes/missing.stats.json","sourceDefinitions":"","recipeBook":"recipes/combat.stats-recipes.json"})");
        check(!stats::inspectRecipeAssetSource(manifest,root/"Assets"),"missing stats source rejected");
        write(manifest,R"({"version":1,"kind":"stats-recipe-source","targetDefinitions":"../outside.stats.json","sourceDefinitions":"","recipeBook":"recipes/combat.stats-recipes.json"})");
        check(!stats::inspectRecipeAssetSource(manifest,root/"Assets"),"escaping stats source rejected");
        write(manifest,manifestText);

        resource::ProjectBuildProfile profile;
        profile.schemaVersion=1;profile.id="stats-test";profile.platform="windows";
        profile.architecture="x64";profile.configuration="Development";
        profile.content.defaultTransform=resource::ProjectAssetTransform::Raw;
        profile.content.defaultStorage=resource::ProjectAssetStorage::Loose;
        profile.content.rules={{"recipes/**/*.aystats.json",resource::ProjectAssetTransform::Cook,
            resource::ProjectAssetStorage::Pak,"stats",true}};
        profile.package.output="out/package";
        write(root/"Assets/recipes/valid.ayscene",
              R"({"entities":[{"components":[{"$type":"StatsRecipeComponent","assetPath":"recipes/combat.aystats"}]}]})");
        auto plan=resource::ProjectBuildPlanner::create(profile,root.string());
        check(plan.valid(),"valid recipe build plan");
        auto build=resource::ProjectBuildExecutor::execute(plan);
        check(build.ok,"first recipe build: "+build.error);
        check(build.cookedCount==1 && build.cacheHitCount==0,"first recipe cook");
        build=resource::ProjectBuildExecutor::execute(plan);
        check(build.ok && build.cacheHitCount==1,"recipe cache hit");
        {
            std::ofstream stream(root/"Assets/recipes/combat.stats-recipes.json",std::ios::app);
            stream<<' ';
        }
        build=resource::ProjectBuildExecutor::execute(plan);
        check(!build.ok && build.error.find("changed after planning")!=std::string::npos,"stale recipe plan");
        auto fresh=resource::ProjectBuildPlanner::create(profile,root.string());
        check(fresh.valid(),"replan recipe change");
        build=resource::ProjectBuildExecutor::execute(fresh);
        check(build.ok && build.cacheHitCount==0,"recipe dependency invalidates cache");
        auto excluded=profile;
        excluded.content.rules.push_back({"recipes/source.stats.json",resource::ProjectAssetTransform::Exclude,
            resource::ProjectAssetStorage::Loose,"",true});
        check(!resource::ProjectBuildPlanner::create(excluded,root.string()).valid(),"excluded stats dependency");
        write(root/"Assets/recipes/invalid.ayscene",
              R"({"entities":[{"components":[{"$type":"StatsRecipeComponent","assetPath":"recipes/missing.aystats"}]}]})");
        check(!resource::ProjectBuildPlanner::create(profile,root.string()).valid(),"scene missing stats asset");
        fs::remove(root/"Assets/recipes/invalid.ayscene");

        auto cooked=stats::encodeRecipeAsset(stats::inspectRecipeAssetSource(manifest,root/"Assets")->asset);
        check(cooked.has_value(),"encode final stats asset");
        const auto finalAssets=root/"final/recipes";
        fs::create_directories(finalAssets);
        write(finalAssets/"combat.aystats",*cooked);
        resource::CookShipOptions ship;
        ship.assetsRoot=(root/"final").string();ship.outputDir=(root/"ship").string();
        auto shipped=resource::cookShipPackage(ship);
        check(shipped.ok,"ship valid stats asset: "+shipped.error);
        const auto priorPakSize=fs::file_size(shipped.pakPath);
        write(finalAssets/"combat.aystats","{broken");
        shipped=resource::cookShipPackage(ship);
        check(!shipped.ok && fs::file_size(root/"ship/content.pak")==priorPakSize,
              "invalid stats asset preserves prior ship package");

        stats::registerRecipeResourceLoader();resource::initializeLoaders();
        const auto content=root/"out/package/Content";
        auto& manager=resource::ResourceManager::instance();
        check(manager.openDatabase((content/"resources.db").string()),"mount stats DB");
        resource::setAssetRoot(content.string());
        fs::remove_all(root/"Assets");
        auto logical=resource::resolveAssetPath({},"recipes/combat.aystats");
        check(logical=="recipes/combat.aystats","package logical path");
        auto loaded=manager.load<stats::RecipeResource>(logical);
        check(loaded && !manager.hasLoadFailed(logical) && loaded->asset(),"Pak-only stats recipe load");
        check(loaded->asset()->book->select(stats::statId("health"),stats::AttributeOp::Decrease,"spill").has_value(),
            "Pak recipe behavior");
        manager.unloadAll();manager.setDatabase({});resource::setAssetRoot({});
        std::error_code cleanupError;fs::remove_all(root,cleanupError);
        std::cout<<"Stats recipe packaging passed\n";return 0;
    }catch(const std::exception& error){
        std::cerr<<error.what()<<'\n';
        auto& manager=resource::ResourceManager::instance();manager.unloadAll();manager.setDatabase({});resource::setAssetRoot({});
        std::error_code cleanupError;fs::remove_all(root,cleanupError);return 1;
    }
}
