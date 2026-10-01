#include <AYTest.h>
#include <AYStats/Inspector.h>
#include <AYTestFixtures.h>
#include <AYUI/Widget.h>
#include <AYUI/Button.h>
#include <AYUI/ComboBox.h>
#include <AYUI/TextArea.h>
#include <AYUI/TextInput.h>
#include <AYUI/TextLabel.h>
#include <AYUI/UnicodeText.h>
#include <AYUI/UIManager.h>
using namespace ayt::stats;
namespace {ayt::ui::Widget* find(ayt::ui::Widget* root,const std::string& id){if(root->getId()==id)return root;for(auto* child:root->getChildren())if(auto* result=find(child,id))return result;return nullptr;}}
namespace {void click(ayt::ui::Button* button){auto at=button->getWorldBounds().getMin()+ayt::math::FVector2{10,10};ayt::ui::UIMouseEvent event(at,0);button->onMouseMove(event);button->onMouseButtonDown(event);button->onMouseButtonUp(event);}}
TEST_SUITE(StatsInspectorTests)
TEST_CASE(exact_author_fields_and_readonly_runtime_inspection) {
    auto schema=Schema::compile({{.name="health",.shape=Shape::Pool,.current=0,.capacity=100}}).value();auto state=StatSet::create(schema).value();const auto original=encodeDefinitions(*schema).value();
    int published=0;auto* root=makeStatsInspector(original,&state,true,[&](auto,auto){++published;});
    auto* current=dynamic_cast<ayt::ui::TextInput*>(find(root,"stats_health_current"));CHECK_NOT_NULL(current);CHECK_TRUE(current->getText()==L"0");CHECK_FALSE(current->isReadOnly());
    CHECK_TRUE(state.read(statId("health"))->current==0);CHECK_TRUE(published==0);CHECK_NOT_NULL(find(root,"stats_plan"));ayt::ui::destroyWidgetTree(root);
    root=makeStatsInspector(original,&state,false,[&](auto,auto){++published;});current=dynamic_cast<ayt::ui::TextInput*>(find(root,"stats_health_current"));CHECK_TRUE(current->isReadOnly());ayt::ui::destroyWidgetTree(root);
}
TEST_CASE(recipe_inspector_load_select_preview_and_validate_before_publish) {
    auto rootPath=ayt::test::testTmpDir()/"stats-inspector-recipes";
    std::filesystem::create_directories(rootPath);
    auto schema=Schema::compile({{.name="health",.shape=Shape::Pool,.current=50,.capacity=100}}).value();
    auto plan=CalculationPlan::compile({{.id=1,.op=Op::Input}},1,1).value();
    auto book=RecipeBook::compile({{"hit",statId("health"),AttributeOp::Decrease,plan,{{InputSource::Amount}},true}}).value();
    const auto original=encodeRecipeBook(*book,*schema).value();
    const auto file=rootPath/"hit.stats-recipes.json";
    CHECK_TRUE(writeTextAtomic(file,original).has_value());
    int published=0;
    auto* widget=makeStatsRecipeInspector(rootPath,schema,true,[&](const auto& path,std::string_view before,std::string_view after)->Result<void>{
        CHECK_TRUE(path==std::filesystem::weakly_canonical(file));
        CHECK_TRUE(before==original);
        ++published;return writeTextAtomic(path,after);
    });
    widget->setSize({260,700});widget->performLayout();
    auto* path=dynamic_cast<ayt::ui::TextInput*>(find(widget,"stats_recipe_path"));
    auto* load=dynamic_cast<ayt::ui::Button*>(find(widget,"stats_recipe_load"));
    auto* save=dynamic_cast<ayt::ui::Button*>(find(widget,"stats_recipe_save"));
    auto* preview=dynamic_cast<ayt::ui::Button*>(find(widget,"stats_recipe_preview"));
    auto* choices=dynamic_cast<ayt::ui::ComboBox*>(find(widget,"stats_recipe_choice"));
    auto* inputs=dynamic_cast<ayt::ui::TextInput*>(find(widget,"stats_recipe_preview_inputs"));
    auto* document=dynamic_cast<ayt::ui::TextArea*>(find(widget,"stats_recipe_document"));
    auto* status=dynamic_cast<ayt::ui::TextLabel*>(find(widget,"stats_recipe_status"));
    CHECK_NOT_NULL(path);CHECK_NOT_NULL(load);CHECK_NOT_NULL(save);CHECK_NOT_NULL(preview);
    CHECK_NOT_NULL(choices);CHECK_NOT_NULL(inputs);CHECK_NOT_NULL(document);CHECK_NOT_NULL(status);
    path->setText(L"hit.stats-recipes.json");click(load);
    CHECK_TRUE(choices->getItemCount()==1 && choices->getSelectedItem()==L"hit");
    inputs->setText(L"42");click(preview);CHECK_TRUE(status->getText()==L"Result=42");
    auto invalid=original;invalid.replace(invalid.find("\"target\":\"health\""),17,"\"target\":\"missing\"");
    document->setText(ayt::ui::decodeUtf8Text(invalid));click(save);
    CHECK_TRUE(published==0 && readText(file).value()==original);
    document->setText(ayt::ui::decodeUtf8Text(original));click(save);
    CHECK_TRUE(published==1 && readText(file).value()==original);
    path->setText(L"../hit.stats-recipes.json");click(load);
    CHECK_MSG(status->getText().find(L"leaves project Assets")!=std::wstring::npos,
              ayt::ui::encodeUtf8Text(status->getText()));
    auto sourceSchema=Schema::compile({{.name="attack",.initial=17}}).value();
    auto sourced=RecipeBook::compile({{"from-source",statId("health"),AttributeOp::Decrease,
        plan,{{InputSource::SourceStat,statId("attack")}},true}}).value();
    CHECK_TRUE(writeTextAtomic(rootPath/"source.stats.json",encodeDefinitions(*sourceSchema).value()).has_value());
    CHECK_TRUE(writeTextAtomic(rootPath/"source.stats-recipes.json",
        encodeRecipeBook(*sourced,*schema,sourceSchema.get()).value()).has_value());
    path->setText(L"source.stats-recipes.json");click(load);
    CHECK_TRUE(status->getText().find(L"source schema required")!=std::wstring::npos);
    auto* sourcePath=dynamic_cast<ayt::ui::TextInput*>(find(widget,"stats_recipe_source_schema_path"));
    sourcePath->setText(L"source.stats.json");click(load);
    CHECK_TRUE(choices->getItemCount()==1 && choices->getSelectedItem()==L"from-source");
    inputs->setText(L"17");click(preview);CHECK_TRUE(status->getText()==L"Result=17");
    ayt::ui::destroyWidgetTree(widget);
    widget=makeStatsRecipeInspector(rootPath,schema,false,[](const auto&,auto,auto)->Result<void>{return {};});
    CHECK_TRUE(!dynamic_cast<ayt::ui::Button*>(find(widget,"stats_recipe_save"))->isEnabled());
    CHECK_TRUE(dynamic_cast<ayt::ui::TextArea*>(find(widget,"stats_recipe_document"))->isReadOnly());
    ayt::ui::destroyWidgetTree(widget);
}
TEST_SUITE_END
