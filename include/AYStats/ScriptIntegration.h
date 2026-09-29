#pragma once
#include <AYStats/EntityIntegration.h>
namespace ayt::stats {
/// Host-owned receiver for trusted Logia scripts; stores handles, never component pointers.
/// Decimal strings preserve the full int64/uint64 contract across script marshalling.
class StatsScriptFacade {
public:
    Result<void> bind(ayt::entity::World& world,ayt::entity::EntityHandle entity);
    void unbind() noexcept {_world=nullptr;}
    std::string read(std::string name,std::string field);
    std::string version();
    bool change(std::string name,std::string operation,std::string amount,std::string expectedVersion);
    std::string error() const {return _error;}
private:
    StatsComponent* resolve();
    ayt::entity::World* _world=nullptr;
    ayt::entity::EntityHandle _entity;
    std::uint64_t _worldToken=0,_generation=0;
    std::string _error;
};
/// Explicit before scripts are compiled; idempotent reflected method registration.
Result<void> registerStatsScriptFacade();
} // namespace ayt::stats
