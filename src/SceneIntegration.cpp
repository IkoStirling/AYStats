#include <AYStats/SceneIntegration.h>
#include <AYScene.h>
#include <AYScene/SceneManager.h>
#include <AYEventSystem/EventBus.h>
#include <AYEventSystem/Events/SceneEvents.h>
namespace ayt::stats {
struct StatsSceneBinding::Impl {
    ayt::event::EventBus* bus=nullptr;ayt::scene::SceneManager* scenes=nullptr;
    std::vector<ayt::event::ConnectionId> subscriptions;
    std::optional<Diagnostic> error;
    Result<void> installCurrent() {
        // Resolve live manager pointers instead of retaining possibly destroyed event payloads.
        for(auto* scene:{scenes->current(),scenes->edit(),scenes->play()})if(scene){auto result=registerStatsSystem(scene->world());if(!result){error=result.error();return result;}}
        return {};
    }
};
StatsSceneBinding::StatsSceneBinding()=default;StatsSceneBinding::~StatsSceneBinding()=default;
Result<void> StatsSceneBinding::bind(ayt::event::EventBus& bus,ayt::scene::SceneManager& scenes) {
    disconnect();auto state=std::make_shared<Impl>();state->bus=&bus;state->scenes=&scenes;_impl=state;
    const std::weak_ptr<Impl> weak=state;
    auto install=[weak](const auto&){if(auto owner=weak.lock())(void)owner->installCurrent();};
    state->subscriptions.push_back(bus.subscribe<ayt::event::SceneCurrentChangedEvent>(install));
    state->subscriptions.push_back(bus.subscribe<ayt::event::SceneBeginPlayEvent>(install));
    state->subscriptions.push_back(bus.subscribe<ayt::event::SceneEndPlayEvent>(install));
    return state->installCurrent();
}
void StatsSceneBinding::disconnect(){if(!_impl)return;for(auto id:_impl->subscriptions)_impl->bus->unsubscribe(id);_impl.reset();}
std::optional<Diagnostic> StatsSceneBinding::lastError() const{return _impl?_impl->error:std::optional<Diagnostic>{};}
} // namespace ayt::stats
