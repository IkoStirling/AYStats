#pragma once
#include <AYStats/Authoring.h>
#include <functional>
namespace ayt::ui {class Widget;}
namespace ayt::stats {
/// Creates a widget owned by the caller's UI tree. Publishes a validated old/new author pair.
/// Host must verify selected owner and unchanged old text, then use its undo command boundary.
ayt::ui::Widget* makeStatsInspector(std::string definitions,const StatSet* runtime,
    bool editable,std::function<void(std::string,std::string)> publish);
} // namespace ayt::stats
