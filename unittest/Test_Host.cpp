#include <AYTest.h>
#include <AYStats/SceneIntegration.h>
#include <AYApplication/EngineModuleRuntime.h>
#include <AYApplication/IEngineHost.h>
#include <AYEntity/EntityComponentModule.h>
#include <AYEntity/EntityRuntimeModule.h>
#include <AYEntity/ComponentRegistry.h>
#include <AYEntity.h>
#include <AYGameLoop/SubSystemRegistry.h>
#include <AYScene.h>
#include <AYScene/SceneManager.h>
using namespace ayt::stats;
TEST_SUITE(StatsHostTests)
TEST_CASE(startup_graph_registers_before_seal_and_unsubscribes_on_shutdown) {
    auto& host=ayt::app::defaultEngineHost();
    ayt::app::bindBuiltinHostServices(host);
    ayt::app::EngineModuleRuntime runtime(host);
    CHECK_TRUE(runtime.modules().emplace<ayt::entity::EntityComponentModule>().succeeded());
    CHECK_TRUE(runtime.modules().emplace<ayt::entity::EntityRuntimeModule>().succeeded());
    CHECK_TRUE(runtime.modules().emplace<StatsIntegrationModule>().succeeded());
    CHECK_TRUE(runtime.modules().emplace<StatsHostIntegrationModule>().succeeded());
    CHECK_TRUE(runtime.prepare().succeeded());
    runtime.context().componentRegistry().seal();
    CHECK_TRUE(runtime.install().succeeded());
    ayt::scene::Scene active(ayt::scene::SceneMode::Edit,"StatsHostActive");
    host.scenes()->setCurrent(&active);
    auto* system=dynamic_cast<StatsSystem*>(active.world().findSystemByName("StatsSystem"));
    CHECK_NOT_NULL(system);
    active.world().fixedUpdate(.016f);
    CHECK_TRUE(system->tick()==1);
    host.scenes()->setCurrent(nullptr);
    runtime.shutdown();
    CHECK_TRUE(ayt::game::SubSystemRegistry::instance().getCount()==0);
    ayt::scene::Scene later(ayt::scene::SceneMode::Edit,"StatsAfterShutdown");
    host.scenes()->setCurrent(&later);
    CHECK_TRUE(later.world().findSystemByName("StatsSystem")==nullptr);
    host.scenes()->setCurrent(nullptr);
}
TEST_SUITE_END
