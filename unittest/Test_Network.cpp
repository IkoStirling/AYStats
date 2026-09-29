#include <AYTest.h>
#include <AYStats/EntityIntegration.h>
#include <AYEntity.h>
#include <AYNetwork/Replication/EntityReplicationAdapter.h>
#include <AYNetwork/Replication/ReflectSerializer.h>
#include <AYReflect/detail/ReflectImpl.h>
using namespace ayt::stats;
TEST_SUITE(StatsNetworkTests)
TEST_CASE(reflected_snapshot_applies_at_sim_boundary_and_unregisters) {
    auto& world=ayt::entity::World::processWorld();world.initialize();CHECK_TRUE(registerStatsSystem(world).has_value());
    auto* server=world.createEntity();auto* client=world.createEntity();auto* authority=server->addComponent<StatsComponent>();auto* replica=client->addComponent<StatsComponent>();
    auto schema=Schema::compile({{.name="energy",.shape=Shape::Pool,.current=0,.capacity=100}}).value();CHECK_TRUE(authority->configure(schema).has_value());CHECK_TRUE(replica->configure(schema).has_value());replica->setAuthority(false);
    ayt::net::ReplicationManager manager(nullptr);CHECK_TRUE(ayt::net::EntityReplicationAdapter::registerEntityComponent<StatsComponent>(manager,server,701));CHECK_TRUE(manager.findObject(701)==authority);
    const Mutation mutation{statId("energy"),MutationOp::Increase,40};CHECK_TRUE(authority->commit(std::span(&mutation,1),authority->version()).has_value());
    auto* type=ayt::reflect::TypeRegistryImpl::instance().findType<StatsComponent>();ayt::net::BitStream stream;
    CHECK_TRUE(ayt::net::ReflectSerializer::serializeObject(type,authority,701,stream));
    stream.resetForRead();ayt::net::ReflectSerializer::FrameHeader header;CHECK_TRUE(ayt::net::ReflectSerializer::readReplicationFrameHeader(stream,header));
    CHECK_TRUE(ayt::net::ReflectSerializer::deserializeObject(type,replica,stream,header.fieldCount));CHECK_TRUE(replica->read(statId("energy"))->current==0);
    world.fixedUpdate(.016f);CHECK_TRUE(replica->read(statId("energy"))->current==40);
    CHECK_TRUE(replica->definitionsJson==authority->definitionsJson); // authored schema is not overwritten by wire
    ayt::net::EntityReplicationAdapter::unregisterEntityComponent(manager,701);CHECK_TRUE(manager.findObject(701)==nullptr);world.shutdown();
}
TEST_SUITE_END
