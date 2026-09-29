#include <AYTest.h>
#include <AYStats/EntityIntegration.h>
#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/SceneSerializer.h>
#include <array>
using namespace ayt::stats;
namespace {
struct WorldFixture {
    ayt::entity::World& world=ayt::entity::World::processWorld();
    WorldFixture(){ayt::entity::World::setActiveWorld(nullptr);world.initialize();auto& registry=ayt::entity::ComponentRegistry::instance();if(!registry.isSealed()){(void)ayt::entity::registerEntityCoreComponents(registry);if(!registerStatsComponent(registry))throw std::runtime_error("Stats registration failed");registry.seal();}if(!registerStatsSystem(world))throw std::runtime_error("Stats system failed");}
    ~WorldFixture(){world.shutdown();}
    StatsComponent* make(){auto* entity=world.createEntity();auto* stats=entity->addComponent<StatsComponent>();auto schema=Schema::compile({{.name="health",.shape=Shape::Pool,.current=0,.capacity=100}}).value();if(!stats->configure(schema))throw std::runtime_error("configure failed");return stats;}
};
}
TEST_SUITE(StatsEntityTests)
TEST_CASE(zero_current_fixed_tick_and_checkpoint_restore) {
    WorldFixture f;auto* stats=f.make();CHECK_TRUE(stats->read(statId("health"))->current==0);f.world.fixedUpdate(1.0f/60);CHECK_TRUE(stats->read(statId("health"))->current==0);
    const auto saved=stats->checkpoint().value();CHECK_TRUE(stats->commit(std::array{Mutation{statId("health"),MutationOp::Increase,20}},stats->version()).has_value());
    CHECK_TRUE(stats->restoreCheckpoint(saved).has_value());CHECK_TRUE(stats->read(statId("health"))->current==0);
    auto* system=dynamic_cast<StatsSystem*>(f.world.findSystemByName("StatsSystem"));CHECK_TRUE(system->tick()==1);f.world.updatePresentation(.1f,1);CHECK_TRUE(system->tick()==1);
}
TEST_CASE(scene_roundtrip_keeps_authored_state_and_dynamic_registration) {
    WorldFixture f;auto* stats=f.make();auto* entity=f.world.getAllEntities().front();entity->setName("StatsOwner");
    CHECK_TRUE(stats->commit(std::array{Mutation{statId("health"),MutationOp::Increase,20}},stats->version()).has_value());
    const auto path=ayt::test::testTmpPath("stats-scene.ayscene");CHECK_TRUE(ayt::entity::saveScene(f.world,path));f.world.destroyEntity(entity);
    ayt::serializer::SerializeError error;CHECK_TRUE(ayt::entity::loadScene(f.world,path,&error));auto* loaded=f.world.findEntity("StatsOwner");CHECK_NOT_NULL(loaded);
    loaded->onStart();CHECK_TRUE(loaded->getComponent<StatsComponent>()->read(statId("health"))->current==0);
}
TEST_CASE(replica_authority_and_definition_mismatch) {
    WorldFixture f;auto* authority=f.make();auto* replica=f.make();replica->setAuthority(false);
    CHECK_TRUE(authority->commit(std::array{Mutation{statId("health"),MutationOp::Increase,30}},authority->version()).has_value());
    replica->wireState=authority->wireState;f.world.fixedUpdate(.016f);CHECK_TRUE(replica->read(statId("health"))->current==30);
    CHECK_FALSE(replica->commit(std::array{Mutation{statId("health"),MutationOp::Increase,1}},replica->version()).has_value());
    auto bad=StatSet::create(Schema::compile({{.name="other"}}).value()).value();replica->wireState=encodeState(bad).value();f.world.fixedUpdate(.016f);CHECK_TRUE(replica->lastError()->code==Error::Stale);CHECK_TRUE(replica->read(statId("health"))->current==30);
}
TEST_CASE(component_and_world_generations_reject_late_results) {
    WorldFixture f;auto* stats=f.make();auto* entity=f.world.getAllEntities().front();auto* system=dynamic_cast<StatsSystem*>(f.world.findSystemByName("StatsSystem"));
    const std::array entities{f.world.getEntityHandle(entity->getId())};const std::array generations{stats->generation()};const std::array versions{stats->version()};const auto token=system->worldToken();
    CHECK_TRUE(settleEntityBatch(f.world,token,entities,generations,versions,{},BatchSemantics::Snapshot).has_value());
    entity->removeComponent<StatsComponent>();auto* replacement=entity->addComponent<StatsComponent>();CHECK_TRUE(replacement->configure(Schema::compile({{.name="health",.shape=Shape::Pool,.current=0,.capacity=100}}).value()).has_value());
    CHECK_TRUE(settleEntityBatch(f.world,token,entities,generations,versions,{},BatchSemantics::Snapshot).error().code==Error::Stale);
    f.world.shutdown();f.world.initialize();CHECK_TRUE(registerStatsSystem(f.world).has_value());CHECK_TRUE(settleEntityBatch(f.world,token,entities,generations,versions,{},BatchSemantics::Snapshot).error().code==Error::Stale);
}
TEST_CASE(legacy_conversion_keeps_zero_and_notifies_actual_changes) {
    ayt::entity::HealthComponent legacy;legacy.currentHp=0;auto state=importLegacyHealth(legacy);CHECK_TRUE(state.has_value());CHECK_TRUE(state->read(statId("health"))->current==0);
    int notifications=0;legacy.onHpChanged=[&](auto*,auto,auto){++notifications;};CHECK_TRUE(state->commit(std::array{Mutation{statId("health"),MutationOp::Increase,20}},state->version()).has_value());
    CHECK_TRUE(exportLegacyHealth(*state,statId("health"),legacy).has_value());CHECK_TRUE(legacy.currentHp==20);CHECK_TRUE(notifications==1);
}
TEST_CASE(post_commit_notifications_reject_reentry_and_queue_next_tick) {
    WorldFixture f;auto* stats=f.make();int events=0;
    stats->setOnSettled([&](const Settlement& result){++events;CHECK_TRUE(stats->read(statId("health"))->current==result.changes.back().after);CHECK_TRUE(stats->commit(std::array{Mutation{statId("health"),MutationOp::Increase,1}},stats->version()).error().code==Error::Reentrant);if(events==1)CHECK_TRUE(stats->enqueue({{statId("health"),MutationOp::Increase,5}}).has_value());});
    CHECK_TRUE(stats->commit(std::array{Mutation{statId("health"),MutationOp::Increase,20}},stats->version()).has_value());CHECK_TRUE(events==1);CHECK_TRUE(stats->read(statId("health"))->current==20);
    f.world.fixedUpdate(.016f);CHECK_TRUE(events==2);CHECK_TRUE(stats->read(statId("health"))->current==25);stats->setOnSettled({});
}
TEST_SUITE_END
