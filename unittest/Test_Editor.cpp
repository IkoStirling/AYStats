#include <AYTest.h>
#include <AYStats/Authoring.h>
#include <AYStats/EntityIntegration.h>
#include <AYStats/RecipeAsset.h>
#include <AYResource/AssetPath.h>
#include <AYResource/ResourceManager.h>
#include <AYEditor/EditorSceneDocument.h>
#include <AYEditor/EditorSession.h>
#include <AYEditor/EditorProjectAssetFactory.h>
#include <AYApplication/IEngineHost.h>
#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYScene.h>
#include <AYUI/Button.h>
#include <AYUI/MockRenderer.h>
#include <AYUI/TextInput.h>
#include <AYUI/TreeView.h>
#include <array>
#include <chrono>
using namespace ayt::stats;
TEST_SUITE(StatsEditorTests)
TEST_CASE(document_author_edit_undo_redo_save_reopen_preserves_zero_initial_state) {
    auto& registry=ayt::entity::ComponentRegistry::instance();
    if(!registry.isSealed()) {
        CHECK_TRUE(ayt::entity::registerEntityCoreComponents(registry).succeeded());
        CHECK_TRUE(registerStatsComponent(registry).has_value());registry.seal();
    }
    ayt::editor::EditorSceneDocument document;
    CHECK_TRUE(registerStatsSystem(document.scene().world()).has_value());
    auto schema=Schema::compile({{.name="resource",.shape=Shape::Pool,.current=0,.capacity=100}}).value();
    std::uint32_t id=0;
    CHECK_TRUE(document.createEntity("Stats owner",[&](ayt::entity::Entity& entity){
        entity.setName("StatsEditorOwner");auto* component=entity.addComponent<StatsComponent>();return component && component->configure(schema).has_value();
    },&id));
    auto* component=document.scene().world().findEntity("StatsEditorOwner")->getComponent<StatsComponent>();
    const auto before=component->definitionsJson;
    const auto after=editDefinitionField(before,statId("resource"),"capacity","200").value();
    CHECK_TRUE(document.mutateComponent(id,"StatsComponent","Capacity","",[&](ayt::entity::IComponent& base){
        auto* live=dynamic_cast<StatsComponent*>(&base);if(!live || live->definitionsJson!=before)return false;live->definitionsJson=after;return true;
    }));
    document.scene().world().fixedUpdate(.016f);CHECK_TRUE(component->read(statId("resource"))->capacity==200);
    CHECK_TRUE(document.undo());document.scene().world().fixedUpdate(.016f);
    CHECK_TRUE(component->definitionsJson==before);CHECK_TRUE(component->read(statId("resource"))->capacity==100);
    CHECK_TRUE(document.redo());document.scene().world().fixedUpdate(.016f);
    CHECK_TRUE(component->read(statId("resource"))->capacity==200);
    CHECK_TRUE(component->commit(std::array{Mutation{statId("resource"),MutationOp::Increase,20}},component->version()).has_value());
    const auto path=ayt::test::testTmpPath("stats-editor-author.scn");CHECK_TRUE(document.saveAs(path));
    CHECK_TRUE(document.open(path));CHECK_TRUE(registerStatsSystem(document.scene().world()).has_value());
    document.scene().world().fixedUpdate(.016f);
    auto* loaded=document.scene().world().findEntity("StatsEditorOwner")->getComponent<StatsComponent>();
    CHECK_TRUE(loaded->definitionsJson==after);CHECK_TRUE(loaded->read(statId("resource"))->capacity==200);
    CHECK_TRUE(loaded->read(statId("resource"))->current==0);
}
TEST_CASE(recipe_asset_document_attach_undo_reopen_and_reload) {
    namespace fs=std::filesystem;
    auto& registry=ayt::entity::ComponentRegistry::instance();
    if(!registry.isSealed()) {
        CHECK_TRUE(ayt::entity::registerEntityCoreComponents(registry).succeeded());
        CHECK_TRUE(registerStatsComponent(registry).has_value());registry.seal();
    }
    const auto root=ayt::test::testTmpDir()/("stats-editor-recipe-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto assets=root/"Assets";fs::create_directories(assets/"recipes");
    auto schema=Schema::compile({{.name="health",.shape=Shape::Pool,.current=100,.capacity=100}}).value();
    auto plan=CalculationPlan::compile({{.id=1,.op=Op::Input}},1,1).value();
    auto book=RecipeBook::compile({{"hit",statId("health"),AttributeOp::Decrease,plan,
        {{InputSource::Amount}},true}}).value();
    const auto definitions=assets/"recipes/target.stats.json";
    const auto recipeFile=assets/"recipes/hit.stats-recipes.json";
    const auto manifest=assets/"recipes/hit.srs";
    CHECK_TRUE(writeTextAtomic(definitions,encodeDefinitions(*schema).value()).has_value());
    const auto original=encodeRecipeBook(*book,*schema).value();
    CHECK_TRUE(writeTextAtomic(recipeFile,original).has_value());
    auto cooked=saveRecipeAssetSource(manifest,assets,"recipes/target.stats.json","",
        "recipes/hit.stats-recipes.json");
    CHECK_TRUE(cooked.has_value());
    ayt::resource::setAssetRoot(assets.string());
    ayt::editor::EditorSceneDocument document;
    CHECK_TRUE(registerStatsSystem(document.scene().world()).has_value());
    std::uint32_t id=0;
    CHECK_TRUE(document.createEntity("Recipe owner",[&](ayt::entity::Entity& entity){
        entity.setName("RecipeEditorOwner");
        auto* stats=entity.addComponent<StatsComponent>();
        auto* reference=entity.addComponent<StatsRecipeComponent>();
        return stats && reference && stats->configure(schema).has_value();
    },&id));
    auto* entity=document.scene().world().findEntity("RecipeEditorOwner");
    CHECK_NOT_NULL(entity);
    CHECK_TRUE(document.mutateComponent(id,"StatsRecipeComponent","Recipe asset","",[&](ayt::entity::IComponent& base){
        auto* reference=dynamic_cast<StatsRecipeComponent*>(&base);
        if(!reference || !reference->assetPath.empty())return false;
        reference->assetPath=cooked->logicalPath;return true;
    }));
    CHECK_TRUE(document.undo());
    CHECK_TRUE(entity->getComponent<StatsRecipeComponent>()->assetPath.empty());
    CHECK_TRUE(document.redo());
    CHECK_TRUE(entity->getComponent<StatsRecipeComponent>()->assetPath==cooked->logicalPath);
    const auto scene=root/"scene.scn";CHECK_TRUE(document.saveAs(scene.string()));
    CHECK_TRUE(document.open(scene.string()));
    CHECK_TRUE(registerStatsSystem(document.scene().world()).has_value());
    document.scene().world().fixedUpdate(.016f);
    entity=document.scene().world().findEntity("RecipeEditorOwner");CHECK_NOT_NULL(entity);
    CHECK_TRUE(entity->getComponent<StatsRecipeComponent>()->assetPath==cooked->logicalPath);
    auto* system=dynamic_cast<StatsSystem*>(document.scene().world().findSystemByName("StatsSystem"));
    auto handle=document.scene().world().getEntityHandle(entity->getId());
    CHECK_TRUE(system->prepareCalculation(handle,statId("health"),AttributeOp::Decrease,5,"hit").has_value());
    auto replacement=RecipeBook::compile({{"heavy",statId("health"),AttributeOp::Decrease,plan,
        {{InputSource::Amount}},true}}).value();
    auto rebuilt=saveRecipeBookAndRecook(recipeFile,assets,original,
        encodeRecipeBook(*replacement,*schema).value());
    CHECK_TRUE(rebuilt.has_value() && rebuilt->size()==1);
    ayt::resource::ResourceManager::instance().reloadResource(
        ayt::resource::resolveAssetPath({},cooked->logicalPath));
    document.scene().world().fixedUpdate(.016f);
    CHECK_FALSE(system->prepareCalculation(handle,statId("health"),AttributeOp::Decrease,5,"hit").has_value());
    CHECK_TRUE(system->prepareCalculation(handle,statId("health"),AttributeOp::Decrease,5,"heavy").has_value());
    ayt::resource::ResourceManager::instance().unloadAll();ayt::resource::setAssetRoot({});
    fs::remove_all(root);
}
TEST_CASE(recipe_asset_inspector_validates_builds_and_attaches_from_widgets) {
    namespace fs=std::filesystem;
    auto& registry=ayt::entity::ComponentRegistry::instance();
    if(!registry.isSealed()) {
        CHECK_TRUE(ayt::entity::registerEntityCoreComponents(registry).succeeded());
        CHECK_TRUE(registerStatsComponent(registry).has_value());registry.seal();
    }
    const auto root=ayt::test::testTmpDir()/("stats-editor-widgets-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto assets=root/"Assets";fs::create_directories(assets/"recipes");
    auto schema=Schema::compile({{.name="health",.shape=Shape::Pool,.current=100,.capacity=100}}).value();
    auto plan=CalculationPlan::compile({{.id=1,.op=Op::Input}},1,1).value();
    auto book=RecipeBook::compile({{"hit",statId("health"),AttributeOp::Decrease,plan,
        {{InputSource::Amount}},true}}).value();
    CHECK_TRUE(writeTextAtomic(assets/"recipes/target.stats.json",encodeDefinitions(*schema).value()).has_value());
    CHECK_TRUE(writeTextAtomic(assets/"recipes/hit.stats-recipes.json",
        encodeRecipeBook(*book,*schema).value()).has_value());
    const auto engineRoot=fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
    const auto layout=engineRoot/"EngineAssets/AYEditor/ui/editor_shell.ui.json";
    CHECK_TRUE(fs::is_regular_file(layout));
    ayt::app::EngineHostScope hostScope(ayt::app::defaultEngineHost());
    ayt::ui::MockRenderer backend;
    ayt::editor::EditorSessionDesc desc;
    desc.uiBackend=&backend;desc.layoutPath=layout.string();desc.projectRoot=root.string();
    ayt::editor::EditorSession session;
    CHECK_TRUE(session.initialize(desc));
    session.setClientSize(1280,720);
    CHECK_TRUE(session.newSceneFromTemplate(ayt::editor::EditorSceneTemplate::Empty));
    std::uint32_t id=0;
    CHECK_TRUE(session.document()->createEntity("Recipe owner",[&](ayt::entity::Entity& entity){
        entity.setName("RecipeWidgetOwner");return entity.addComponent<StatsRecipeComponent>()!=nullptr;
    },&id));
    session.update(.016f);
    auto* outliner=dynamic_cast<ayt::ui::TreeView*>(session.ui().findById("tree_outliner"));
    CHECK_NOT_NULL(outliner);
    if(outliner)outliner->setSelectedIndex(1);
    CHECK_TRUE(session.selectedEntityId()==id);
    auto* manifest=dynamic_cast<ayt::ui::TextInput*>(session.ui().findById("stats_asset_manifest"));
    auto* target=dynamic_cast<ayt::ui::TextInput*>(session.ui().findById("stats_asset_target"));
    auto* bookInput=dynamic_cast<ayt::ui::TextInput*>(session.ui().findById("stats_asset_book"));
    auto* build=dynamic_cast<ayt::ui::Button*>(session.ui().findById("stats_asset_create_attach"));
    auto* create=dynamic_cast<ayt::ui::Button*>(session.ui().findById("stats_asset_new_attach"));
    CHECK_NOT_NULL(manifest);CHECK_NOT_NULL(target);CHECK_NOT_NULL(bookInput);CHECK_NOT_NULL(build);
    CHECK_NOT_NULL(create);
    if(manifest && target && bookInput && build && create) {
        manifest->setText(L"recipes/hit.srs");
        target->setText(L"recipes/target.stats.json");
        bookInput->setText(L"recipes/missing.stats-recipes.json");
        session.ui().layout();
        const auto click=[&] {
            const auto bounds=build->getWorldBounds();
            const ayt::ui::UIMouseEvent event(ayt::math::FVector2{
                (bounds.minX+bounds.maxX)*.5f,(bounds.minY+bounds.maxY)*.5f});
            CHECK_TRUE(build->onMouseButtonDown(event));
            CHECK_TRUE(build->onMouseButtonUp(event));
        };
        click();
        CHECK_FALSE(fs::exists(assets/"recipes/hit.sts"));
        CHECK_TRUE(session.document()->scene().world().findEntity(id)
            ->getComponent<StatsRecipeComponent>()->assetPath.empty());
        bookInput->setText(L"recipes/hit.stats-recipes.json");
        click();
        CHECK_TRUE(fs::is_regular_file(assets/"recipes/hit.srs"));
        CHECK_TRUE(fs::is_regular_file(assets/"recipes/hit.sts"));
        CHECK_TRUE(session.document()->scene().world().findEntity(id)
            ->getComponent<StatsRecipeComponent>()->assetPath=="recipes/hit.sts");
        CHECK_TRUE(session.document()->undo());
        CHECK_TRUE(session.document()->scene().world().findEntity(id)
            ->getComponent<StatsRecipeComponent>()->assetPath.empty());
        CHECK_TRUE(session.document()->redo());
        CHECK_TRUE(session.document()->scene().world().findEntity(id)
            ->getComponent<StatsRecipeComponent>()->assetPath=="recipes/hit.sts");
        const auto createBounds=create->getWorldBounds();
        const ayt::ui::UIMouseEvent createEvent(ayt::math::FVector2{
            (createBounds.minX+createBounds.maxX)*.5f,
            (createBounds.minY+createBounds.maxY)*.5f});
        CHECK_TRUE(create->onMouseButtonDown(createEvent));
        CHECK_TRUE(create->onMouseButtonUp(createEvent));
        CHECK_TRUE(fs::is_regular_file(assets/"stats/NewStats.target.stats.json"));
        CHECK_TRUE(fs::is_regular_file(assets/"stats/NewStats.stats-recipes.json"));
        CHECK_TRUE(fs::is_regular_file(assets/"stats/NewStats.srs"));
        CHECK_TRUE(fs::is_regular_file(assets/"stats/NewStats.sts"));
        CHECK_TRUE(session.assetDatabase().scanNow());
        const auto* indexed=session.assetDatabase().findByLogicalPath(
            "Assets/stats/NewStats.srs");
        CHECK_NOT_NULL(indexed);
        if(indexed)CHECK_TRUE(indexed->type==ayt::editor::EditorAssetType::StatsRecipe);
        CHECK_TRUE(session.document()->scene().world().findEntity(id)
            ->getComponent<StatsRecipeComponent>()->assetPath=="stats/NewStats.sts");
        CHECK_TRUE(session.document()->undo());
        CHECK_TRUE(session.document()->scene().world().findEntity(id)
            ->getComponent<StatsRecipeComponent>()->assetPath=="recipes/hit.sts");
        const auto second=ayt::editor::createEditorProjectAsset(
            root.string(),ayt::editor::EditorAssetType::StatsRecipe);
        CHECK_TRUE(second.success);
        CHECK_TRUE(second.logicalPath=="Assets/stats/NewStats 2.srs");
        CHECK_TRUE(fs::is_regular_file(assets/"stats/NewStats 2.sts"));
    }
    session.shutdown();
    CHECK_TRUE(fs::weakly_canonical(root).parent_path()==fs::canonical(ayt::test::testTmpDir()));
    fs::remove_all(root);
}
TEST_SUITE_END
