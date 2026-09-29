#include <AYTest.h>
#include <AYStats/SceneIntegration.h>
#include <AYScene.h>
#include <AYScene/SceneManager.h>
#include <AYEventSystem/EventBus.h>
#include <AYEventSystem/Events/SceneEvents.h>
#include <AYEntity.h>
using namespace ayt::stats;
TEST_SUITE(StatsSceneTests)
TEST_CASE(host_events_install_one_fixed_system_for_each_new_world) {
    auto& manager=ayt::scene::SceneManager::instance();auto& bus=ayt::event::EventBus::instance();
    ayt::scene::Scene first(ayt::scene::SceneMode::Edit,"StatsFirst"),second(ayt::scene::SceneMode::Play,"StatsSecond");
    StatsSceneBinding binding;CHECK_TRUE(binding.bind(bus,manager).has_value());
    manager.setCurrent(&first);bus.emit(ayt::event::SceneCurrentChangedEvent{&first});
    auto* a=dynamic_cast<StatsSystem*>(first.world().findSystemByName("StatsSystem"));CHECK_NOT_NULL(a);first.world().fixedUpdate(.016f);CHECK_TRUE(a->tick()==1);
    manager.setCurrent(&second);bus.emit(ayt::event::SceneCurrentChangedEvent{&second});
    auto* b=dynamic_cast<StatsSystem*>(second.world().findSystemByName("StatsSystem"));CHECK_NOT_NULL(b);CHECK_TRUE(a->worldToken()!=b->worldToken());
    bus.emit(ayt::event::SceneBeginPlayEvent{&second});second.world().fixedUpdate(.016f);CHECK_TRUE(b->tick()==1);CHECK_TRUE(a->tick()==1);
    binding.disconnect();manager.setCurrent(nullptr);
}
TEST_SUITE_END
