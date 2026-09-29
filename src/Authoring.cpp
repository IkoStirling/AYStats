#include <AYStats/Authoring.h>
#include <charconv>
namespace ayt::stats {
namespace {
auto fail(std::string text){return std::unexpected(Diagnostic{Error::Invalid,std::move(text),{}});}
Result<Value> number(std::string_view text){Value value=0;auto r=std::from_chars(text.data(),text.data()+text.size(),value);if(r.ec!=std::errc{} || r.ptr!=text.data()+text.size())return fail("expected an exact decimal integer");return value;}
}
Result<std::string> editDefinitionField(std::string_view text,StatId id,std::string_view field,std::string_view value) {
    auto schema=decodeDefinitions(text);if(!schema)return std::unexpected(schema.error());auto index=(*schema)->index(id);if(!index)return fail("author definition missing");
    std::vector<Definition> definitions((*schema)->definitions().begin(),(*schema)->definitions().end());auto& d=definitions[*index];
    if(field=="mode") {
        if(value=="basic") {if(!d.zones.empty() || d.baseFrom || d.capacityFrom)return fail("remove advanced zones/references before selecting basic mode");d.mode=Mode::Basic;}
        else if(value=="advanced"){d.mode=Mode::Advanced;if(d.zones.empty())d.zones={{1,ZoneOp::Flat},{2,ZoneOp::PercentSum},{3,ZoneOp::FactorProduct}};}else return fail("unknown mode");
    } else if(field=="shape") {if(value=="scalar")d.shape=Shape::Scalar;else if(value=="pool")d.shape=Shape::Pool;else return fail("unknown shape");}
    else if(field=="capacityPolicy") {if(value=="current")d.capacityPolicy=CapacityPolicy::KeepCurrent;else if(value=="ratio")d.capacityPolicy=CapacityPolicy::KeepRatio;else if(value=="missing")d.capacityPolicy=CapacityPolicy::KeepMissing;else return fail("unknown capacity policy");}
    else if(field=="baseFrom" || field=="capacityFrom") {const auto ref=value.empty()?0:statId(value);if(field=="baseFrom")d.baseFrom=ref;else d.capacityFrom=ref;}
    else if(field=="zones") {
        d.zones.clear();std::size_t start=0;
        while(start<value.size()) {const auto end=value.find(',',start);const auto entry=value.substr(start,end==std::string_view::npos?value.size()-start:end-start);const auto colon=entry.find(':');if(colon==std::string_view::npos)return fail("zones use id:flat, id:percent, id:factor, id:min, id:max or id:override without spaces");auto zoneId=number(entry.substr(0,colon));if(!zoneId || *zoneId<0 || *zoneId>UINT32_MAX)return fail("invalid zone id");ZoneOp op;const auto type=entry.substr(colon+1);if(type=="flat")op=ZoneOp::Flat;else if(type=="percent")op=ZoneOp::PercentSum;else if(type=="factor")op=ZoneOp::FactorProduct;else if(type=="min")op=ZoneOp::Minimum;else if(type=="max")op=ZoneOp::Maximum;else if(type=="override")op=ZoneOp::Override;else return fail("unknown zone operation");d.zones.push_back({std::uint32_t(*zoneId),op});if(end==std::string_view::npos)break;start=end+1;}
    } else {
        auto parsed=number(value);if(!parsed)return std::unexpected(parsed.error());const auto n=*parsed;
        if(field=="initial")d.initial=n;else if(field=="current")d.current=n;else if(field=="capacity")d.capacity=n;else if(field=="minimum")d.minimum=n;else if(field=="maximum")d.maximum=n;else if(field=="derivedFactor")d.derivedFactor=n;else if(field=="derivedAdd")d.derivedAdd=n;else if(field=="stage" && n>=0 && n<=UINT32_MAX)d.stage=std::uint32_t(n);else return fail("unknown field or invalid stage");
    }
    auto compiled=Schema::compile(std::move(definitions));if(!compiled)return std::unexpected(compiled.error());auto preview=StatSet::create(*compiled);if(!preview)return std::unexpected(preview.error());return encodeDefinitions(**compiled);
}
Result<StatSet> previewDefinitions(std::string_view text,std::span<const Modifier> modifiers) {
    auto schema=decodeDefinitions(text);if(!schema)return std::unexpected(schema.error());auto set=StatSet::create(*schema);if(!set)return set;
    for(const auto& m:modifiers)if(auto result=set->insert(m);!result)return std::unexpected(result.error());return set;
}
Result<CalculationTrace> previewRecipe(std::string_view planText,std::string_view csv,CalculationContext context) {
    auto plan=decodePlan(planText);if(!plan)return std::unexpected(plan.error());std::vector<Value> inputs;std::size_t start=0;
    while(start<csv.size()){const auto end=csv.find(',',start);auto value=number(csv.substr(start,end==std::string_view::npos?csv.size()-start:end-start));if(!value)return std::unexpected(value.error());inputs.push_back(*value);if(end==std::string_view::npos)break;start=end+1;if(start==csv.size())return fail("empty trailing CSV input");}
    if(inputs.size()!=plan->inputCount())return fail("recipe input count mismatch");return trace(*plan,inputs,context,256);
}
} // namespace ayt::stats
