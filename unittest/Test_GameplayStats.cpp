#include <AYTest.h>
#include <AYStats/EntityIntegration.h>
#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include "../demo/GameplayScenario.h"
#include <array>
#include <stdexcept>
using namespace ayt::stats;
namespace {
struct GameplayFixture {
    ayt::entity::World& world=ayt::entity::World::processWorld();
    StatsSystem* system=nullptr;
    GameplayFixture() {
        ayt::entity::World::setActiveWorld(nullptr);world.initialize();
        auto& registry=ayt::entity::ComponentRegistry::instance();
        if(!registry.isSealed()) {
            (void)ayt::entity::registerEntityCoreComponents(registry);
            demo::require(registerStatsComponent(registry));registry.seal();
        }
        demo::require(registerStatsSystem(world));system=dynamic_cast<StatsSystem*>(world.findSystemByName("StatsSystem"));
    }
    ~GameplayFixture(){world.shutdown();}
    std::pair<ayt::entity::EntityHandle,StatsComponent*> make(Value health=100) {
        auto* entity=world.createEntity();auto* stats=entity->addComponent<StatsComponent>();
        demo::require(stats->configure(demo::require(Schema::compile({
            {.name="health",.shape=Shape::Pool,.current=health,.capacity=100},
            {.name="power",.mode=Mode::Advanced,.initial=10,.maximum=1000,.zones={{1,ZoneOp::PercentSum}}}
        }))));
        return {world.getEntityHandle(entity->getId()),stats};
    }
};
CalculationPlan inputPlan(){return demo::require(CalculationPlan::compile({{.id=1,.op=Op::Input}},1,1));}
Recipe halfHealth(){return {"half",statId("health"),AttributeOp::Decrease,
    demo::require(CalculationPlan::compile({{.id=1,.op=Op::Input},{.id=2,.op=Op::Constant,.value=2},
        {.id=3,.op=Op::Divide,.a=1,.b=2}},3,1)),{{InputSource::TargetStat,statId("health"),InputField::Current}},true};}
struct Observer final:StateObserver {
    int preparations=0,publications=0;
    bool reject=false,throwPrepare=false;
    std::size_t batchPublications=0;
    std::vector<PublicationReason> reasons;
    StatsComponent* attempt=nullptr;
    Error nestedError=Error::Invalid;
    Result<void> prepare(ayt::entity::EntityHandle,const StatsComponent&,const Schema&,PublicationReason,std::size_t count) override {
        ++preparations;batchPublications=count;
        if(throwPrepare)throw std::runtime_error("preflight test");
        if(reject)return std::unexpected(Diagnostic{Error::Limit,"observer capacity",{}});
        return {};
    }
    void published(ayt::entity::EntityHandle,const StatsComponent&,PublicationReason reason) noexcept override {
        ++publications;
        // This capacity is reserved before registration, per the allocation-free observer contract.
        reasons.push_back(reason);
        if(attempt){auto r=attempt->commit(std::array{Mutation{statId("health"),MutationOp::Decrease,1}},attempt->version());
            if(!r)nestedError=r.error().code;}
    }
    Observer(){reasons.reserve(64);}
};
}
TEST_SUITE(StatsGameplayTests)
TEST_CASE(entity_batch_profiling_preserves_observer_and_callback_publication_order) {
    GameplayFixture f;auto [a,first]=f.make();auto [b,second]=f.make();
    const auto observer=std::make_shared<Observer>();demo::require(f.system->observeState(observer));
    std::size_t callbacks=0;
    first->setOnSettled([&](const Settlement& settlement){++callbacks;CHECK_TRUE(second->read(statId("health"))->current==80);CHECK_TRUE(settlement.changes[0].actual==-20);});
    second->setOnSettled([&](const Settlement&){++callbacks;CHECK_TRUE(first->read(statId("health"))->current==80);});
    const std::array handles{a,b};const std::array generations{first->generation(),second->generation()},versions{first->version(),second->version()};
    const auto plan=std::make_shared<const CalculationPlan>(inputPlan());
    const std::array requests{
        SettlementRequest{.id=1,.plan=plan,.inputs={{.literal=20}},.writes={{0,statId("health"),MutationOp::Decrease}}},
        SettlementRequest{.id=2,.plan=plan,.inputs={{.literal=20}},.writes={{1,statId("health"),MutationOp::Decrease}}}};
    SettlementMetrics metrics;metrics.notifiedWrites=42;
    const auto result=settleEntityBatch(f.world,f.system->worldToken(),handles,generations,versions,requests,BatchSemantics::Ordered,nullptr,&metrics);
    CHECK_TRUE(result.has_value());CHECK_TRUE(callbacks==2 && observer->publications==2);
    CHECK_TRUE(metrics.notifiedWrites==2 && metrics.copiedStates==2 && metrics.committedWrites==2);
    const auto stale=settleEntityBatch(f.world,f.system->worldToken(),handles,generations,versions,requests,BatchSemantics::Ordered,nullptr,&metrics);
    CHECK_TRUE(!stale && stale.error().code==Error::Stale);CHECK_TRUE(metrics.notifiedWrites==0 && callbacks==2);
}
TEST_CASE(direct_operations_distinguish_pool_current_and_advanced_scalar_base) {
    GameplayFixture f;auto [handle,stats]=f.make(40);
    demo::require(stats->applyEffect({.id=1,.key=1,.expires=10,.modifiers={
        {.handle=1,.rule=1,.target=statId("power"),.zone=1,.value=5000}}},EffectPolicy::Independent,0));
    auto changed=f.system->increase(handle,"power",10);CHECK_TRUE(changed.has_value());
    CHECK_TRUE(changed->changes[0].field==ChangeField::Base);CHECK_TRUE(changed->changes[0].actual==10);
    CHECK_TRUE(stats->read(statId("power"))->base==20);CHECK_TRUE(stats->read(statId("power"))->effective==30);
    auto healed=f.system->increase(handle,"health",80);CHECK_TRUE(healed->changes[0].actual==60);CHECK_TRUE(healed->changes[0].excess==20);
    CHECK_TRUE(f.system->set(handle,"health",0).has_value());CHECK_TRUE(stats->read(statId("health"))->current==0);
    const auto version=stats->version();CHECK_FALSE(f.system->set(handle,"health",101).has_value());
    CHECK_FALSE(f.system->decrease(handle,"health",-1).has_value());CHECK_FALSE(f.system->set(handle,"unknown",1).has_value());
    CHECK_TRUE(stats->version()==version);
}
TEST_CASE(named_recipes_sample_explicit_source_and_retain_their_plan) {
    GameplayFixture f;auto [source,actor]=f.make();auto [target,victim]=f.make();
    demo::require(actor->applyEffect({.id=1,.key=1,.expires=10,.modifiers={
        {.handle=1,.rule=1,.target=statId("power"),.zone=1,.value=5000}}},EffectPolicy::Independent,0));
    Recipe r{"source-power",statId("health"),AttributeOp::Decrease,inputPlan(),{{InputSource::SourceStat,statId("power")}},true};
    demo::require(f.system->setRecipes(demo::require(RecipeBook::compile({r}))));
    CHECK_TRUE(f.system->decrease(target,"health",0,"",source)->changes[0].actual==-15);
    const auto version=victim->version();
    CHECK_TRUE(f.system->decrease(target,"health",1,"source-power").error().code==Error::Missing);
    CHECK_TRUE(f.system->increase(target,"health",1,"source-power",source).error().code==Error::Invalid);
    CHECK_TRUE(f.system->decrease(target,"health",1,"missing",source).error().code==Error::Missing);
    CHECK_TRUE(victim->version()==version);
    auto prepared=demo::require(f.system->prepareCalculation(target,statId("health"),AttributeOp::Decrease,0,"source-power",source));
    demo::require(f.system->setRecipes(nullptr));f.world.shutdown();
    std::vector<Value> scratch(prepared.plan->instructions().size());
    CHECK_TRUE(prepared.plan->evaluate(prepared.inputs,prepared.context,scratch).value()==15);
}
TEST_CASE(direct_prepared_calculation_survives_world_teardown) {
    GameplayFixture f;auto [handle,stats]=f.make();
    auto prepared=demo::require(f.system->prepareCalculation(handle,statId("health"),AttributeOp::Decrease,7));
    f.world.shutdown();std::vector<Value> scratch(prepared.plan->instructions().size());
    CHECK_TRUE(prepared.plan->evaluate(prepared.inputs,prepared.context,scratch).value()==7);
}
TEST_CASE(ordered_recipe_actions_match_single_calls_and_accumulate_scalar_writes) {
    GameplayFixture f;auto [a,actor]=f.make();auto [b,victim]=f.make();auto [c,batchActor]=f.make();auto [d,batchVictim]=f.make();
    auto product=demo::require(CalculationPlan::compile({{.id=1,.op=Op::Input},{.id=2,.op=Op::Input,.binding=1},
        {.id=3,.op=Op::Multiply,.a=1,.b=2}},3,2));
    demo::require(f.system->setRecipes(demo::require(RecipeBook::compile({
        {"strike",statId("health"),AttributeOp::Decrease,product,{{InputSource::SourceStat,statId("power")},{InputSource::Amount}}}}))));
    const auto first=demo::require(f.system->increase(a,"power",5));
    const auto second=demo::require(f.system->increase(a,"power",3));
    const auto third=demo::require(f.system->decrease(b,"health",2,"strike",a));
    const std::array actions{
        StatsSystem::AttributeAction{1,c,"power",AttributeOp::Increase,5},
        StatsSystem::AttributeAction{2,c,"power",AttributeOp::Increase,3},
        StatsSystem::AttributeAction{3,d,"health",AttributeOp::Decrease,2,"strike",c}};
    const auto batch=demo::require(f.system->changeBatch(actions));
    CHECK_TRUE(batch.results[0][0].changes[0].actual==first.changes[0].actual);
    CHECK_TRUE(batch.results[1][0].changes[0].after==second.changes[0].after);
    CHECK_TRUE(batch.results[2][0].changes[0].actual==third.changes[0].actual);
    CHECK_TRUE(batchActor->read(statId("power"))->base==18);CHECK_TRUE(batchVictim->read(statId("health"))->current==64);
    CHECK_TRUE(actor->read(statId("power"))->effective==batchActor->read(statId("power"))->effective);
    CHECK_TRUE(victim->read(statId("health"))->current==batchVictim->read(statId("health"))->current);
}
TEST_CASE(snapshot_is_explicit_and_actual_attribution_remains_in_request_order) {
    GameplayFixture f;auto [a,ordered]=f.make();auto [b,snapshot]=f.make();
    demo::require(f.system->setRecipes(demo::require(RecipeBook::compile({halfHealth()}))));
    std::array actions{StatsSystem::AttributeAction{1,a,"health",AttributeOp::Decrease,0},
        StatsSystem::AttributeAction{2,a,"health",AttributeOp::Decrease,0}};
    auto o=demo::require(f.system->changeBatch(actions));CHECK_TRUE(o.values[0]==50 && o.values[1]==25);
    actions[0].target=b;actions[1].target=b;
    auto s=demo::require(f.system->changeBatch(actions,BatchSemantics::Snapshot));CHECK_TRUE(s.values[0]==50 && s.values[1]==50);
    CHECK_TRUE(ordered->read(statId("health"))->current==25);CHECK_TRUE(snapshot->read(statId("health"))->current==0);
    demo::require(f.system->setRecipes(nullptr));demo::require(f.system->set(b,"health",100));
    actions[0].amount=80;actions[1].amount=80;
    s=demo::require(f.system->changeBatch(actions));
    CHECK_TRUE(s.results[0][0].changes[0].actual==-80);CHECK_TRUE(s.results[1][0].changes[0].actual==-20);
    CHECK_TRUE(s.results[1][0].changes[0].excess==60);
}
TEST_CASE(batch_failure_duplicate_and_late_replay_do_not_publish) {
    GameplayFixture f;auto [a,actor]=f.make();auto [b,victim]=f.make();int notifications=0;
    actor->setOnSettled([&](const auto&){++notifications;});victim->setOnSettled([&](const auto&){++notifications;});
    const auto av=actor->version(),bv=victim->version();
    auto negative=demo::require(CalculationPlan::compile({{.id=1,.op=Op::Constant,.value=-1}},1,0));
    demo::require(f.system->setRecipes(demo::require(RecipeBook::compile({
        {"negative",statId("health"),AttributeOp::Increase,negative,{}}}))));
    std::array actions{StatsSystem::AttributeAction{1,a,"health",AttributeOp::Decrease,10},
        StatsSystem::AttributeAction{2,b,"health",AttributeOp::Increase,1,"negative"}};
    CHECK_TRUE(f.system->changeBatch(actions).error().code==Error::Invalid);
    CHECK_TRUE(actor->version()==av && victim->version()==bv);CHECK_TRUE(notifications==0);
    actions[1]={1,b,"health",AttributeOp::Decrease,10};CHECK_TRUE(f.system->changeBatch(actions).error().code==Error::Duplicate);
    CHECK_TRUE(actor->version()==av && victim->version()==bv);CHECK_TRUE(notifications==0);
    const std::array handles{a};const std::array generations{actor->generation()},versions{av};
    const std::array request{demo::require(prepareRecipeRequest(nullptr,{1,0,statId("health"),AttributeOp::Decrease,10}))};
    CHECK_TRUE(settleEntityBatch(f.world,f.system->worldToken(),handles,generations,versions,request,BatchSemantics::Ordered).has_value());
    CHECK_TRUE(settleEntityBatch(f.world,f.system->worldToken(),handles,generations,versions,request,BatchSemantics::Ordered).error().code==Error::Stale);
    CHECK_TRUE(actor->read(statId("health"))->current==90);CHECK_TRUE(notifications==1);
}
TEST_CASE(batch_callbacks_see_published_bank_and_reject_cross_participant_reentry) {
    GameplayFixture f;auto [a,actor]=f.make();auto [b,victim]=f.make();int notifications=0;
    actor->setOnSettled([&](const auto&){++notifications;CHECK_TRUE(victim->read(statId("health"))->current==70);
        CHECK_TRUE(f.system->decrease(b,"health",1).error().code==Error::Reentrant);
        CHECK_TRUE(victim->commit(std::array{Mutation{statId("health"),MutationOp::Decrease,1}},victim->version()).error().code==Error::Reentrant);});
    const std::array actions{StatsSystem::AttributeAction{1,a,"health",AttributeOp::Decrease,20},
        StatsSystem::AttributeAction{2,b,"health",AttributeOp::Decrease,30}};
    CHECK_TRUE(f.system->changeBatch(actions).has_value());CHECK_TRUE(notifications==1);
    CHECK_TRUE(actor->read(statId("health"))->current==80 && victim->read(statId("health"))->current==70);
}
TEST_CASE(internal_observer_is_independent_weak_and_preflight_failure_is_atomic) {
    GameplayFixture f;auto [h,stats]=f.make();auto observer=std::make_shared<Observer>();
    demo::require(f.system->observeState(observer));demo::require(f.system->observeState(observer));
    int gameplay=0;stats->setOnSettled([&](const auto&){++gameplay;});
    observer->attempt=stats;CHECK_TRUE(f.system->decrease(h,"health",10).has_value());
    CHECK_TRUE(observer->publications==1 && observer->nestedError==Error::Reentrant);CHECK_TRUE(gameplay==1);
    stats->setOnSettled({});observer->attempt=nullptr;
    auto saved=demo::require(stats->checkpoint());const auto version=stats->version(),generation=stats->generation();const auto wire=stats->wireState;
    observer->reject=true;CHECK_TRUE(stats->restoreCheckpoint(saved).error().code==Error::Limit);
    CHECK_TRUE(stats->version()==version && stats->generation()==generation && stats->wireState==wire);
    observer->reject=false;observer->throwPrepare=true;CHECK_TRUE(f.system->decrease(h,"health",5).error().code==Error::Invalid);
    CHECK_TRUE(stats->version()==version && stats->read(statId("health"))->current==90);
    observer->throwPrepare=false;demo::require(stats->restoreCheckpoint(saved));
    CHECK_TRUE(observer->reasons.back()==PublicationReason::Restore);
    observer.reset();CHECK_TRUE(f.system->increase(h,"health",5).has_value());CHECK_TRUE(gameplay==1);
}
TEST_CASE(effect_publications_include_removal_update_expiry_and_detach) {
    GameplayFixture f;auto [h,stats]=f.make();auto observer=std::make_shared<Observer>();demo::require(f.system->observeState(observer));
    Effect effect{.id=1,.key=1,.expires=3,.modifiers={{.handle=1,.rule=1,.target=statId("power"),.zone=1,.value=5000}}};
    demo::require(stats->applyEffect(effect,EffectPolicy::Independent,0));
    effect.modifiers[0].value=10000;demo::require(stats->updateEffect(effect,0));CHECK_TRUE(stats->read(statId("power"))->effective==20);
    demo::require(stats->removeEffect(1));CHECK_TRUE(stats->read(statId("power"))->effective==10);
    demo::require(stats->applyEffect(effect,EffectPolicy::Independent,0));
    for(int i=0;i<3;++i)f.world.fixedUpdate(.016f);
    CHECK_TRUE(stats->read(statId("power"))->effective==10);CHECK_TRUE(observer->publications==5);
    f.world.getEntityByHandle(h)->removeComponent<StatsComponent>();CHECK_TRUE(observer->reasons.back()==PublicationReason::Detach);
}
TEST_CASE(observer_tracks_incarnations_replica_and_whole_bank_publication) {
    GameplayFixture f;auto [a,actor]=f.make();auto [b,victim]=f.make();auto observer=std::make_shared<Observer>();
    demo::require(f.system->observeState(observer));
    const auto generation=actor->generation();demo::require(actor->configure(actor->state()->schema()));
    CHECK_TRUE(actor->generation()!=generation && observer->reasons.back()==PublicationReason::Configure);
    int callbacks=0;actor->setOnSettled([&](const auto&){++callbacks;CHECK_TRUE(observer->batchPublications==2);
        CHECK_TRUE(observer->publications==3);CHECK_TRUE(victim->read(statId("health"))->current==70);});
    const std::array actions{StatsSystem::AttributeAction{1,a,"health",AttributeOp::Decrease,20},
        StatsSystem::AttributeAction{2,b,"health",AttributeOp::Decrease,30}};
    CHECK_TRUE(f.system->changeBatch(actions).has_value());CHECK_TRUE(callbacks==1);
    auto [c,replica]=f.make();replica->setAuthority(false);replica->wireState=victim->wireState;
    f.world.fixedUpdate(.016f);CHECK_TRUE(replica->read(statId("health"))->current==70);
    CHECK_TRUE(observer->reasons.back()==PublicationReason::Replica);
    std::vector<std::shared_ptr<Observer>> retained;
    for(int i=0;i<15;++i){retained.push_back(std::make_shared<Observer>());CHECK_TRUE(f.system->observeState(retained.back()).has_value());}
    CHECK_TRUE(f.system->observeState(std::make_shared<Observer>()).error().code==Error::Limit);
    retained.pop_back();CHECK_TRUE(f.system->observeState(std::make_shared<Observer>()).has_value());
}
TEST_CASE(stale_handles_and_replica_inputs_reject_single_and_batch_actions) {
    GameplayFixture f;auto [a,actor]=f.make();auto [b,victim]=f.make();actor->setAuthority(false);
    CHECK_TRUE(f.system->decrease(a,"health",1).error().code==Error::Invalid);
    CHECK_TRUE(f.system->decrease(b,"health",1,"",a).error().code==Error::Invalid);
    const std::array actions{StatsSystem::AttributeAction{1,b,"health",AttributeOp::Decrease,1,"",a}};
    CHECK_TRUE(f.system->changeBatch(actions).error().code==Error::Invalid);CHECK_TRUE(victim->read(statId("health"))->current==100);
    f.world.destroyEntity(f.world.getEntityByHandle(a));
    CHECK_TRUE(f.system->decrease(a,"health",1).error().code==Error::Stale);
    CHECK_TRUE(f.system->changeBatch(actions).error().code==Error::Stale);
}
TEST_CASE(authored_gameplay_example_preserves_actual_leech_zero_and_effect_checkpoint) {
    GameplayFixture f;const auto r=demo::runGameplayScenario(f.world,AY_STATS_EXAMPLE_ASSETS,ayt::test::testTmpPath("gameplay-saves"));
    CHECK_TRUE(r.buffedAttack==198);CHECK_TRUE(r.absorbed==30);CHECK_TRUE(r.theoreticalHealthDamage==69);
    CHECK_TRUE(r.actualHealthDamage==50 && r.excess==19);CHECK_TRUE(r.actualLeech==10);
    CHECK_TRUE(r.sourceHealth==50 && r.targetHealth==0 && r.targetShield==0);
    CHECK_TRUE(r.restoredHealth==50 && r.restoredTargetHealth==0);CHECK_TRUE(r.attackAfterExpiry==100);
    CHECK_TRUE(r.depletionNotifications==1);CHECK_TRUE(r.lateResultRejected);
}
TEST_SUITE_END
