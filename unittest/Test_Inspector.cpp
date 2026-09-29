#include <AYTest.h>
#include <AYStats/Inspector.h>
#include <AYUI/Widget.h>
#include <AYUI/TextInput.h>
#include <AYUI/TextLabel.h>
#include <AYUI/UIManager.h>
using namespace ayt::stats;
namespace {ayt::ui::Widget* find(ayt::ui::Widget* root,const std::string& id){if(root->getId()==id)return root;for(auto* child:root->getChildren())if(auto* result=find(child,id))return result;return nullptr;}}
TEST_SUITE(StatsInspectorTests)
TEST_CASE(exact_author_fields_and_readonly_runtime_inspection) {
    auto schema=Schema::compile({{.name="health",.shape=Shape::Pool,.current=0,.capacity=100}}).value();auto state=StatSet::create(schema).value();const auto original=encodeDefinitions(*schema).value();
    int published=0;auto* root=makeStatsInspector(original,&state,true,[&](auto,auto){++published;});
    auto* current=dynamic_cast<ayt::ui::TextInput*>(find(root,"stats_health_current"));CHECK_NOT_NULL(current);CHECK_TRUE(current->getText()==L"0");CHECK_FALSE(current->isReadOnly());
    CHECK_TRUE(state.read(statId("health"))->current==0);CHECK_TRUE(published==0);CHECK_NOT_NULL(find(root,"stats_plan"));ayt::ui::destroyWidgetTree(root);
    root=makeStatsInspector(original,&state,false,[&](auto,auto){++published;});current=dynamic_cast<ayt::ui::TextInput*>(find(root,"stats_health_current"));CHECK_TRUE(current->isReadOnly());ayt::ui::destroyWidgetTree(root);
}
TEST_SUITE_END
