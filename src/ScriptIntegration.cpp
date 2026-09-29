#include <AYStats/ScriptIntegration.h>
#include <AYEntity.h>
#include <AYScript/logia/MethodInfoImpl.h>
#include <charconv>
namespace ayt::stats {
StatsComponent* StatsScriptFacade::resolve() {
    if(!_world){_error="Stats script receiver is unbound";return nullptr;}
    auto* system=dynamic_cast<StatsSystem*>(_world->findSystemByName("StatsSystem"));
    auto* entity=system && system->worldToken()==_worldToken?_world->getEntityByHandle(_entity):nullptr;
    auto* stats=entity?entity->getComponent<StatsComponent>():nullptr;
    if(!stats || stats->generation()!=_generation){_error="Stats script receiver is stale";return nullptr;}
    _error.clear();return stats;
}
Result<void> StatsScriptFacade::bind(ayt::entity::World& world,ayt::entity::EntityHandle entity) {
    auto* system=dynamic_cast<StatsSystem*>(world.findSystemByName("StatsSystem"));auto* owner=world.getEntityByHandle(entity);auto* stats=owner?owner->getComponent<StatsComponent>():nullptr;
    if(!system || !stats || !stats->state())return std::unexpected(Diagnostic{Error::Invalid,"script binding requires a configured Stats owner",{}});
    _world=&world;_entity=entity;_worldToken=system->worldToken();_generation=stats->generation();_error.clear();return {};
}
std::string StatsScriptFacade::version(){auto* stats=resolve();return stats?std::to_string(stats->version()):"";}
std::string StatsScriptFacade::read(std::string name,std::string field) {
    auto* stats=resolve();if(!stats)return "";auto row=stats->read(statId(name));if(!row){_error=row.error().message;return "";}
    if(field=="base")return std::to_string(row->base);if(field=="effective")return std::to_string(row->effective);if(field=="current")return std::to_string(row->current);if(field=="capacity")return std::to_string(row->capacity);
    _error="unknown Stats field";return "";
}
bool StatsScriptFacade::change(std::string name,std::string operation,std::string amount,std::string expectedVersion) {
    auto* stats=resolve();if(!stats)return false;Value value=0;std::uint64_t versionValue=0;
    const auto a=std::from_chars(amount.data(),amount.data()+amount.size(),value),v=std::from_chars(expectedVersion.data(),expectedVersion.data()+expectedVersion.size(),versionValue);
    if(a.ec!=std::errc{} || a.ptr!=amount.data()+amount.size() || v.ec!=std::errc{} || v.ptr!=expectedVersion.data()+expectedVersion.size()){_error="amount/version must be exact decimal integers";return false;}
    MutationOp op;
    if(operation=="setBase")op=MutationOp::SetBase;else if(operation=="setCurrent")op=MutationOp::SetCurrent;else if(operation=="increase")op=MutationOp::Increase;else if(operation=="decrease")op=MutationOp::Decrease;else if(operation=="setCapacity")op=MutationOp::SetCapacity;else if(operation=="restore")op=MutationOp::Restore;else {_error="unknown Stats operation";return false;}
    const Mutation mutation{statId(name),op,value};auto result=stats->commit(std::span(&mutation,1),versionValue);if(!result){_error=result.error().message;return false;}return true;
}
Result<void> registerStatsScriptFacade() {
    auto& registry=ayt::reflect::TypeRegistryImpl::instance();if(registry.findType<StatsScriptFacade>())return {};
    using Info=ayt::reflect::TypeInfoImpl<StatsScriptFacade>;auto* info=new Info("StatsScriptFacade",ayt::reflect::detail::defaultCreate<StatsScriptFacade>,ayt::reflect::detail::defaultDestroy<StatsScriptFacade>,ayt::reflect::detail::defaultCopy<StatsScriptFacade>);
    using namespace ayt::script::logia::reflect;
    info->addMethod(new MethodInfoImpl<StatsScriptFacade,std::string,std::string,std::string>("read",&StatsScriptFacade::read));
    info->addMethod(new MethodInfoImpl<StatsScriptFacade,std::string>("version",&StatsScriptFacade::version));
    info->addMethod(new MethodInfoImpl<StatsScriptFacade,bool,std::string,std::string,std::string,std::string>("change",&StatsScriptFacade::change));
    info->addMethod(new MethodInfoImplConst<StatsScriptFacade,std::string>("error",&StatsScriptFacade::error));
    registry.registerTypeInfo("StatsScriptFacade",info);return {};
}
} // namespace ayt::stats
