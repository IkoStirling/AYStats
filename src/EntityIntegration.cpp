#include <AYStats/EntityIntegration.h>
#include <AYEntity.h>
#include <AYEntity/ComponentRegistration.h>
#include <AYEntity/EntityRuntimeModule.h>
#include <atomic>
#include <array>
namespace ayt::stats {
AY_FINALIZE_REGISTRATION_METADATA(StatsComponent)
namespace {
std::atomic_uint64_t identities=0;
auto fail(Error e,std::string m){return std::unexpected(Diagnostic{e,std::move(m),{}});}
}
StatsComponent::StatsComponent():_generation(++identities){}
void StatsComponent::onAttach(ayt::entity::Entity*){_generation=++identities;}
void StatsComponent::onStart(){if(auto r=synchronize();!r)_error=r.error();}
Result<void> StatsComponent::configure(std::shared_ptr<const Schema> schema) {
    if(_notifying)return fail(Error::Reentrant,"configure during settlement notification");
    if(!schema)return fail(Error::Invalid,"null Stats schema");
    auto encoded=encodeDefinitions(*schema);if(!encoded)return std::unexpected(encoded.error());
    auto state=StatSet::create(std::move(schema));if(!state)return std::unexpected(state.error());
    _state=std::move(*state);_effects={};_context={};_pending.clear();_deferredSettlements.clear();_generation=++identities;_remoteVersion=0;
    definitionsJson=*encoded;_appliedDefinition=*encoded;_error.reset();publish();return {};
}
Result<Row> StatsComponent::read(StatId id) const {if(!_state)return fail(Error::Invalid,"Stats not configured");return _state->read(id);}
Result<Settlement> StatsComponent::commit(std::span<const Mutation> operations,std::uint64_t expected,NotificationDelivery delivery) {
    if(_notifying)return fail(Error::Reentrant,"enqueue commands instead of reentering settlement");
    if(!_authority)return fail(Error::Invalid,"replica cannot commit authoritative state");
    if(!_state)return fail(Error::Invalid,"Stats not configured");
    if(delivery!=NotificationDelivery::Immediate && delivery!=NotificationDelivery::NextTick)return fail(Error::Invalid,"invalid notification delivery");
    Settlement notification;
    if(delivery==NotificationDelivery::NextTick) {
        if(_deferredSettlements.size()>=1024)return fail(Error::Limit,"deferred settlement notification limit");
        _deferredSettlements.reserve(1024);notification.changes.reserve(operations.size());
    }
    auto result=_state->commit(operations,expected);
    if(result){
        publish();
        if(delivery==NotificationDelivery::Immediate)notify(*result);
        else {notification=*result;_deferredSettlements.push_back(std::move(notification));}
    }
    return result;
}
void StatsComponent::notify(const Settlement& result) {
    if(!_onSettled)return;auto callback=_onSettled;_notifying=true;
    try {callback(result);}catch(...){_error=Diagnostic{Error::Invalid,"settlement notification threw after commit",{}};}
    _notifying=false;
}
Result<void> StatsComponent::enqueue(std::vector<Mutation> mutations) {
    if(!_authority || !_state)return fail(Error::Invalid,"queued mutation requires configured authority");
    if(_pending.size()>=1024 || mutations.empty() || mutations.size()>65536)return fail(Error::Limit,"queued mutation limit/empty command");
    _pending.push_back(std::move(mutations));return {};
}
Result<void> StatsComponent::applyEffect(Effect effect,EffectPolicy policy,std::uint64_t tick) {
    if(_notifying)return fail(Error::Reentrant,"effect application during notification");
    if(!_authority || !_state)return fail(Error::Invalid,"effect requires configured authority");
    auto result=_effects.apply(*_state,std::move(effect),policy,tick);if(result)publish();return result;
}
Result<std::string> StatsComponent::checkpoint() const {
    if(!_state)return fail(Error::Invalid,"Stats not configured");return encodeCheckpoint({*_state,_effects,_context});
}
Result<void> StatsComponent::restoreCheckpoint(std::string_view text) {
    if(_notifying)return fail(Error::Reentrant,"restore during notification");
    if(!_state || !_authority)return fail(Error::Invalid,"restore requires configured authority");
    auto restored=decodeCheckpoint(text,_state->schema());if(!restored)return std::unexpected(restored.error());
    _state=std::move(restored->state);_effects=std::move(restored->effects);_context=restored->context;_pending.clear();_deferredSettlements.clear();_generation=++identities;publish();return {};
}
void StatsComponent::publish() {
    if(!_state)return;
    auto text=encodeState(*_state);if(!text){_error=text.error();return;}
    wireState=*text;_appliedWire=*text;
}
Result<void> StatsComponent::synchronize() {
    if(!_state || definitionsJson!=_appliedDefinition) {
        auto schema=definitionsJson.empty()?Schema::compile({}):decodeDefinitions(definitionsJson);
        if(!schema)return std::unexpected(schema.error());
        // A replica's received wire property must survive local author initialization.
        const auto incoming=wireState;auto result=configure(*schema);if(!result)return result;
        if(!_authority)wireState=incoming;
    }
    if(!_authority && wireState!=_appliedWire && !wireState.empty()) {
        auto state=decodeState(wireState,_state->schema());if(!state)return std::unexpected(state.error());
        if(state->version()<=_remoteVersion)return fail(Error::Stale,"replica update is older or duplicates a conflicting version");
        _remoteVersion=state->version();_state=std::move(*state);_appliedWire=wireState;
    }
    return {};
}
Result<void> StatsComponent::advance(std::uint64_t tick) {
    if(auto r=synchronize();!r)return r;
    if(!_authority){_deferredSettlements.clear();return {};}
    auto notifications=std::move(_deferredSettlements);_deferredSettlements.clear();
    for(const auto& notification:notifications)notify(notification);
    const auto previous=_state->version();auto result=_effects.advance(*_state,tick);_context.tick=tick;
    if(!result)return result;if(previous!=_state->version())publish();
    const auto commands=_pending.size();
    for(std::size_t i=0;i<commands;++i){auto command=std::move(_pending.front());_pending.pop_front();auto settled=commit(command,version());if(!settled)return std::unexpected(settled.error());}
    return {};
}
StatsSystem::StatsSystem():_token(++identities){}
void StatsSystem::onUpdate(float) {
    if(!_world)return;const auto entities=_world->getAllEntities();
    for(auto* entity:entities)if(auto* component=entity->getComponent<StatsComponent>())_tick=std::max(_tick,component->_effects.tick());
    if(_tick==UINT64_MAX)return;++_tick;
    for(auto* entity:entities)if(auto* component=entity->getComponent<StatsComponent>()){if(auto result=component->advance(_tick);!result)component->_error=result.error();}
}
Result<void> registerStatsSystem(ayt::entity::World& world) {
    if(!world.findSystemByName("StatsSystem"))world.registerSystem<StatsSystem>(120,ayt::entity::SystemLane::Sim);
    auto* system=dynamic_cast<StatsSystem*>(world.findSystemByName("StatsSystem"));if(!system)return fail(Error::Invalid,"StatsSystem name collision");system->bind(world);return {};
}
Result<void> registerStatsComponent(ayt::entity::ComponentRegistry& registry) {
    auto result=ayt::entity::registerSceneComponent<StatsComponent>(registry,"StatsComponent","Stats","Gameplay");
    if(!result)return fail(Error::Invalid,result.message());return {};
}
StatsIntegrationModule::StatsIntegrationModule():_descriptor{.id="AYStats.Integration",.displayName="Stats",.version="0.1.0",.dependencies={ayt::module::ModuleDependency::required(std::string(ayt::entity::kEntityRuntimeModuleId))}}{}
ayt::module::ModuleResult StatsIntegrationModule::registerTypes(ayt::module::IModuleContext& context) {
    auto* registry=context.findServiceAs<ayt::entity::ComponentRegistry>(ayt::entity::kComponentRegistryModuleService);
    if(!registry)return ayt::module::ModuleResult::failure(ayt::module::ModuleErrorCode::TypeRegistrationFailed,"Stats ComponentRegistry unavailable");
    auto result=registerStatsComponent(*registry);return result?ayt::module::ModuleResult::success():ayt::module::ModuleResult::failure(ayt::module::ModuleErrorCode::TypeRegistrationFailed,result.error().message);
}
ayt::module::ModuleResult StatsIntegrationModule::install(ayt::module::IModuleContext&) {
    auto result=registerStatsSystem(ayt::entity::World::instance());return result?ayt::module::ModuleResult::success():ayt::module::ModuleResult::failure(ayt::module::ModuleErrorCode::TypeRegistrationFailed,result.error().message);
}
Result<BatchSettlement> settleEntityBatch(ayt::entity::World& world,std::uint64_t token,std::span<const ayt::entity::EntityHandle> entities,std::span<const std::uint64_t> generations,std::span<const std::uint64_t> versions,std::span<const SettlementRequest> requests,BatchSemantics semantics,ayt::task::ITaskScheduler* scheduler) {
    auto* system=dynamic_cast<StatsSystem*>(world.findSystemByName("StatsSystem"));
    if(!system || system->worldToken()!=token || entities.size()!=generations.size() || entities.size()!=versions.size())return fail(Error::Stale,"World token or batch identity shape mismatch");
    std::vector<StatsComponent*> components;std::vector<StatSet*> bank;
    for(std::size_t i=0;i<entities.size();++i) {
        auto* entity=world.getEntityByHandle(entities[i]);if(!entity)return fail(Error::Stale,"Entity handle invalid");
        auto* component=entity->getComponent<StatsComponent>();if(!component || component->generation()!=generations[i] || !component->_authority || !component->_state)return fail(Error::Stale,"Stats component generation/authority changed");
        if(component->_notifying)return fail(Error::Reentrant,"batch during settlement notification");
        components.push_back(component);bank.push_back(&*component->_state);
    }
    auto result=settleBatch(bank,requests,semantics,versions,scheduler);if(!result)return result;
    for(auto* component:components)component->publish();
    // Entire bank is published before any callback; callbacks cannot write another bank participant.
    for(auto* component:components)component->_notifying=true;
    for(std::size_t r=0;r<requests.size();++r)for(std::size_t w=0;w<requests[r].writes.size();++w){
        auto* component=components[requests[r].writes[w].state];
        if(component->_onSettled)try{auto callback=component->_onSettled;callback(result->results[r][w]);}catch(...){component->_error=Diagnostic{Error::Invalid,"batch notification threw after commit",{}};}
    }
    for(auto* component:components)component->_notifying=false;return result;
}
Result<StatSet> importLegacyHealth(const ayt::entity::HealthComponent& legacy,std::string name) {
    if(legacy.maxHp<0 || legacy.currentHp<0 || legacy.currentHp>legacy.maxHp)return fail(Error::Invalid,"legacy health range invalid");
    auto schema=Schema::compile({Definition{.name=std::move(name),.shape=Shape::Pool,.current=legacy.currentHp,.capacity=legacy.maxHp,.maximum=INT32_MAX}});if(!schema)return std::unexpected(schema.error());return StatSet::create(*schema);
}
Result<void> exportLegacyHealth(const StatSet& state,StatId resource,ayt::entity::HealthComponent& legacy) {
    auto row=state.read(resource);if(!row)return std::unexpected(row.error());
    if(state.schema()->definitions()[*state.schema()->index(resource)].shape!=Shape::Pool || row->capacity>INT32_MAX || row->current>INT32_MAX)return fail(Error::Overflow,"legacy health conversion range");
    const auto old=legacy.currentHp;legacy.currentHp=std::int32_t(row->current);legacy.maxHp=std::int32_t(row->capacity);
    if(old!=legacy.currentHp && legacy.onHpChanged)legacy.onHpChanged(&legacy,old,legacy.currentHp);
    if(old>0 && legacy.currentHp==0 && legacy.onDeath)legacy.onDeath(&legacy);return {};
}
} // namespace ayt::stats
