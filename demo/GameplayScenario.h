#pragma once
#include <AYStats/EntityIntegration.h>
#include <AYEntity.h>
#include <array>
#include <stdexcept>

// Example gameplay policy belongs here, outside the general Stats implementation.
namespace ayt::stats::demo {
template<class T> T require(Result<T> result) {
    if(!result)throw std::runtime_error(result.error().message+": "+result.error().path);
    return std::move(*result);
}
inline void require(Result<void> result) {
    if(!result)throw std::runtime_error(result.error().message+": "+result.error().path);
}
struct GameplayReport {
    Value buffedAttack=0, absorbed=0, theoreticalHealthDamage=0, actualHealthDamage=0;
    Value excess=0, actualLeech=0, sourceHealth=0, targetHealth=0, targetShield=0;
    Value restoredHealth=0, restoredTargetHealth=0, attackAfterExpiry=0;
    int depletionNotifications=0;
    bool lateResultRejected=false;
};
/// Run on an initialized World with Stats types registered and StatsSystem installed.
/// Creates two temporary entities, reads authored loose assets, and retains runtime saves.
inline GameplayReport runGameplayScenario(ayt::entity::World& world,
    const std::filesystem::path& assets,const std::filesystem::path& saves) {
    auto* system=dynamic_cast<StatsSystem*>(world.findSystemByName("StatsSystem"));
    if(!system)throw std::runtime_error("install StatsSystem before running the example");
    auto* source=world.createEntity();auto* target=world.createEntity();
    const auto sourceHandle=world.getEntityHandle(source->getId()),targetHandle=world.getEntityHandle(target->getId());
    struct Cleanup {
        ayt::entity::World& world;std::array<ayt::entity::EntityHandle,2> handles;
        ~Cleanup(){for(auto h:handles)if(auto* e=world.getEntityByHandle(h))world.destroyEntity(e);}
    } cleanup{world,{sourceHandle,targetHandle}};
    auto* actor=source->addComponent<StatsComponent>();auto* victim=target->addComponent<StatsComponent>();
    require(actor->configure(require(decodeDefinitions(require(readText(assets/"source.stats.json"))))));
    require(victim->configure(require(decodeDefinitions(require(readText(assets/"target.stats.json"))))));
    require(system->loadRecipes(assets/"combat.stats-recipes.json",*victim->state()->schema(),
                                actor->state()->schema().get()));
    // Flat, summed percentage and independent multiplier have distinct authored zones.
    require(actor->applyEffect({.id=1,.key=1,.expires=system->tick()+3,.modifiers={
        {.handle=1,.rule=1,.target=statId("attack"),.stage=1,.zone=1,.value=20},
        {.handle=2,.rule=2,.target=statId("attack"),.stage=1,.zone=2,.value=5000},
        {.handle=3,.rule=3,.target=statId("attack"),.stage=1,.zone=3,.value=11000}
    }},EffectPolicy::Independent,system->tick()));
    require(system->set(sourceHandle,"health",40)); // Basic direct operation, no formula required.
    GameplayReport report;report.buffedAttack=require(actor->read(statId("attack"))).effective;
    victim->setOnSettled([&](const Settlement& result){
        for(const auto& change:result.changes)if(change.target==statId("health") &&
            change.field==ChangeField::Current && change.before>0 && change.after==0)++report.depletionNotifications;
    });
    // Both outputs must sample the same shield: Ordered would re-read an already depleted shield.
    const std::array actions{
        StatsSystem::AttributeAction{1,targetHandle,"shield",AttributeOp::Decrease,5000,"absorb",sourceHandle},
        StatsSystem::AttributeAction{2,targetHandle,"health",AttributeOp::Decrease,5000,"spill",sourceHandle}};
    auto hit=require(system->changeBatch(actions,BatchSemantics::Snapshot));
    report.absorbed=-hit.results[0][0].changes[0].actual;
    report.theoreticalHealthDamage=hit.values[1];
    report.actualHealthDamage=-hit.results[1][0].changes[0].actual;
    report.excess=hit.results[1][0].changes[0].excess;
    // The gameplay layer chooses actual health loss as the leech basis; shield/overkill do not heal.
    auto healed=require(system->increase(sourceHandle,"health",report.actualHealthDamage,"leech"));
    report.actualLeech=healed.changes[0].actual;
    report.sourceHealth=require(actor->read(statId("health"))).current;
    report.targetHealth=require(victim->read(statId("health"))).current;
    report.targetShield=require(victim->read(statId("shield"))).current;
    std::filesystem::create_directories(saves);
    require(writeTextAtomic(saves/"source.checkpoint.json",require(actor->checkpoint())));
    require(writeTextAtomic(saves/"target.checkpoint.json",require(victim->checkpoint())));
    const std::array handles{targetHandle};const std::array generations{victim->generation()},versions{victim->version()};
    require(system->set(sourceHandle,"health",1));
    require(actor->restoreCheckpoint(require(readText(saves/"source.checkpoint.json"))));
    require(victim->restoreCheckpoint(require(readText(saves/"target.checkpoint.json"))));
    report.restoredHealth=require(actor->read(statId("health"))).current;
    report.restoredTargetHealth=require(victim->read(statId("health"))).current;
    auto late=settleEntityBatch(world,system->worldToken(),handles,generations,versions,{},BatchSemantics::Snapshot);
    report.lateResultRejected=!late && late.error().code==Error::Stale;
    for(int i=0;i<3;++i)world.fixedUpdate(1.f/60);
    report.attackAfterExpiry=require(actor->read(statId("attack"))).effective;
    victim->setOnSettled({});require(system->setRecipes(nullptr));
    return report;
}
} // namespace ayt::stats::demo
