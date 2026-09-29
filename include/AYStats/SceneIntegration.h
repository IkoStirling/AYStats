#pragma once
#include <AYStats/EntityIntegration.h>
namespace ayt::event {class EventBus;}
namespace ayt::scene {class SceneManager;}
namespace ayt::stats {
/// Host-scoped subscription. Installs the single Sim system for each current/edit/play World.
/// Owner calls disconnect while bus/manager are alive; destruction invalidates weak callbacks.
class StatsSceneBinding {
public:
    StatsSceneBinding();
    ~StatsSceneBinding();
    Result<void> bind(ayt::event::EventBus& bus,ayt::scene::SceneManager& scenes);
    void disconnect();
    std::optional<Diagnostic> lastError() const;
private:
    struct Impl;
    std::shared_ptr<Impl> _impl;
};
/// Optional standard Application/Editor startup node. Keeps Scene/Host out of Core.
class StatsHostIntegrationModule final : public ayt::module::IModule {
public:
    StatsHostIntegrationModule();
    const ayt::module::ModuleDescriptor& descriptor() const noexcept override {return _descriptor;}
    ayt::module::ModuleResult install(ayt::module::IModuleContext&) override;
    void shutdown(ayt::module::IModuleContext&) noexcept override;
private:
    ayt::module::ModuleDescriptor _descriptor;
    StatsSceneBinding _binding;
};
} // namespace ayt::stats
