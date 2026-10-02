#include <AYStats/EntityIntegration.h>
#include <AYAssetFormat/AssetFormat.h>
#include <AYEntity.h>
#include <AYEntity/ComponentRegistration.h>
#include <AYEntity/EntityRuntimeModule.h>
#if defined(AY_STATS_HAS_RESOURCES)
#include <AYStats/RecipeResource.h>
#include <AYResource/ResourceManager.h>
#include <AYResource/AssetPath.h>
#endif
#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>
#include <unordered_map>
namespace ayt::stats {
AY_FINALIZE_REGISTRATION_METADATA(StatsComponent)
AY_FINALIZE_REGISTRATION_METADATA(StatsRecipeComponent)
namespace {
std::atomic_uint64_t identities=0;
auto fail(Error e,std::string m){return std::unexpected(Diagnostic{e,std::move(m),{}});}
}
StatsComponent::StatsComponent():_generation(++identities){}
void StatsRecipeComponent::onDetach() {
    if(_owner)if(auto* world=_owner->getWorld())
        if(auto* system=dynamic_cast<StatsSystem*>(world->findSystemByName("StatsSystem")))
            system->releaseRecipeAsset(world->getEntityHandle(_owner->getId()));
    _owner=nullptr;
}
void StatsRecipeComponent::onStart() {if(auto result=refresh();!result)_error=result.error();}
Result<void> StatsRecipeComponent::refresh() {
    if(!_owner || !_owner->getWorld())return fail(Error::Missing,"Stats recipe owner has no World");
    auto& world=*_owner->getWorld();
    auto* system=dynamic_cast<StatsSystem*>(world.findSystemByName("StatsSystem"));
    if(!system)return fail(Error::Missing,"StatsSystem is unavailable");
    auto handle=world.getEntityHandle(_owner->getId());
    if(assetPath.empty()) {system->releaseRecipeAsset(handle);_error.reset();return {};}
    auto result=system->bindRecipeAsset(handle,assetPath);
    if(!result)_error=result.error();else _error.reset();
    return result;
}
void StatsComponent::onAttach(ayt::entity::Entity* owner){_owner=owner;_generation=++identities;}
void StatsComponent::onDetach(){publish(PublicationReason::Detach);_owner=nullptr;}
void StatsComponent::onStart(){if(auto r=synchronize();!r)_error=r.error();}
Result<void> StatsComponent::ensureReady(){auto result=synchronize();if(!result)_error=result.error();return result;}
Result<void> StatsComponent::configure(std::shared_ptr<const Schema> schema) {
    if(_notifying)return fail(Error::Reentrant,"configure during settlement notification");
    if(!schema)return fail(Error::Invalid,"null Stats schema");
    auto encoded=encodeDefinitions(*schema);if(!encoded)return std::unexpected(encoded.error());
    auto state=StatSet::create(std::move(schema));if(!state)return std::unexpected(state.error());
    if(auto ready=preparePublication(*state->schema(),PublicationReason::Configure);!ready)return ready;
    _state=std::move(*state);_effects={};_context={};_pending.clear();_deferredSettlements.clear();_generation=++identities;_remoteVersion=0;
    definitionsJson=*encoded;_appliedDefinition=*encoded;_error.reset();publish(PublicationReason::Configure);return {};
}
Result<Row> StatsComponent::read(StatId id) const {if(!_state)return fail(Error::Invalid,"Stats not configured");return _state->read(id);}
Result<Settlement> StatsComponent::commit(std::span<const Mutation> operations,std::uint64_t expected,NotificationDelivery delivery) {
    if(_notifying)return fail(Error::Reentrant,"enqueue commands instead of reentering settlement");
    if(!_authority)return fail(Error::Invalid,"replica cannot commit authoritative state");
    if(!_state)return fail(Error::Invalid,"Stats not configured");
    if(delivery!=NotificationDelivery::Immediate && delivery!=NotificationDelivery::NextTick)return fail(Error::Invalid,"invalid notification delivery");
    if(operations.size()>65536)return fail(Error::Limit,"transaction operation limit");
    if(auto ready=preparePublication(*_state->schema(),PublicationReason::Commit);!ready)return std::unexpected(ready.error());
    Settlement notification;
    if(delivery==NotificationDelivery::NextTick) {
        if(_deferredSettlements.size()>=1024)return fail(Error::Limit,"deferred settlement notification limit");
        _deferredSettlements.reserve(1024);notification.changes.reserve(operations.size()*2);
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
    if(auto ready=preparePublication(*_state->schema(),PublicationReason::Effects);!ready)return ready;
    auto result=_effects.apply(*_state,std::move(effect),policy,tick);if(result)publish(PublicationReason::Effects);return result;
}
Result<void> StatsComponent::removeEffect(std::uint64_t instance) {
    if(_notifying)return fail(Error::Reentrant,"effect removal during notification");
    if(!_authority || !_state)return fail(Error::Invalid,"effect removal requires configured authority");
    if(auto ready=preparePublication(*_state->schema(),PublicationReason::Effects);!ready)return ready;
    auto result=_effects.remove(*_state,instance);if(result)publish(PublicationReason::Effects);return result;
}
Result<std::string> StatsComponent::checkpoint() const {
    if(!_state)return fail(Error::Invalid,"Stats not configured");return encodeCheckpoint({*_state,_effects,_context});
}
Result<void> StatsComponent::updateEffect(Effect effect,std::uint64_t tick) {
    if(_notifying)return fail(Error::Reentrant,"effect update during notification");
    if(!_authority || !_state)return fail(Error::Invalid,"effect update requires configured authority");
    if(auto ready=preparePublication(*_state->schema(),PublicationReason::Effects);!ready)return ready;
    auto result=_effects.update(*_state,std::move(effect),tick);if(result)publish(PublicationReason::Effects);return result;
}
Result<void> StatsComponent::restoreCheckpoint(std::string_view text) {
    if(_notifying)return fail(Error::Reentrant,"restore during notification");
    if(!_state || !_authority)return fail(Error::Invalid,"restore requires configured authority");
    auto restored=decodeCheckpoint(text,_state->schema());if(!restored)return std::unexpected(restored.error());
    if(auto ready=preparePublication(*restored->state.schema(),PublicationReason::Restore);!ready)return ready;
    _state=std::move(restored->state);_effects=std::move(restored->effects);_context=restored->context;_pending.clear();_deferredSettlements.clear();_generation=++identities;publish(PublicationReason::Restore);return {};
}
Result<void> StatsComponent::preparePublication(const Schema& schema,PublicationReason reason,std::size_t publications) {
    if(!_owner)return {};
    auto* world=_owner->getWorld();
    auto* system=world?dynamic_cast<StatsSystem*>(world->findSystemByName("StatsSystem")):nullptr;
    return system?system->preparePublication(world->getEntityHandle(_owner->getId()),*this,schema,reason,publications):Result<void>{};
}
void StatsComponent::publish(PublicationReason reason) {
    // Mandatory internal propagation must still run if transport encoding fails.
    if(_owner)if(auto* world=_owner->getWorld())if(auto* system=dynamic_cast<StatsSystem*>(world->findSystemByName("StatsSystem")))
        system->publish(world->getEntityHandle(_owner->getId()),*this,reason);
    if(reason==PublicationReason::Detach)return;
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
        if(auto ready=preparePublication(*state->schema(),PublicationReason::Replica);!ready)return ready;
        _remoteVersion=state->version();_state=std::move(*state);_appliedWire=wireState;
        if(_owner)if(auto* world=_owner->getWorld())if(auto* system=dynamic_cast<StatsSystem*>(world->findSystemByName("StatsSystem")))
            system->publish(world->getEntityHandle(_owner->getId()),*this,PublicationReason::Replica);
    }
    return {};
}
Result<void> StatsComponent::advance(std::uint64_t tick) {
    if(auto r=synchronize();!r)return r;
    if(!_authority){_deferredSettlements.clear();return {};}
    auto notifications=std::move(_deferredSettlements);_deferredSettlements.clear();
    for(const auto& notification:notifications)notify(notification);
    if(auto ready=preparePublication(*_state->schema(),PublicationReason::Effects);!ready)return ready;
    const auto previous=_state->version();auto result=_effects.advance(*_state,tick);_context.tick=tick;
    if(!result)return result;if(previous!=_state->version())publish(PublicationReason::Effects);
    const auto commands=_pending.size();
    for(std::size_t i=0;i<commands;++i){auto command=std::move(_pending.front());_pending.pop_front();auto settled=commit(command,version());if(!settled)return std::unexpected(settled.error());}
    return {};
}
StatsSystem::StatsSystem():_token(++identities),_direct(std::make_shared<const CalculationPlan>(CalculationPlan::compile({{.id=1,.op=Op::Input,.binding=0}},1,1).value())) {
    _observers.reserve(16);
}
Result<void> StatsSystem::observeState(const std::shared_ptr<StateObserver>& observer) {
    if(_publishing)return fail(Error::Reentrant,"observer registration during publication");
    if(!observer)return fail(Error::Invalid,"null state observer");
    std::erase_if(_observers,[](const auto& weak){return weak.expired();});
    for(const auto& weak:_observers)if(weak.lock()==observer)return {};
    if(_observers.size()>=16)return fail(Error::Limit,"internal state observer limit");
    _observers.push_back(observer);return {};
}
Result<void> StatsSystem::preparePublication(ayt::entity::EntityHandle entity,const StatsComponent& component,const Schema& schema,PublicationReason reason,std::size_t publications) {
    if(_publishing)return fail(Error::Reentrant,"mutation during internal publication");
    _publishing=true;struct Guard{bool& flag;~Guard(){flag=false;}} guard{_publishing};
    for(const auto& weak:_observers)if(auto observer=weak.lock()) {
        try {if(auto result=observer->prepare(entity,component,schema,reason,publications);!result)return result;}
        catch(...){return fail(Error::Invalid,"internal state preflight threw");}
    }
    return {};
}
void StatsSystem::publish(ayt::entity::EntityHandle entity,const StatsComponent& component,PublicationReason reason) noexcept {
    if(_publishing)std::terminate();
    _publishing=true;
    for(const auto& weak:_observers)if(auto observer=weak.lock())observer->published(entity,component,reason);
    _publishing=false;
}
Result<void> StatsSystem::setRecipes(std::shared_ptr<const RecipeBook> recipes) {
    if(_publishing || _changing)return fail(Error::Reentrant,"recipe replacement during calculation/publication");
    if(_recipeOwner.isValid())return fail(Error::Invalid,"World recipe asset owner controls the active book");
    _recipes=std::move(recipes);return {};
}
Result<void> StatsSystem::loadRecipes(const std::filesystem::path& path,const Schema& targetSchema,
                                     const Schema* sourceSchema,Extensions extensions) {
    if(_publishing || _changing)return fail(Error::Reentrant,"recipe replacement during calculation/publication");
    auto loaded=loadRecipeBook(path,targetSchema,sourceSchema,std::move(extensions));
    if(!loaded)return std::unexpected(loaded.error());
    return setRecipes(std::move(*loaded));
}
Result<void> StatsSystem::bindRecipeAsset(ayt::entity::EntityHandle owner,std::string_view path) {
    if(_publishing || _changing)return fail(Error::Reentrant,"recipe replacement during calculation/publication");
    if(!owner.isValid())return fail(Error::Invalid,"invalid recipe owner");
    if(_recipeOwner.isValid() && _recipeOwner!=owner)return fail(Error::Invalid,"World already has a recipe asset owner");
    const std::filesystem::path logical{std::string(path)};
    if(path.empty() || !ayt::asset_format::matchesPath(path, ayt::asset_format::Id::StatsRecipes) || logical.has_root_path() ||
       logical.has_root_name() || path.find(':')!=std::string_view::npos ||
       std::ranges::any_of(logical,[](const auto& part){return part=="..";}))
        return fail(Error::Invalid,"recipe asset path must be Assets-relative .sts");
#if defined(AY_STATS_HAS_RESOURCES)
    registerRecipeResourceLoader();
    const auto resolved=resource::resolveAssetPath("",std::string(path));
    auto& manager=resource::ResourceManager::instance();
    auto loaded=manager.load<RecipeResource>(resolved);
    if(!loaded || manager.hasLoadFailed(resolved) || !loaded->asset())
        return fail(Error::Missing,"Stats recipe asset could not be loaded: "+std::string(path));
    auto snapshot=loaded->asset();
    if(_recipeOwner==owner && _recipeAssetPath==path && _recipeSnapshot.lock()==snapshot)return {};
    _recipes=snapshot->book;
    _recipeOwner=owner;_recipeAssetPath=path;_recipeSnapshot=snapshot;return {};
#else
    (void)path;return fail(Error::Missing,"AYResource integration is unavailable");
#endif
}
void StatsSystem::releaseRecipeAsset(ayt::entity::EntityHandle owner) {
    if(_recipeOwner!=owner || _publishing || _changing)return;
    _recipeOwner={};_recipeAssetPath.clear();_recipeSnapshot.reset();_recipes.reset();
}
Result<StatsSystem::PreparedCalculation> StatsSystem::prepareCalculation(ayt::entity::EntityHandle target,StatId stat,
    AttributeOp operation,Value amount,std::string_view recipe,std::optional<ayt::entity::EntityHandle> source) {
    if(_publishing)return fail(Error::Reentrant,"calculation during internal publication");
    if(operation<AttributeOp::Increase || operation>AttributeOp::Set || (operation!=AttributeOp::Set && amount<0))return fail(Error::Invalid,"invalid operation/amount");
    auto* entity=_world?_world->getEntityByHandle(target):nullptr;
    auto* component=entity?entity->getComponent<StatsComponent>():nullptr;
    if(!component || !component->state())return fail(Error::Stale,"target Stats is unavailable");
    if(!component->authority())return fail(Error::Invalid,"replica cannot calculate an authoritative action");
    auto index=component->state()->schema()->index(stat);
    if(!index || component->state()->schema()->definitions()[*index].lifetime!=Lifetime::Persistent)return fail(Error::Missing,"persistent target attribute missing");
    PreparedCalculation prepared;prepared.owner=_recipes;
    const Recipe* selected=nullptr;
    if(_recipes){auto chosen=_recipes->select(stat,operation,recipe);if(!chosen)return std::unexpected(chosen.error());selected=*chosen;}
    else if(!recipe.empty())return fail(Error::Missing,"no recipes configured");
    const StatSet* sourceState=nullptr;
    if(source){auto* sourceEntity=_world->getEntityByHandle(*source);auto* sourceStats=sourceEntity?sourceEntity->getComponent<StatsComponent>():nullptr;
        if(!sourceStats || !sourceStats->state())return fail(Error::Stale,"source Stats is unavailable");
        if(!sourceStats->authority())return fail(Error::Invalid,"replica cannot supply authoritative action inputs");
        sourceState=sourceStats->state();}
    if(selected){auto inputs=sampleRecipe(*selected,amount,*component->state(),sourceState);if(!inputs)return std::unexpected(inputs.error());
        prepared.retainedPlan=std::shared_ptr<const CalculationPlan>(prepared.owner,&selected->plan);prepared.inputs=std::move(*inputs);}
    else{prepared.retainedPlan=_direct;prepared.inputs={amount};}
    prepared.plan=prepared.retainedPlan.get();
    if(_calculations==UINT64_MAX)return fail(Error::Limit,"calculation identity exhausted");
    const auto identity=++_calculations;prepared.context={.seed=_token,.tick=_tick,.calculation=identity,.hit=identity};
    return prepared;
}
Result<Settlement> StatsSystem::change(ayt::entity::EntityHandle target,std::string_view stat,AttributeOp operation,
    Value amount,std::string_view recipe,std::optional<ayt::entity::EntityHandle> source) {
    if(_changing || _publishing)return fail(Error::Reentrant,"attribute action is already running");
    if(stat.empty())return fail(Error::Invalid,"empty attribute name");
    _changing=true;struct Guard{bool& flag;~Guard(){flag=false;}} guard{_changing};
    auto prepared=prepareCalculation(target,statId(stat),operation,amount,recipe,source);if(!prepared)return std::unexpected(prepared.error());
    auto* component=_world->getEntityByHandle(target)->getComponent<StatsComponent>();
    auto index=component->state()->schema()->index(statId(stat));
    if(!index || component->state()->schema()->definitions()[*index].name!=stat)return fail(Error::Missing,"target attribute name missing");
    const auto generation=component->generation(),version=component->version();
    std::vector<Value> scratch(prepared->plan->instructions().size());
    auto value=prepared->plan->evaluate(prepared->inputs,prepared->context,scratch);if(!value)return std::unexpected(value.error());
    auto* currentEntity=_world->getEntityByHandle(target);
    component=currentEntity?currentEntity->getComponent<StatsComponent>():nullptr;
    if(!component || component->generation()!=generation || component->version()!=version)return fail(Error::Stale,"attribute changed during calculation");
    auto mutation=attributeMutation(*component->state(),statId(stat),operation,*value);if(!mutation)return std::unexpected(mutation.error());
    return component->commit(std::span(&*mutation,1),version);
}
Result<BatchSettlement> StatsSystem::changeBatch(std::span<const AttributeAction> actions,BatchSemantics semantics,ayt::task::ITaskScheduler* scheduler) {
    if(_changing || _publishing)return fail(Error::Reentrant,"attribute action is already running");
    if(!_world)return fail(Error::Missing,"StatsSystem is not bound");
    if(actions.size()>1000000)return fail(Error::Limit,"attribute action count limit");
    if(semantics!=BatchSemantics::Snapshot && semantics!=BatchSemantics::Ordered)return fail(Error::Invalid,"unknown batch semantics");
    _changing=true;struct Guard{bool& flag;~Guard(){flag=false;}} guard{_changing};
    std::vector<ayt::entity::EntityHandle> handles;
    std::vector<std::uint64_t> generations,versions;
    std::unordered_map<std::uint64_t,std::size_t> indices;
    const auto capture=[&](ayt::entity::EntityHandle handle)->Result<std::size_t> {
        auto* entity=_world->getEntityByHandle(handle);
        auto* stats=entity?entity->getComponent<StatsComponent>():nullptr;
        if(!stats || !stats->state())return fail(Error::Stale,"batch Stats is unavailable");
        if(!stats->authority())return fail(Error::Invalid,"replica cannot submit authoritative actions");
        if(stats->_notifying)return fail(Error::Reentrant,"batch during settlement notification");
        if(auto found=indices.find(handle.id);found!=indices.end())return found->second;
        if(handles.size()>=65536)return fail(Error::Limit,"attribute bank count limit");
        const auto index=handles.size();indices.emplace(handle.id,index);handles.push_back(handle);
        generations.push_back(stats->generation());versions.push_back(stats->version());return index;
    };
    std::vector<SettlementRequest> requests;requests.reserve(actions.size());
    if(actions.size()>UINT64_MAX-_calculations)return fail(Error::Limit,"calculation identity exhausted");
    for(const auto& action:actions) {
        if(action.stat.empty())return fail(Error::Invalid,"empty attribute name");
        auto target=capture(action.target);if(!target)return std::unexpected(target.error());
        auto* state=_world->getEntityByHandle(action.target)->getComponent<StatsComponent>()->state();
        auto index=state->schema()->index(statId(action.stat));
        if(!index || state->schema()->definitions()[*index].name!=action.stat)return fail(Error::Missing,"target attribute missing");
        std::optional<std::size_t> source;
        if(action.source){auto r=capture(*action.source);if(!r)return std::unexpected(r.error());source=*r;}
        const auto identity=++_calculations;
        auto request=prepareRecipeRequest(_recipes,{action.id,*target,statId(action.stat),action.operation,action.amount,action.recipe,source,
            {.seed=_token,.tick=_tick,.calculation=identity,.hit=identity}});
        if(!request)return std::unexpected(request.error());requests.push_back(std::move(*request));
    }
    return settleEntityBatch(*_world,_token,handles,generations,versions,requests,semantics,scheduler);
}
void StatsSystem::onUpdate(float) {
    if(!_world)return;const auto entities=_world->getAllEntities();
    for(auto* entity:entities)if(auto* component=entity->getComponent<StatsRecipeComponent>())(void)component->refresh();
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
    if(!result)return fail(Error::Invalid,result.message());
    auto recipes=ayt::entity::registerSceneComponent<StatsRecipeComponent>(registry,"StatsRecipeComponent","Stats Recipes","Gameplay");
    if(!recipes)return fail(Error::Invalid,recipes.message());return {};
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
Result<BatchSettlement> settleEntityBatch(ayt::entity::World& world,std::uint64_t token,std::span<const ayt::entity::EntityHandle> entities,std::span<const std::uint64_t> generations,std::span<const std::uint64_t> versions,std::span<const SettlementRequest> requests,BatchSemantics semantics,ayt::task::ITaskScheduler* scheduler,SettlementMetrics* metrics) {
    using Clock=std::chrono::steady_clock;
    if(metrics)*metrics={};
    const auto stamp=[&]{return metrics?Clock::now():Clock::time_point{};};
    const auto elapsed=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
    const auto start=stamp();
    auto* system=dynamic_cast<StatsSystem*>(world.findSystemByName("StatsSystem"));
    if(!system || system->worldToken()!=token || entities.size()!=generations.size() || entities.size()!=versions.size())return fail(Error::Stale,"World token or batch identity shape mismatch");
    std::vector<StatsComponent*> components;std::vector<StatSet*> bank;
    components.reserve(entities.size());bank.reserve(entities.size());
    for(std::size_t i=0;i<entities.size();++i) {
        auto* entity=world.getEntityByHandle(entities[i]);if(!entity)return fail(Error::Stale,"Entity handle invalid");
        auto* component=entity->getComponent<StatsComponent>();if(!component || component->generation()!=generations[i] || !component->_authority || !component->_state)return fail(Error::Stale,"Stats component generation/authority changed");
        if(component->_notifying)return fail(Error::Reentrant,"batch during settlement notification");
        if(auto ready=component->preparePublication(*component->_state->schema(),PublicationReason::Commit,entities.size());!ready)return std::unexpected(ready.error());
        components.push_back(component);bank.push_back(&*component->_state);
    }
    const auto prepared=stamp();
    auto result=settleBatch(bank,requests,semantics,versions,scheduler,{},metrics);
    if(metrics)metrics->entityPreparationMs=elapsed(start,prepared);
    if(!result)return result;
    const auto settled=stamp();
    for(auto* component:components)component->publish();
    const auto published=stamp();if(metrics)metrics->entityPublicationMs=elapsed(settled,published);
    // Entire bank is published before any callback; callbacks cannot write another bank participant.
    for(auto* component:components)component->_notifying=true;
    for(std::size_t r=0;r<requests.size();++r)for(std::size_t w=0;w<requests[r].writes.size();++w){
        auto* component=components[requests[r].writes[w].state];
        if(component->_onSettled)try{auto callback=component->_onSettled;if(metrics)++metrics->notifiedWrites;callback(result->results[r][w]);}catch(...){component->_error=Diagnostic{Error::Invalid,"batch notification threw after commit",{}};}
    }
    for(auto* component:components)component->_notifying=false;
    if(metrics)metrics->notificationMs=elapsed(published,stamp());return result;
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
