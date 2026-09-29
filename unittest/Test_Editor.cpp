#include <AYTest.h>
#include <AYStats/Authoring.h>
#include <AYStats/EntityIntegration.h>
#include <AYEditor/EditorSceneDocument.h>
#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYScene.h>
#include <array>
using namespace ayt::stats;
TEST_SUITE(StatsEditorTests)
TEST_CASE(document_author_edit_undo_redo_save_reopen_preserves_zero_initial_state) {
    auto& registry=ayt::entity::ComponentRegistry::instance();
    CHECK_TRUE(ayt::entity::registerEntityCoreComponents(registry).succeeded());
    CHECK_TRUE(registerStatsComponent(registry).has_value());registry.seal();
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
    const auto path=ayt::test::testTmpPath("stats-editor-author.ayscene");CHECK_TRUE(document.saveAs(path));
    CHECK_TRUE(document.open(path));CHECK_TRUE(registerStatsSystem(document.scene().world()).has_value());
    document.scene().world().fixedUpdate(.016f);
    auto* loaded=document.scene().world().findEntity("StatsEditorOwner")->getComponent<StatsComponent>();
    CHECK_TRUE(loaded->definitionsJson==after);CHECK_TRUE(loaded->read(statId("resource"))->capacity==200);
    CHECK_TRUE(loaded->read(statId("resource"))->current==0);
}
TEST_SUITE_END
