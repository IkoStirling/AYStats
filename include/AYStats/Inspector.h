#pragma once
#include <AYStats/Authoring.h>
#include <filesystem>
#include <functional>
namespace ayt::ui {class Widget;}
namespace ayt::stats {
/// Creates a widget owned by the caller's UI tree. Publishes a validated old/new author pair.
/// Host must verify selected owner and unchanged old text, then use its undo command boundary.
ayt::ui::Widget* makeStatsInspector(std::string definitions,const StatSet* runtime,
    bool editable,std::function<void(std::string,std::string)> publish);
/// Project-asset recipe editor. Paths entered by the user are relative to assetsRoot;
/// the host callback must still verify its selected owner before replacing the file.
ayt::ui::Widget* makeStatsRecipeInspector(const std::filesystem::path& assetsRoot,
    std::shared_ptr<const Schema> targetSchema,bool editable,
    std::function<Result<void>(const std::filesystem::path&,std::string_view,std::string_view)> publish);
} // namespace ayt::stats
