#pragma once
#include <AYStats/AssetIO.h>
#include <AYStats/Batch.h>
#include <AYEntity/IEntity.h>
#include <AYEntity/EntityHandle.h>
#include <AYModule/IModule.h>
#include <functional>
#include <deque>
namespace ayt::entity {class World;class ComponentRegistry;class HealthComponent;}
namespace ayt::stats {
inline constexpr std::uint32_t kStatsWire=std::uint32_t(ayt::reflect::FieldAttribute::NetReplicate);
/// Deferred delivery lets a higher-level transaction finish its own bookkeeping first.
enum class NotificationDelivery { Immediate, NextTick };
#define AY_CURRENT_CLASS StatsComponent
/// Optional scene/reflection facade. Definitions are authored; wireState is authority-produced.
class StatsComponent final : public ayt::entity::IComponent {
public:
    StatsComponent();
    const char* getName() const override {return "Stats";}
    AY_PROPERTY(std::string,definitionsJson,ayt::entity::kAttrSerialize)
    AY_PROPERTY(std::string,wireState,kStatsWire)
    void onAttach(ayt::entity::Entity*) override;
    void onStart() override;
    /// Replace authored definitions atomically; invalid data leaves current state intact.
    Result<void> configure(std::shared_ptr<const Schema> schema);
    Result<Row> read(StatId id) const;
    /// NextTick reserves notification storage before mutation; drains on the next Stats Sim update.
    Result<Settlement> commit(std::span<const Mutation> mutations,std::uint64_t expectedVersion,
                              NotificationDelivery delivery=NotificationDelivery::Immediate);
    /// Owner callback after publication. Reentrant mutation is rejected; enqueue next-tick commands.
    void setOnSettled(std::function<void(const Settlement&)> callback) {_onSettled=std::move(callback);}
    Result<void> enqueue(std::vector<Mutation> mutations);
    Result<void> applyEffect(Effect effect,EffectPolicy policy,std::uint64_t tick);
    /// Explicit ownership decision by Host; clients cannot commit or apply local effects.
    void setAuthority(bool authority) noexcept {_authority=authority;}
    bool authority() const noexcept {return _authority;}
    std::uint64_t generation() const noexcept {return _generation;}
    std::uint64_t version() const noexcept {return _state?_state->version():0;}
    const StatSet* state() const noexcept {return _state?&*_state:nullptr;}
    const std::optional<Diagnostic>& lastError() const noexcept {return _error;}
    /// Runtime saves use a separate checkpoint, not the authored scene definition property.
    Result<std::string> checkpoint() const;
    Result<void> restoreCheckpoint(std::string_view text);
private:
    Result<void> synchronize();
    Result<void> advance(std::uint64_t tick);
    void publish();
    void notify(const Settlement& result);
    std::optional<StatSet> _state;
    EffectManager _effects;
    CalculationContext _context;
    std::string _appliedDefinition,_appliedWire;
    std::uint64_t _generation=0,_remoteVersion=0;
    bool _authority=true;
    bool _notifying=false;
    std::function<void(const Settlement&)> _onSettled;
    std::deque<std::vector<Mutation>> _pending;
    std::vector<Settlement> _deferredSettlements;
    std::optional<Diagnostic> _error;
    friend class StatsSystem;
    friend Result<BatchSettlement> settleEntityBatch(ayt::entity::World&,std::uint64_t,std::span<const ayt::entity::EntityHandle>,std::span<const std::uint64_t>,std::span<const std::uint64_t>,std::span<const SettlementRequest>,BatchSemantics,ayt::task::ITaskScheduler*);
};
#undef AY_CURRENT_CLASS
/// One World-owned Sim lane system; explicit bind avoids singleton tick ambiguity.
class StatsSystem final : public ayt::entity::ISystem {
public:
    StatsSystem();
    const char* getName() const override {return "StatsSystem";}
    void onUpdate(float) override;
    void bind(ayt::entity::World& world) noexcept {_world=&world;}
    std::uint64_t worldToken() const noexcept {return _token;}
    std::uint64_t tick() const noexcept {return _tick;}
private:
    ayt::entity::World* _world=nullptr;
    std::uint64_t _token=0,_tick=0;
};
/// Install for each newly created World; does not create a second Host tick authority.
Result<void> registerStatsSystem(ayt::entity::World& world);
/// Explicit startup registration before ComponentRegistry::seal.
Result<void> registerStatsComponent(ayt::entity::ComponentRegistry& registry);
/// Opt-in startup node; existing Scene hosts call registerStatsSystem for subsequent Worlds.
class StatsIntegrationModule final : public ayt::module::IModule {
public:
    StatsIntegrationModule();
    const ayt::module::ModuleDescriptor& descriptor() const noexcept override {return _descriptor;}
    ayt::module::ModuleResult registerTypes(ayt::module::IModuleContext&) override;
    ayt::module::ModuleResult install(ayt::module::IModuleContext&) override;
private:
    ayt::module::ModuleDescriptor _descriptor;
};
/// Validate World/Entity/component/state identity before synchronous owned-shadow parallel work.
Result<BatchSettlement> settleEntityBatch(ayt::entity::World& world,std::uint64_t worldToken,
    std::span<const ayt::entity::EntityHandle> entities,std::span<const std::uint64_t> componentGenerations,
    std::span<const std::uint64_t> expectedVersions,std::span<const SettlementRequest> requests,
    BatchSemantics semantics,ayt::task::ITaskScheduler* scheduler=nullptr);
/// Explicit legacy conversion helpers; callers select one authority rather than two mutable mirrors.
Result<StatSet> importLegacyHealth(const ayt::entity::HealthComponent& legacy,std::string name="health");
Result<void> exportLegacyHealth(const StatSet& state,StatId resource,ayt::entity::HealthComponent& legacy);
} // namespace ayt::stats
