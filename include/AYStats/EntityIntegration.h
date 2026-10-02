#pragma once
#include <AYStats/AssetIO.h>
#include <AYStats/Batch.h>
#include <AYStats/Recipes.h>
#include <AYEntity/IEntity.h>
#include <AYEntity/EntityHandle.h>
#include <AYModule/IModule.h>
#include <functional>
#include <deque>
namespace ayt::entity {class World;class ComponentRegistry;class HealthComponent;}
namespace ayt::stats {
struct RecipeAsset;
inline constexpr std::uint32_t kStatsWire=std::uint32_t(ayt::reflect::FieldAttribute::NetReplicate);
/// Deferred delivery lets a higher-level transaction finish its own bookkeeping first.
enum class NotificationDelivery { Immediate, NextTick };
class StatsComponent;
enum class PublicationReason { Commit, Effects, Configure, Restore, Replica, Detach };
/// Internal integration observer, separate from gameplay notifications. Single World owner.
/// prepare is pure; published is allocation-free/noexcept and cannot mutate owners or World.
class StateObserver {
public:
    virtual ~StateObserver() = default;
    virtual Result<void> prepare(ayt::entity::EntityHandle, const StatsComponent&, const Schema&, PublicationReason, std::size_t publications) = 0;
    virtual void published(ayt::entity::EntityHandle, const StatsComponent&, PublicationReason) noexcept = 0;
};
#define AY_CURRENT_CLASS StatsComponent
/// Optional scene/reflection facade. Definitions are authored; wireState is authority-produced.
class StatsComponent final : public ayt::entity::IComponent {
public:
    StatsComponent();
    const char* getName() const override {return "Stats";}
    AY_PROPERTY(std::string,definitionsJson,ayt::entity::kAttrSerialize)
    AY_PROPERTY(std::string,wireState,kStatsWire)
    void onAttach(ayt::entity::Entity*) override;
    void onDetach() override;
    void onStart() override;
    /// Bring authored scene definitions into the live state before the first Sim tick.
    /// Repeated calls with unchanged definitions preserve current values and effects.
    Result<void> ensureReady();
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
    /// Remove this manager's contribution atomically, then publish internal state before returning.
    Result<void> removeEffect(std::uint64_t instance);
    /// Update one owned instance, including its modifiers, without removing other same-key sources.
    Result<void> updateEffect(Effect effect, std::uint64_t tick);
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
    Result<void> preparePublication(const Schema&, PublicationReason, std::size_t publications=1);
    void publish(PublicationReason reason=PublicationReason::Commit);
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
    ayt::entity::Entity* _owner=nullptr;
    friend class StatsSystem;
    friend Result<BatchSettlement> settleEntityBatch(ayt::entity::World&,std::uint64_t,std::span<const ayt::entity::EntityHandle>,std::span<const std::uint64_t>,std::span<const std::uint64_t>,std::span<const SettlementRequest>,BatchSemantics,ayt::task::ITaskScheduler*,SettlementMetrics*);
};
#undef AY_CURRENT_CLASS
#define AY_CURRENT_CLASS StatsRecipeComponent
/// Scene-owned reference to the World recipe asset. One active owner per World.
class StatsRecipeComponent final : public ayt::entity::IComponent {
public:
    const char* getName() const override {return "StatsRecipes";}
    AY_PROPERTY(std::string,assetPath,ayt::entity::kAttrSerialize)
    void onAttach(ayt::entity::Entity* owner) override {_owner=owner;}
    void onDetach() override;
    void onStart() override;
    Result<void> refresh();
    const std::optional<Diagnostic>& lastError() const noexcept {return _error;}
private:
    ayt::entity::Entity* _owner=nullptr;
    std::optional<Diagnostic> _error;
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
    /// Weak registration follows observer ownership; replacing public callbacks cannot disconnect it.
    Result<void> observeState(const std::shared_ptr<StateObserver>& observer);
    Result<void> preparePublication(ayt::entity::EntityHandle, const StatsComponent&, const Schema&, PublicationReason, std::size_t publications=1);
    void publish(ayt::entity::EntityHandle, const StatsComponent&, PublicationReason) noexcept;
    /// Immutable World-level recipe configuration. An active StatsRecipeComponent owns
    /// replacement until it releases its asset; manual replacement is rejected meanwhile.
    Result<void> setRecipes(std::shared_ptr<const RecipeBook> recipes);
    /// Load/validate a complete loose recipe asset before replacing the active book.
    /// Invalid or missing files keep the previous book and all live states intact.
    Result<void> loadRecipes(const std::filesystem::path& path,const Schema& targetSchema,
                             const Schema* sourceSchema=nullptr,Extensions extensions={});
    /// Resolve a cooked .sts asset through AYResource (loose or DB/Pak).
    /// Replacement is atomic and rejected during publication/calculation.
    Result<void> bindRecipeAsset(ayt::entity::EntityHandle owner,std::string_view path);
    void releaseRecipeAsset(ayt::entity::EntityHandle owner);
    struct PreparedCalculation {
        std::shared_ptr<const RecipeBook> owner;
        /// Owns the direct plan or aliases the immutable book; survives recipe/World replacement.
        std::shared_ptr<const CalculationPlan> retainedPlan;
        const CalculationPlan* plan=nullptr;
        std::vector<Value> inputs;
        CalculationContext context;
    };
    /// Owner-side pure snapshot; retained input/plan ownership is not delayed commit authorization.
    Result<PreparedCalculation> prepareCalculation(ayt::entity::EntityHandle target, StatId stat,
        AttributeOp operation, Value amount, std::string_view recipe={},
        std::optional<ayt::entity::EntityHandle> source={});
    /// Gameplay entry: selects recipe, samples inputs and commits through the current component.
    /// No permission/damage provenance is implied. Pool writes current; scalar writes base.
    Result<Settlement> change(ayt::entity::EntityHandle target, std::string_view stat, AttributeOp operation,
        Value amount, std::string_view recipe={}, std::optional<ayt::entity::EntityHandle> source={});
    Result<Settlement> increase(ayt::entity::EntityHandle target, std::string_view stat, Value amount, std::string_view recipe={}, std::optional<ayt::entity::EntityHandle> source={}) {
        return change(target,stat,AttributeOp::Increase,amount,recipe,source);
    }
    Result<Settlement> decrease(ayt::entity::EntityHandle target, std::string_view stat, Value amount, std::string_view recipe={}, std::optional<ayt::entity::EntityHandle> source={}) {
        return change(target,stat,AttributeOp::Decrease,amount,recipe,source);
    }
    Result<Settlement> set(ayt::entity::EntityHandle target, std::string_view stat, Value amount, std::string_view recipe={}, std::optional<ayt::entity::EntityHandle> source={}) {
        return change(target,stat,AttributeOp::Set,amount,recipe,source);
    }
    struct AttributeAction {
        std::uint64_t id=0;
        ayt::entity::EntityHandle target;
        std::string stat;
        AttributeOp operation=AttributeOp::Decrease;
        Value amount=0;
        std::string recipe;
        std::optional<ayt::entity::EntityHandle> source;
    };
    /// Atomic named actions; all handles/generations/versions are captured on the World owner.
    /// Ordered matches consecutive single actions; Snapshot explicitly samples the pre-batch bank.
    /// IDs reject duplicates within this call, not retries across calls; use expected-version
    /// settleEntityBatch for delayed work. Callbacks see the entire published bank.
    Result<BatchSettlement> changeBatch(std::span<const AttributeAction> actions,
        BatchSemantics semantics=BatchSemantics::Ordered, ayt::task::ITaskScheduler* scheduler=nullptr);
private:
    ayt::entity::World* _world=nullptr;
    std::uint64_t _token=0,_tick=0;
    std::uint64_t _calculations=0;
    bool _publishing=false,_changing=false;
    std::vector<std::weak_ptr<StateObserver>> _observers;
    std::shared_ptr<const RecipeBook> _recipes;
    ayt::entity::EntityHandle _recipeOwner;
    std::string _recipeAssetPath;
    std::weak_ptr<const RecipeAsset> _recipeSnapshot;
    std::shared_ptr<const CalculationPlan> _direct;
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
/// Optional profiling includes identity/preflight, transport/observers and gameplay callbacks.
Result<BatchSettlement> settleEntityBatch(ayt::entity::World& world,std::uint64_t worldToken,
    std::span<const ayt::entity::EntityHandle> entities,std::span<const std::uint64_t> componentGenerations,
    std::span<const std::uint64_t> expectedVersions,std::span<const SettlementRequest> requests,
    BatchSemantics semantics,ayt::task::ITaskScheduler* scheduler=nullptr,SettlementMetrics* metrics=nullptr);
/// Explicit legacy conversion helpers; callers select one authority rather than two mutable mirrors.
Result<StatSet> importLegacyHealth(const ayt::entity::HealthComponent& legacy,std::string name="health");
Result<void> exportLegacyHealth(const StatSet& state,StatId resource,ayt::entity::HealthComponent& legacy);
} // namespace ayt::stats
