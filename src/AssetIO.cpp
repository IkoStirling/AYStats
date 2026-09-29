#include <AYStats/AssetIO.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <atomic>
#include <chrono>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace ayt::stats {
namespace {
using Json=nlohmann::json;
auto fail(Error e,std::string m){return std::unexpected(Diagnostic{e,std::move(m),{}});}
std::uint64_t u(const Json& j) {if(!j.is_number_integer() || (!j.is_number_unsigned() && j.get<Value>()<0))throw std::runtime_error("expected unsigned integer");return j.get<std::uint64_t>();}
Value v(const Json& j) {if(!j.is_number_integer() || (j.is_number_unsigned() && j.get<std::uint64_t>()>std::uint64_t(INT64_MAX)))throw std::runtime_error("expected signed 64-bit integer");return j.get<Value>();}
template<class E> E enumeration(const Json& j,std::uint64_t max){auto value=u(j);if(value>max)throw std::runtime_error("unknown enum");return static_cast<E>(value);}
std::uint32_t u32(const Json& j){auto value=u(j);if(value>UINT32_MAX)throw std::runtime_error("uint32 overflow");return std::uint32_t(value);}
void limit(const Json& j,std::size_t max){if(!j.is_array() || j.size()>max)throw std::runtime_error("array limit/type");}
Json parse(std::string_view text){if(text.size()>16*1024*1024)throw std::runtime_error("document byte limit");return Json::parse(text,[](int depth,Json::parse_event_t,Json&){if(depth>32)throw std::runtime_error("document nesting limit");return true;});}
void version(const Json& j,const char* kind){if(u(j.at("version"))!=1 || j.at("kind")!=kind)throw std::runtime_error("unsupported version/document kind");}
std::string ref(const Schema& schema,StatId id){if(!id)return {};return schema.definitions()[schema.index(id).value()].name;}
Json definitionJson(const Schema& schema) {
    Json array=Json::array();
    for(const auto& d:schema.definitions()) {
        if(d.lifetime!=Lifetime::Persistent)throw std::runtime_error("intermediate definitions are not persistent assets");
        Json zones=Json::array();for(const auto& z:d.zones)zones.push_back({{"id",z.id},{"op",int(z.op)}});
        array.push_back({{"name",d.name},{"shape",int(d.shape)},{"mode",int(d.mode)},{"initial",d.initial},{"current",d.current},{"capacity",d.capacity},{"minimum",d.minimum},{"maximum",d.maximum},{"capacityPolicy",int(d.capacityPolicy)},{"capacityFrom",ref(schema,d.capacityFrom)},{"baseFrom",ref(schema,d.baseFrom)},{"derivedFactor",d.derivedFactor},{"derivedAdd",d.derivedAdd},{"stage",d.stage},{"zones",zones}});
    }
    return Json{{"version",1},{"kind","stats-definitions"},{"definitions",array}};
}
Json modJson(const Modifier& m,const Schema& schema){return {{"handle",m.handle},{"effect",m.effect},{"rule",m.rule},{"hit",m.hit},{"layer",m.layer},{"target",ref(schema,m.target)},{"stage",m.stage},{"zone",m.zone},{"value",m.value},{"priority",m.priority}};}
Modifier readMod(const Json& j) {
    const auto priority=v(j.at("priority"));if(priority<INT32_MIN || priority>INT32_MAX)throw std::runtime_error("priority overflow");
    return {.handle=u(j.at("handle")),.effect=u(j.at("effect")),.rule=u(j.at("rule")),.hit=u(j.at("hit")),.layer=u32(j.at("layer")),.target=statId(j.at("target").get<std::string>()),.stage=u32(j.at("stage")),.zone=u32(j.at("zone")),.value=v(j.at("value")),.priority=std::int32_t(priority)};
}
Json stateJson(const StatSet& state) {
    Json rows=Json::array(),mods=Json::array();const auto& schema=*state.schema();
    for(std::size_t i=0;i<state.rows().size();++i){const auto& r=state.rows()[i];rows.push_back({{"name",schema.definitions()[i].name},{"base",r.base},{"current",r.current},{"capacity",r.capacity}});}
    for(const auto& m:state.modifiers())mods.push_back(modJson(m,schema));
    return {{"version",1},{"kind","stats-state"},{"definitions",definitionJson(schema)},{"stateVersion",state.version()},{"rows",rows},{"modifiers",mods}};
}
}
Result<std::string> encodeDefinitions(const Schema& schema){try{return definitionJson(schema).dump();}catch(const std::exception& e){return fail(Error::Invalid,e.what());}}
Result<std::shared_ptr<const Schema>> decodeDefinitions(std::string_view text) {
    try {
        auto json=parse(text);version(json,"stats-definitions");const auto& array=json.at("definitions");limit(array,65536);std::vector<Definition> defs;
        for(const auto& j:array) {
            Definition d;d.name=j.at("name").get<std::string>();d.shape=enumeration<Shape>(j.at("shape"),1);d.mode=enumeration<Mode>(j.at("mode"),1);
            d.initial=v(j.at("initial"));d.current=v(j.at("current"));d.capacity=v(j.at("capacity"));d.minimum=v(j.at("minimum"));d.maximum=v(j.at("maximum"));d.capacityPolicy=enumeration<CapacityPolicy>(j.at("capacityPolicy"),2);
            auto name=j.at("capacityFrom").get<std::string>();if(!name.empty())d.capacityFrom=statId(name);name=j.at("baseFrom").get<std::string>();if(!name.empty())d.baseFrom=statId(name);
            d.derivedFactor=v(j.at("derivedFactor"));d.derivedAdd=v(j.at("derivedAdd"));d.stage=u32(j.at("stage"));limit(j.at("zones"),256);
            for(const auto& z:j.at("zones"))d.zones.push_back({u32(z.at("id")),enumeration<ZoneOp>(z.at("op"),5)});defs.push_back(std::move(d));
        }
        return Schema::compile(std::move(defs));
    }catch(const std::exception& e){return fail(Error::Invalid,e.what());}
}
Result<std::string> encodeState(const StatSet& state){try{return stateJson(state).dump();}catch(const std::exception& e){return fail(Error::Invalid,e.what());}}
Result<std::string> encodePlan(const CalculationPlan& plan) {
    try {
        Json nodes=Json::array();for(const auto& inst:plan.instructions()){const auto& n=inst.node;nodes.push_back({{"id",n.id},{"op",int(n.op)},{"a",n.a},{"b",n.b},{"c",n.c},{"binding",n.binding},{"value",n.value},{"table",n.table},{"extension",n.extension}});}
        return Json{{"version",1},{"kind","stats-plan"},{"inputs",plan.inputCount()},{"output",plan.instructions()[plan.outputIndex()].node.id},{"nodes",nodes}}.dump();
    }catch(const std::exception& e){return fail(Error::Invalid,e.what());}
}
Result<CalculationPlan> decodePlan(std::string_view text,Extensions extensions) {
    try {
        const auto j=parse(text);version(j,"stats-plan");limit(j.at("nodes"),4096);std::vector<Node> nodes;
        for(const auto& n:j.at("nodes")){Node node{.id=u32(n.at("id")),.op=enumeration<Op>(n.at("op"),std::uint64_t(Op::Extension)),.a=u32(n.at("a")),.b=u32(n.at("b")),.c=u32(n.at("c")),.binding=u32(n.at("binding")),.value=v(n.at("value"))};limit(n.at("table"),65536);for(const auto& entry:n.at("table"))node.table.push_back(v(entry));node.extension=n.at("extension").get<std::string>();nodes.push_back(std::move(node));}
        return CalculationPlan::compile(std::move(nodes),u32(j.at("output")),u32(j.at("inputs")),std::move(extensions));
    }catch(const std::exception& e){return fail(Error::Invalid,e.what());}
}
Result<StatSet> decodeState(std::string_view text,std::shared_ptr<const Schema> expected) {
    try {
        if(!expected)return fail(Error::Invalid,"null expected schema");auto j=parse(text);version(j,"stats-state");
        auto saved=decodeDefinitions(j.at("definitions").dump());if(!saved)return std::unexpected(saved.error());
        if(definitionJson(**saved)!=definitionJson(*expected))return fail(Error::Stale,"definition contract mismatch");
        limit(j.at("rows"),65536);limit(j.at("modifiers"),65536);if(j.at("rows").size()!=expected->definitions().size())return fail(Error::Invalid,"saved row count mismatch");
        std::vector<Row> rows(expected->definitions().size());std::vector<std::uint8_t> seen(rows.size());
        for(const auto& row:j.at("rows")){auto id=statId(row.at("name").get<std::string>());auto index=expected->index(id);if(!index || seen[*index])return fail(Error::Duplicate,"unknown/duplicate saved row");seen[*index]=1;rows[*index]={v(row.at("base")),v(row.at("current")),v(row.at("capacity")),0};}
        std::vector<Modifier> mods;for(const auto& m:j.at("modifiers"))mods.push_back(readMod(m));return StatSet::restore(std::move(expected),rows,mods,u(j.at("stateVersion")));
    }catch(const std::exception& e){return fail(Error::Invalid,e.what());}
}
Result<std::string> encodeCheckpoint(const RuntimeCheckpoint& cp) {
    try {
        Json effects=Json::array();for(const auto& effect:cp.effects.effects()){Json mods=Json::array();for(const auto& m:effect.modifiers)mods.push_back(modJson(m,*cp.state.schema()));effects.push_back({{"id",effect.id},{"key",effect.key},{"expires",effect.expires},{"maxStacks",effect.maxStacks},{"modifiers",mods}});}
        const auto& c=cp.context;return Json{{"version",1},{"kind","stats-checkpoint"},{"state",stateJson(cp.state)},{"effects",effects},{"tick",cp.effects.tick()},{"context",{{"seed",c.seed},{"tick",c.tick},{"calculation",c.calculation},{"hit",c.hit},{"parent",c.parent},{"depth",c.depth}}}}.dump();
    }catch(const std::exception& e){return fail(Error::Invalid,e.what());}
}
Result<RuntimeCheckpoint> decodeCheckpoint(std::string_view text,std::shared_ptr<const Schema> expected) {
    try {
        auto j=parse(text);version(j,"stats-checkpoint");auto state=decodeState(j.at("state").dump(),std::move(expected));if(!state)return std::unexpected(state.error());
        limit(j.at("effects"),65536);std::vector<Effect> effects;
        for(const auto& e:j.at("effects")){Effect effect{.id=u(e.at("id")),.key=u(e.at("key")),.expires=u(e.at("expires")),.maxStacks=u32(e.at("maxStacks"))};limit(e.at("modifiers"),65536);for(const auto& m:e.at("modifiers"))effect.modifiers.push_back(readMod(m));effects.push_back(std::move(effect));}
        auto manager=EffectManager::restore(*state,std::move(effects),u(j.at("tick")));if(!manager)return std::unexpected(manager.error());
        const auto& c=j.at("context");CalculationContext context{u(c.at("seed")),u(c.at("tick")),u(c.at("calculation")),u(c.at("hit")),u(c.at("parent")),u32(c.at("depth"))};
        return RuntimeCheckpoint{std::move(*state),std::move(*manager),context};
    }catch(const std::exception& e){return fail(Error::Invalid,e.what());}
}
Result<void> writeTextAtomic(const std::filesystem::path& path,std::string_view text) {
    if(text.size()>16*1024*1024)return fail(Error::Limit,"file byte limit");
    static std::atomic_uint64_t serial=0;
    auto temp=path;const auto stamp=std::chrono::high_resolution_clock::now().time_since_epoch().count();
#ifdef _WIN32
    const auto process=GetCurrentProcessId();
#else
    const auto process=std::uint64_t(reinterpret_cast<std::uintptr_t>(&serial));
#endif
    temp+=std::filesystem::path(".stats-"+std::to_string(process)+"-"+std::to_string(stamp)+"-"+std::to_string(++serial)+".tmp");
    try {
        {std::ofstream file(temp,std::ios::binary|std::ios::trunc);if(!file)return fail(Error::Invalid,"cannot open temporary file");file.write(text.data(),std::streamsize(text.size()));file.flush();if(!file){file.close();std::filesystem::remove(temp);return fail(Error::Invalid,"failed writing temporary file");}}
#ifdef _WIN32
        if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){std::filesystem::remove(temp);return fail(Error::Invalid,"atomic file replace failed");}
#else
        std::filesystem::rename(temp,path);
#endif
        return {};
    }catch(const std::exception& e){std::error_code ignored;std::filesystem::remove(temp,ignored);return fail(Error::Invalid,e.what());}
}
Result<std::string> readText(const std::filesystem::path& path,std::size_t maxBytes) {
    try {const auto size=std::filesystem::file_size(path);if(size>maxBytes)return fail(Error::Limit,"file byte limit");std::string data(std::size_t(size),'\0');std::ifstream file(path,std::ios::binary);if(!file || !file.read(data.data(),std::streamsize(size)))return fail(Error::Invalid,"file read failed");return data;}catch(const std::exception& e){return fail(Error::Invalid,e.what());}
}
} // namespace ayt::stats
