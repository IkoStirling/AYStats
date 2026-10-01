#include <AYStats/Inspector.h>
#include <AYUI/Box.h>
#include <AYUI/TextLabel.h>
#include <AYUI/TextInput.h>
#include <AYUI/TextArea.h>
#include <AYUI/ComboBox.h>
#include <AYUI/Button.h>
#include <AYUI/UnicodeText.h>
#include <memory>
#include <system_error>
namespace ayt::stats {
namespace {
Result<std::filesystem::path> projectAssetPath(const std::filesystem::path& root,std::string_view name) {
    if(name.empty())return std::unexpected(Diagnostic{Error::Invalid,"enter a project asset path"});
    const auto relative=std::filesystem::u8path(name);
    if(relative.is_absolute() || relative.has_root_name())
        return std::unexpected(Diagnostic{Error::Invalid,"use a path relative to Assets"});
    for(const auto& component:relative)if(component=="..")
        return std::unexpected(Diagnostic{Error::Invalid,"asset path leaves project Assets",relative.generic_string()});
    std::error_code ec;
    const auto base=std::filesystem::weakly_canonical(root,ec);
    if(ec)return std::unexpected(Diagnostic{Error::Invalid,ec.message(),root.generic_string()});
    const auto path=std::filesystem::weakly_canonical(base/relative,ec);
    if(ec)return std::unexpected(Diagnostic{Error::Invalid,ec.message(),relative.generic_string()});
    const auto inside=path.lexically_relative(base);
    if(inside.empty() || inside=="." || *inside.begin()=="..")
        return std::unexpected(Diagnostic{Error::Invalid,"asset path leaves project Assets",relative.generic_string()});
    return path;
}
}
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
ayt::ui::Widget* makeStatsRecipeInspector(const std::filesystem::path& assetsRoot,
    std::shared_ptr<const Schema> targetSchema,bool editable,
    std::function<Result<void>(const std::filesystem::path&,std::string_view,std::string_view)> publish) {
    using namespace ayt::ui;
    auto* root=new VBox();root->setId("stats_recipe_inspector");root->setSpacing(3);
    const auto label=[root](std::string_view value,const char* id="") {
        auto* row=new TextLabel();row->setId(id);row->setText(decodeUtf8Text(std::string(value)));
        row->setSize({240,24});root->addWidget(row,24);return row;
    };
    label("Recipe file under project Assets");
    auto* path=new TextInput();path->setId("stats_recipe_path");path->setMaxLength(1024);
    path->setSize({240,25});root->addWidget(path,25);
    label("Source definitions file under Assets (optional)");
    auto* sourcePath=new TextInput();sourcePath->setId("stats_recipe_source_schema_path");
    sourcePath->setMaxLength(1024);sourcePath->setSize({240,25});root->addWidget(sourcePath,25);
    auto* status=label("Choose a file and load it", "stats_recipe_status");
    auto* load=new Button();load->setId("stats_recipe_load");load->setText(L"Load recipe file");
    load->setSize({240,27});root->addWidget(load,27);
    label("Named recipe");
    auto* choice=new ComboBox();choice->setId("stats_recipe_choice");choice->setSize({240,25});
    root->addWidget(choice,25);
    auto* provenance=new TextArea();provenance->setId("stats_recipe_provenance");
    provenance->setReadOnly(true);provenance->setWordWrap(true);
    provenance->setText(L"No recipe selected");provenance->setSize({240,110});
    root->addWidget(provenance,110);
    label("Exact preview inputs (comma-separated integers)");
    auto* inputs=new TextInput();inputs->setId("stats_recipe_preview_inputs");inputs->setMaxLength(8192);
    inputs->setSize({240,25});root->addWidget(inputs,25);
    auto* preview=new Button();preview->setId("stats_recipe_preview");preview->setText(L"Preview selected recipe");
    preview->setSize({240,27});root->addWidget(preview,27);
    label("Complete versioned recipe document");
    auto* document=new TextArea();document->setId("stats_recipe_document");document->setSize({240,240});
    document->setMaxLength(16*1024*1024);document->setReadOnly(!editable);
    document->setLineNumbersVisible(true);root->addWidget(document,240);
    auto* save=new Button();save->setId("stats_recipe_save");save->setText(L"Validate and save recipe file");
    save->setEnabled(editable);save->setSize({240,27});root->addWidget(save,27);
    struct Loaded {
        std::filesystem::path path;
        std::string original;
        std::shared_ptr<const Schema> sourceSchema;
        std::shared_ptr<const RecipeBook> book;
    };
    auto loaded=std::make_shared<Loaded>();
    auto showError=[status](const Diagnostic& error) {
        status->setText(decodeUtf8Text(error.message+" "+error.path));
    };
    auto describe=[choice,provenance,loaded,targetSchema] {
        const auto index=choice->getSelectedIndex();
        if(!loaded->book || index<0 || std::size_t(index)>=loaded->book->recipes().size())return;
        const auto& recipe=loaded->book->recipes()[index];
        const auto nameOf=[&](const Schema* schema,StatId id) {
            if(schema)if(const auto found=schema->index(id))return schema->definitions()[*found].name;
            return std::to_string(id);
        };
        const char* operations[]={"increase","decrease","set"};
        std::string info=recipe.name+" -> "+nameOf(targetSchema.get(),recipe.target)
            +" ("+operations[int(recipe.operation)]+")\ninputs:";
        for(std::size_t i=0;i<recipe.inputs.size();++i) {
            info+="\n"+std::to_string(i)+": ";
            const auto& binding=recipe.inputs[i];
            switch(binding.source) {
            case InputSource::Amount:info+="amount";break;
            case InputSource::Constant:info+="constant:"+std::to_string(binding.constant);break;
            case InputSource::SourceStat:info+="source:"+nameOf(loaded->sourceSchema.get(),binding.stat);break;
            case InputSource::TargetStat:info+="target:"+nameOf(targetSchema.get(),binding.stat);break;
            }
            if(binding.source==InputSource::SourceStat || binding.source==InputSource::TargetStat) {
                const char* fields[]={"effective","current","base","capacity"};
                info+=".";info+=fields[int(binding.field)];
            }
        }
        provenance->setText(decodeUtf8Text(info));
    };
    auto refreshChoices=[choice,provenance,loaded,describe] {
        std::vector<std::wstring> names;
        if(loaded->book)for(const auto& recipe:loaded->book->recipes())
            names.push_back(decodeUtf8Text(recipe.name));
        choice->setItems(names);choice->setSelectedIndex(names.empty()?-1:0);
        provenance->setText(L"No recipe selected");
        describe();
    };
    choice->setOnSelectionChanged([describe](int){describe();});
    load->setOnClicked([=] {
        if(!targetSchema){status->setText(L"Missing target schema");return;}
        auto selected=projectAssetPath(assetsRoot,encodeUtf8Text(path->getText()));
        if(!selected){showError(selected.error());return;}
        if(selected->filename().string().ends_with(".stats-recipes.json")==false) {
            status->setText(L"Choose a .stats-recipes.json file");return;
        }
        std::shared_ptr<const Schema> sourceSchema;
        if(!sourcePath->getText().empty()) {
            auto sourceFile=projectAssetPath(assetsRoot,encodeUtf8Text(sourcePath->getText()));
            if(!sourceFile){showError(sourceFile.error());return;}
            auto sourceText=readText(*sourceFile);
            if(!sourceText){showError(sourceText.error());return;}
            auto decoded=decodeDefinitions(*sourceText);
            if(!decoded){showError(decoded.error());return;}
            sourceSchema=*decoded;
        }
        auto text=readText(*selected);if(!text){showError(text.error());return;}
        auto book=decodeRecipeBook(*text,*targetSchema,sourceSchema.get());
        if(!book){showError(book.error());return;}
        loaded->path=*selected;loaded->original=*text;
        loaded->sourceSchema=std::move(sourceSchema);loaded->book=*book;
        document->setText(decodeUtf8Text(*text));
        refreshChoices();
        status->setText(L"Recipe file loaded and validated");
    });
    preview->setOnClicked([=] {
        if(!loaded->book){status->setText(L"Load a recipe file first");return;}
        const auto index=choice->getSelectedIndex();
        if(index<0 || std::size_t(index)>=loaded->book->recipes().size()){
            status->setText(L"Choose a recipe");return;
        }
        auto plan=encodePlan(loaded->book->recipes()[index].plan);
        if(!plan){showError(plan.error());return;}
        auto result=previewRecipe(*plan,encodeUtf8Text(inputs->getText()));
        if(!result){showError(result.error());return;}
        status->setText(decodeUtf8Text("Result="+std::to_string(result->result)));
    });
    save->setOnClicked([=] {
        if(loaded->path.empty() || !targetSchema){status->setText(L"Load a recipe file first");return;}
        auto book=decodeRecipeBook(encodeUtf8Text(document->getText()),*targetSchema,loaded->sourceSchema.get());
        if(!book){showError(book.error());return;}
        auto canonical=encodeRecipeBook(**book,*targetSchema,loaded->sourceSchema.get());
        if(!canonical){showError(canonical.error());return;}
        if(canonical->size()>16*1024*1024){status->setText(L"Recipe file exceeds byte limit");return;}
        auto result=publish(loaded->path,loaded->original,*canonical);
        if(!result){showError(result.error());return;}
        loaded->original=*canonical;loaded->book=*book;
        document->setText(decodeUtf8Text(*canonical));
        refreshChoices();
        status->setText(L"Recipe file saved");
    });
    return root;
}
} // namespace ayt::stats
