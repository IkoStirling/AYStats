#pragma once
#include <AYStats/AssetIO.h>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace ayt::stats {

/// Self-contained runtime asset; schemas and the immutable book share one versioned payload.
struct RecipeAsset {
    std::shared_ptr<const Schema> targetSchema;
    std::shared_ptr<const Schema> sourceSchema;
    std::shared_ptr<const RecipeBook> book;
};

/// Assets-relative author manifest and its validated source closure.
struct RecipeAssetSource {
    std::filesystem::path manifest;
    std::filesystem::path targetDefinitions;
    std::filesystem::path sourceDefinitions;
    std::filesystem::path recipeBook;
    std::string logicalPath;
    RecipeAsset asset;
};

/// Inspect a .srs manifest and every dependency before cooking.
/// References must be inside assetsRoot and source files must all exist.
Result<RecipeAssetSource> inspectRecipeAssetSource(const std::filesystem::path& manifest,
                                                   const std::filesystem::path& assetsRoot);
/// Validate a proposed manifest before writing it. The manifest path may not exist yet.
Result<RecipeAssetSource> inspectRecipeAssetSourceText(const std::filesystem::path& manifest,
    const std::filesystem::path& assetsRoot,std::string_view text);
/// Create/update a source manifest with optimistic disk checking and cook its loose runtime asset.
/// An existing manifest must be loaded first and supplied as expectedText.
Result<RecipeAssetSource> saveRecipeAssetSource(const std::filesystem::path& manifest,
    const std::filesystem::path& assetsRoot,std::string_view targetDefinitions,
    std::string_view sourceDefinitions,std::string_view recipeBook,
    std::optional<std::string_view> expectedText=std::nullopt);
/// Rebuild one self-contained .sts next to its manifest, replacing only after validation.
Result<std::filesystem::path> cookRecipeAssetSource(const std::filesystem::path& manifest,
    const std::filesystem::path& assetsRoot);
/// Save one authored recipe book and rebuild every referring .sts in Assets.
/// Returns cooked paths for AYResource hot reload at the next World Sim boundary.
Result<std::vector<std::filesystem::path>> saveRecipeBookAndRecook(
    const std::filesystem::path& recipePath,const std::filesystem::path& assetsRoot,
    std::string_view expectedText,std::string_view replacement);
/// Cook the self-contained .sts bytes; no filesystem access occurs during decode.
Result<std::string> encodeRecipeAsset(const RecipeAsset& asset);
Result<RecipeAsset> decodeRecipeAsset(std::string_view bytes);

} // namespace ayt::stats
