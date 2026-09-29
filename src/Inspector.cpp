#include <AYStats/Inspector.h>
#include <AYUI/Box.h>
#include <AYUI/TextLabel.h>
#include <AYUI/TextInput.h>
#include <AYUI/ComboBox.h>
#include <AYUI/Button.h>
#include <AYUI/UnicodeText.h>
namespace ayt::stats {
ayt::ui::Widget* makeStatsInspector(std::string definitions,const StatSet* runtime,bool editable,std::function<void(std::string,std::string)> publish) {
    using namespace ayt::ui;auto* root=new VBox();root->setSpacing(3);root->setId("stats_inspector");
    auto label=[&](std::string text){auto* l=new TextLabel();l->setText(decodeUtf8Text(text));l->setSize({240,24});root->addWidget(l,24);return l;};
    auto* status=label("Author values; preview does not change simulation state");status->setId("stats_status");
    auto schema=definitions.empty()?Schema::compile({}):decodeDefinitions(definitions);
    if(!schema){status->setText(decodeUtf8Text(schema.error().message+" "+schema.error().path));return root;}
    if((*schema)->definitions().empty()) {
        auto* button=new Button();button->setText(L"Add basic resource example");button->setEnabled(editable);button->setSize({240,27});root->addWidget(button,27);
        button->setOnClicked([definitions,publish]{auto s=Schema::compile({{.name="resource",.shape=Shape::Pool,.current=100,.capacity=100}}).value();publish(definitions,encodeDefinitions(*s).value());});
    }
    const auto referenceName=[&](StatId id){auto index=(*schema)->index(id);return index?(*schema)->definitions()[*index].name:std::string{};};
    for(const auto& d:(*schema)->definitions()) {
        label(d.name+" ["+(d.shape==Shape::Pool?"pool":"scalar")+", "+(d.mode==Mode::Basic?"basic":"advanced")+"]");
        auto update=[definitions,id=d.id,publish,status](std::string field,std::string value){auto result=editDefinitionField(definitions,id,field,value);if(!result){status->setText(decodeUtf8Text(result.error().message+" "+result.error().path));return;}publish(definitions,*result);};
        auto text=[&](std::string field,std::string value){label(field);auto* input=new TextInput();input->setText(decodeUtf8Text(value));input->setId("stats_"+d.name+"_"+field);input->setReadOnly(!editable);input->setSize({240,25});input->setMaxLength(8192);input->setOnSubmit([update,field](const auto& v){update(field,encodeUtf8Text(v));});root->addWidget(input,25);};
        auto choice=[&](std::string field,std::vector<std::wstring> items,std::vector<std::string> values,int selected){label(field);auto* combo=new ComboBox();combo->setItems(items);combo->setSelectedIndex(selected);combo->setEnabled(editable);combo->setSize({240,25});combo->setOnSelectionChanged([update,field,values](int index){if(index>=0 && std::size_t(index)<values.size())update(field,values[index]);});root->addWidget(combo,25);};
        choice("shape",{L"Scalar",L"Pool"},{"scalar","pool"},int(d.shape));choice("mode",{L"Basic",L"Advanced"},{"basic","advanced"},int(d.mode));
        if(d.shape==Shape::Scalar || d.mode==Mode::Advanced)text("initial",std::to_string(d.initial));
        if(d.mode==Mode::Advanced){text("minimum",std::to_string(d.minimum));text("maximum",std::to_string(d.maximum));}
        if(d.shape==Shape::Pool){text("capacity",std::to_string(d.capacity));text("current",std::to_string(d.current));if(d.mode==Mode::Advanced)choice("capacityPolicy",{L"Keep current",L"Keep ratio",L"Keep missing"},{"current","ratio","missing"},int(d.capacityPolicy));}
        if(d.mode==Mode::Advanced){text("stage",std::to_string(d.stage));text("baseFrom",referenceName(d.baseFrom));if(d.shape==Shape::Pool)text("capacityFrom",referenceName(d.capacityFrom));text("derivedFactor",std::to_string(d.derivedFactor));text("derivedAdd",std::to_string(d.derivedAdd));std::string zones;for(const auto& z:d.zones){if(!zones.empty())zones+=",";const char* names[]{"flat","percent","factor","min","max","override"};zones+=std::to_string(z.id)+":"+names[int(z.op)];}text("zones",zones);}
        if(runtime){auto row=runtime->read(d.id);if(row){if(d.mode==Mode::Advanced)label("Runtime base="+std::to_string(row->base)+" effective="+std::to_string(row->effective)+" current="+std::to_string(row->current)+" capacity="+std::to_string(row->capacity));else if(d.shape==Shape::Pool)label("Runtime current="+std::to_string(row->current)+" capacity="+std::to_string(row->capacity));else label("Runtime value="+std::to_string(row->effective));}for(const auto& m:runtime->modifiers())if(m.target==d.id)label("effect="+std::to_string(m.effect)+" rule="+std::to_string(m.rule)+" stage="+std::to_string(m.stage)+" zone="+std::to_string(m.zone)+" value="+std::to_string(m.value));}
    }
    auto preview=previewDefinitions(definitions.empty()?encodeDefinitions(**schema).value():definitions);if(preview)for(std::size_t i=0;i<preview->rows().size();++i){const auto& d=preview->schema()->definitions()[i];const auto& row=preview->rows()[i];label("Preview "+d.name+(d.shape==Shape::Pool?" current="+std::to_string(row.current)+" capacity="+std::to_string(row.capacity):" effective="+std::to_string(row.effective)));}
    label("Recipe preview (JSON, CPU pure operations)");auto* plan=new TextInput();plan->setId("stats_plan");plan->setMaxLength(65536);plan->setSize({240,25});root->addWidget(plan,25);
    label("Inputs (exact integers separated by commas)");auto* inputs=new TextInput();inputs->setId("stats_inputs");inputs->setSize({240,25});inputs->setMaxLength(8192);root->addWidget(inputs,25);
    auto* result=label("No recipe preview yet");auto* button=new Button();button->setText(L"Validate and preview");button->setSize({240,27});root->addWidget(button,27);
    button->setOnClicked([plan,inputs,result]{auto output=previewRecipe(encodeUtf8Text(plan->getText()),encodeUtf8Text(inputs->getText()));if(!output){result->setText(decodeUtf8Text(output.error().message+" "+output.error().path));return;}std::string text="Result="+std::to_string(output->result);for(const auto& [stage,value]:output->stages)text+="; "+std::to_string(stage)+"="+std::to_string(value);result->setText(decodeUtf8Text(text));});
    return root;
}
} // namespace ayt::stats
