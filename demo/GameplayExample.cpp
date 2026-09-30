#include "GameplayScenario.h"
#include <AYEntity/EntityModule.h>
#include <iostream>

int main(int argc,char** argv) {
    auto& world=ayt::entity::World::processWorld();
    try {
        world.initialize();
        auto& registry=ayt::entity::ComponentRegistry::instance();
        auto registered=ayt::entity::registerEntityCoreComponents(registry);
        if(!registered)throw std::runtime_error(registered.message());
        ayt::stats::demo::require(ayt::stats::registerStatsComponent(registry));registry.seal();
        ayt::stats::demo::require(ayt::stats::registerStatsSystem(world));
        const auto assets=argc>1?std::filesystem::path(argv[1]):std::filesystem::path(AY_STATS_EXAMPLE_ASSETS);
        const auto saves=argc>2?std::filesystem::path(argv[2]):std::filesystem::temp_directory_path()/"AYStatsGameplayExample";
        const auto r=ayt::stats::demo::runGameplayScenario(world,assets,saves);
        std::cout<<"Buffed attack: "<<r.buffedAttack<<"\nShield absorbed: "<<r.absorbed
            <<"\nHealth damage requested/actual/excess: "<<r.theoreticalHealthDamage<<"/"<<r.actualHealthDamage<<"/"<<r.excess
            <<"\nActual leech: "<<r.actualLeech<<"\nSource/target health: "<<r.sourceHealth<<"/"<<r.targetHealth
            <<"\nRestored source/target health: "<<r.restoredHealth<<"/"<<r.restoredTargetHealth
            <<"\nAttack after buff expiry: "<<r.attackAfterExpiry
            <<"\nDepletion notifications: "<<r.depletionNotifications
            <<"\nLate result rejected: "<<r.lateResultRejected<<"\nRuntime saves: "<<saves.string()<<'\n';
        world.shutdown();return r.lateResultRejected && r.depletionNotifications==1?0:1;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';world.shutdown();return 1;}
}
