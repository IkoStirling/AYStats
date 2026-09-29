#pragma once
#include <AYStats/AssetIO.h>
namespace ayt::stats {
/// Pure author field edit. Returns validated canonical definitions without touching live state.
/// Stable name/id and dependency changes require a full validated definition document.
Result<std::string> editDefinitionField(std::string_view text,StatId id,std::string_view field,std::string_view value);
/// Inspector preview uses an independent set, optionally with explicit modifier sources.
Result<StatSet> previewDefinitions(std::string_view text,std::span<const Modifier> modifiers={});
/// Validates and traces an authored plan using exact decimal CSV inputs, with bounded output.
Result<CalculationTrace> previewRecipe(std::string_view planText,std::string_view inputCsv,CalculationContext context={});
} // namespace ayt::stats
