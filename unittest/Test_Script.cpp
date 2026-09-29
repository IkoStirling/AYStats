#include <AYTest.h>
#include <AYStats/ScriptIntegration.h>
#include <AYEntity.h>
#include <AYScript/ScriptRuntimeBridge.h>
#include <AYReflect/detail/ReflectImpl.h>
using namespace ayt::stats;
TEST_SUITE(StatsScriptTests)
TEST_CASE(logia_exact_string_read_write_authority_and_stale_handle) {
    auto& world=ayt::entity::World::processWorld();world.initialize();CHECK_TRUE(registerStatsSystem(world).has_value());
    auto* entity=world.createEntity();auto* stats=entity->addComponent<StatsComponent>();CHECK_NOT_NULL(stats);
    CHECK_TRUE(stats->configure(Schema::compile({{.name="energy",.shape=Shape::Pool,.current=0,.capacity=INT64_MAX}}).value()).has_value());
    StatsScriptFacade facade;CHECK_TRUE(facade.bind(world,world.getEntityHandle(entity->getId())).has_value());
    ayt::script::LogiaRuntimeBridge bridge;CHECK_TRUE(registerStatsScriptFacade().has_value());
    ayt::script::logia::LogiaHostContext context;context.kind=ayt::script::logia::LogiaHostKind::Component;context.hostType=ayt::reflect::TypeRegistryImpl::instance().findType<StatsScriptFacade>();context.expectSelf=true;
    std::vector<ayt::script::logia::CompilerError> errors;
    const char* source=R"(script StatsScriptFacade {
        on_start() {
            __stats_changed = tostring(self.change("energy", "increase", "9007199254740993", self.version()))
            __stats_read = self.read("energy", "current")
        }
    })";
    CHECK_TRUE(bridge.loadScript("StatsFixture",source,context,errors));CHECK_TRUE(bridge.callLifecycle("StatsFixture","on_start",&facade));
    CHECK_TRUE(bridge.getLuaGlobalString("__stats_changed")=="true");CHECK_TRUE(bridge.getLuaGlobalString("__stats_read")=="9007199254740993");
    stats->setAuthority(false);CHECK_FALSE(facade.change("energy","increase","1",facade.version()));stats->setAuthority(true);
    CHECK_FALSE(facade.change("energy","increase","1.5",facade.version()));world.destroyEntity(entity);CHECK_TRUE(facade.read("energy","current").empty());facade.unbind();world.shutdown();
}
TEST_SUITE_END
