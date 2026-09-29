#include <AYStats/SceneIntegration.h>
#include <AYApplication/EngineModuleContext.h>
namespace ayt::stats {
StatsHostIntegrationModule::StatsHostIntegrationModule():_descriptor{.id="AYStats.HostIntegration",.displayName="Stats Host Scene lifecycle",.version="0.1.0",.dependencies={ayt::module::ModuleDependency::required("AYStats.Integration")}}{}
ayt::module::ModuleResult StatsHostIntegrationModule::install(ayt::module::IModuleContext& context) {
    auto* engine=dynamic_cast<ayt::app::EngineModuleContext*>(&context);auto* scenes=engine?engine->host().scenes():nullptr;
    if(!engine || !scenes)return ayt::module::ModuleResult::failure(ayt::module::ModuleErrorCode::TypeRegistrationFailed,"Stats Host integration requires EngineModuleContext Scene service");
    auto result=_binding.bind(engine->host().eventBus(),*scenes);
    return result?ayt::module::ModuleResult::success():ayt::module::ModuleResult::failure(ayt::module::ModuleErrorCode::TypeRegistrationFailed,result.error().message);
}
void StatsHostIntegrationModule::shutdown(ayt::module::IModuleContext&) noexcept {try{_binding.disconnect();}catch(...){}}
} // namespace ayt::stats
