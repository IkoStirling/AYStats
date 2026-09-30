#include <AYStats/EntityIntegration.h>
#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYTask/TaskScheduler.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace ayt::stats;
using Clock=std::chrono::steady_clock;
double milliseconds(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
template<class T> T require(Result<T> value){if(!value)throw std::runtime_error(value.error().message);return std::move(*value);}
void require(Result<void> value){if(!value)throw std::runtime_error(value.error().message);}
struct Observer final:StateObserver {
    std::size_t prepared=0,publishedCount=0;
    Result<void> prepare(ayt::entity::EntityHandle,const StatsComponent&,const Schema&,PublicationReason,std::size_t) override {++prepared;return {};}
    void published(ayt::entity::EntityHandle,const StatsComponent&,PublicationReason reason) noexcept override {if(reason==PublicationReason::Commit)++publishedCount;}
};
int main(int argc,char** argv) {
    auto& world=ayt::entity::World::processWorld();
    try {
        const auto samples=argc>1?std::stoull(argv[1]):5;const bool profiling=argc>2 && std::string_view(argv[2])=="profile";
        if(!samples || samples>100)throw std::runtime_error("samples must be 1..100");
        world.initialize();auto& registry=ayt::entity::ComponentRegistry::instance();
        const auto registered=ayt::entity::registerEntityCoreComponents(registry);if(!registered)throw std::runtime_error(registered.message());
        require(registerStatsComponent(registry));registry.seal();require(registerStatsSystem(world));
        auto* system=dynamic_cast<StatsSystem*>(world.findSystemByName("StatsSystem"));
        const auto observer=std::make_shared<Observer>();require(system->observeState(observer));
        const auto schema=require(Schema::compile({{.name="resource",.shape=Shape::Pool,.current=100,.capacity=100}}));
        const auto id=statId("resource");const auto plan=std::make_shared<const CalculationPlan>(require(CalculationPlan::compile({{.id=1,.op=Op::Input},{.id=2,.op=Op::Constant,.value=1},{.id=3,.op=Op::Minimum,.a=1,.b=2}},3,1)));
        ayt::task::TaskScheduler scheduler(4);
        std::cout<<"requests,distribution,semantics,sample,profiling,construction_ms,entity_settlement_ms,callbacks,actual_deduction,entity_preparation_ms,entity_publication_ms,notification_ms,core_validation_ms,core_grouping_ms,core_clone_ms,core_execution_wall_ms,core_publication_ms,observer_publications\n";
        for(std::size_t n:{1000,100000,1000000})for(int distribution=0;distribution<3;++distribution)for(const auto semantics:{BatchSemantics::Snapshot,BatchSemantics::Ordered})for(std::size_t run=0;run<=samples;++run) {
            const auto start=Clock::now();const auto targets=distribution==0?1:std::min<std::size_t>(n,1000);
            std::vector<ayt::entity::EntityHandle> handles;std::vector<std::uint64_t> generations,versions;
            handles.reserve(targets);generations.reserve(targets);versions.reserve(targets);
            std::size_t callbacks=0;Value deducted=0;
            for(std::size_t i=0;i<targets;++i){auto* entity=world.createEntity();auto* stats=entity->addComponent<StatsComponent>();require(stats->configure(schema));
                handles.push_back(world.getEntityHandle(entity->getId()));generations.push_back(stats->generation());versions.push_back(stats->version());
                stats->setOnSettled([&](const Settlement& settlement){++callbacks;deducted-=settlement.changes[0].actual;});}
            std::vector<SettlementRequest> requests;requests.reserve(n);
            for(std::size_t i=0;i<n;++i){const auto target=i%targets,source=distribution==2?(target+1)%targets:target;
                requests.push_back({.id=i+1,.plan=plan,.inputs={{source,id,ReadField::Current}},.writes={{target,id,MutationOp::Decrease}}});}
            const auto prepared=Clock::now();const auto observerBefore=observer->publishedCount;SettlementMetrics metrics;
            auto result=settleEntityBatch(world,system->worldToken(),handles,generations,versions,requests,semantics,&scheduler,profiling?&metrics:nullptr);
            const auto completed=Clock::now();if(!result)throw std::runtime_error(result.error().message);
            Value attributed=0,remaining=0;
            for(const auto& row:result->results)for(const auto& settlement:row)attributed-=settlement.changes[0].actual;
            for(const auto handle:handles)remaining+=require(world.getEntityByHandle(handle)->getComponent<StatsComponent>()->read(id)).current;
            if(callbacks!=n || deducted!=attributed || remaining+deducted!=Value(targets*100) || observer->publishedCount-observerBefore!=targets)throw std::runtime_error("callback/observer attribution mismatch");
            if(profiling && metrics.notifiedWrites!=n)throw std::runtime_error("notification metrics mismatch");
            if(run)std::cout<<n<<','<<(distribution==0?"hotspot":distribution==1?"distributed":"cross-dependent")<<','<<(semantics==BatchSemantics::Ordered?"ordered":"snapshot")<<','<<run<<','<<profiling<<','<<milliseconds(start,prepared)<<','<<milliseconds(prepared,completed)<<','<<callbacks<<','<<deducted<<','<<metrics.entityPreparationMs<<','<<metrics.entityPublicationMs<<','<<metrics.notificationMs<<','<<metrics.validationMs<<','<<metrics.groupingMs<<','<<metrics.cloneMs<<','<<metrics.executionWallMs<<','<<metrics.publicationMs<<','<<observer->publishedCount-observerBefore<<'\n';
            for(const auto handle:handles)world.destroyEntity(world.getEntityByHandle(handle));
        }
        scheduler.shutdown();world.shutdown();return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';world.shutdown();return 1;}
}
