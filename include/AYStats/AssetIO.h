#pragma once
#include <AYStats/Effects.h>
#include <filesystem>
namespace ayt::stats {
/// Versioned definition document. Does not serialize implementation callbacks or compact indices.
Result<std::string> encodeDefinitions(const Schema& schema);
Result<std::shared_ptr<const Schema>> decodeDefinitions(std::string_view text);
/// Versioned pure plan; trusted extension implementations are supplied on load by stable id.
Result<std::string> encodePlan(const CalculationPlan& plan);
Result<CalculationPlan> decodePlan(std::string_view text,Extensions extensions={});
/// Runtime checkpoint; derived fields rebuild. Default limits cap untrusted text and arrays.
Result<std::string> encodeState(const StatSet& state);
Result<StatSet> decodeState(std::string_view text,std::shared_ptr<const Schema> expectedSchema);
/// Save a complete runtime (state plus timed effect ownership/tick and caller random domain).
struct RuntimeCheckpoint { StatSet state; EffectManager effects; CalculationContext context; };
Result<std::string> encodeCheckpoint(const RuntimeCheckpoint& checkpoint);
Result<RuntimeCheckpoint> decodeCheckpoint(std::string_view text,std::shared_ptr<const Schema> expectedSchema);
/// Atomic replacement with a sibling temporary file; failed writes preserve the existing file.
Result<void> writeTextAtomic(const std::filesystem::path& path,std::string_view text);
Result<std::string> readText(const std::filesystem::path& path,std::size_t maxBytes=16*1024*1024);
} // namespace ayt::stats
