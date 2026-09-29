#include <AYStats/Effects.h>
#include <algorithm>
#include <tuple>

namespace ayt::stats {
namespace {auto fail(Error e,std::string m){return std::unexpected(Diagnostic{e,std::move(m),{}});}}
Result<void> EffectManager::apply(StatSet& set,Effect effect,EffectPolicy policy,std::uint64_t tick) {
    if(policy<EffectPolicy::Independent || policy>EffectPolicy::Stack)return fail(Error::Invalid,"unknown effect policy");
    if(tick<_tick || !effect.id || !effect.key || !effect.maxStacks || effect.modifiers.empty() || effect.expires<=tick)
        return fail(Error::Invalid,"invalid effect identity, tick or lifetime");
    if(std::any_of(_effects.begin(),_effects.end(),[&](const auto& e){return e.id==effect.id && !(policy==EffectPolicy::Refresh && e.key==effect.key);}))return fail(Error::Duplicate,"effect id exists");
    auto nextSet=set;auto next=*this;
    if(auto r=next.advance(nextSet,tick);!r)return r;
    auto match=std::find_if(next._effects.begin(),next._effects.end(),[&](const auto& e){return e.key==effect.key;});
    if(policy==EffectPolicy::Refresh) {
        auto byId=std::find_if(next._effects.begin(),next._effects.end(),[&](const auto& e){return e.id==effect.id && e.key==effect.key;});
        if(byId!=next._effects.end())match=byId;
        else if(std::count_if(next._effects.begin(),next._effects.end(),[&](const auto& e){return e.key==effect.key;})>1)return fail(Error::Invalid,"refresh key matches multiple instances; supply the existing instance id");
    }
    if(policy==EffectPolicy::Refresh && match!=next._effects.end()) {match->expires=effect.expires;*this=std::move(next);set=std::move(nextSet);return {};}
    if(policy==EffectPolicy::Replace) {
        for(auto it=next._effects.begin();it!=next._effects.end();) {
            if(it->key!=effect.key){++it;continue;}
            for(const auto& m:it->modifiers)if(auto r=nextSet.erase(m.handle);!r)return r;
            it=next._effects.erase(it);
        }
    }
    if(policy==EffectPolicy::Stack) {
        if(match!=next._effects.end() && match->maxStacks!=effect.maxStacks)return fail(Error::Invalid,"stack limit differs from active group");
        const auto count=std::count_if(next._effects.begin(),next._effects.end(),[&](const auto& e){return e.key==effect.key;});
        if(std::size_t(count)>=effect.maxStacks)return fail(Error::Limit,"effect stack limit");
    }
    if(next._effects.size()>=65536)return fail(Error::Limit,"effect count limit");
    for(auto& m:effect.modifiers) {m.effect=effect.id;if(auto r=nextSet.insert(m);!r)return r;}
    next._effects.push_back(std::move(effect));next._tick=tick;set=std::move(nextSet);*this=std::move(next);return {};
}
Result<void> EffectManager::advance(StatSet& set,std::uint64_t tick) {
    if(tick<_tick)return fail(Error::Invalid,"simulation tick cannot move backwards");
    if(std::none_of(_effects.begin(),_effects.end(),[&](const auto& e){return e.expires<=tick;})){_tick=tick;return {};}
    auto nextSet=set;auto next=*this;
    std::sort(next._effects.begin(),next._effects.end(),[](const auto& a,const auto& b){return std::tie(a.expires,a.id)<std::tie(b.expires,b.id);});
    for(auto it=next._effects.begin();it!=next._effects.end();) {
        if(it->expires>tick){++it;continue;}
        for(const auto& m:it->modifiers)if(auto r=nextSet.erase(m.handle);!r)return r;
        it=next._effects.erase(it);
    }
    next._tick=tick;set=std::move(nextSet);*this=std::move(next);return {};
}
Result<void> EffectManager::remove(StatSet& set,std::uint64_t id) {
    auto nextSet=set;auto next=*this;
    auto it=std::find_if(next._effects.begin(),next._effects.end(),[&](const auto& e){return e.id==id;});
    if(it==next._effects.end())return fail(Error::Missing,"effect id missing");
    for(const auto& m:it->modifiers)if(auto r=nextSet.erase(m.handle);!r)return r;
    next._effects.erase(it);set=std::move(nextSet);*this=std::move(next);return {};
}
Result<void> ReactionQueue::begin() {
    if(!_pending.empty())return fail(Error::Reentrant,"pending reactions cannot be reset");
    _derived=0;_ids.clear();return {};
}
Result<void> ReactionQueue::derive(const CalculationContext& parent,CalculationContext child,bool permitted) {
    if(!permitted)return fail(Error::Invalid,"trigger relation denied");
    if(parent.depth>=_maxDepth || _derived>=_limit)return fail(Error::Limit,"reaction depth/count limit");
    if(!child.calculation || child.calculation==parent.calculation || std::find(_ids.begin(),_ids.end(),child.calculation)!=_ids.end())return fail(Error::Duplicate,"reaction calculation identity reused");
    child.parent=parent.calculation;child.depth=parent.depth+1;child.tick=parent.tick;
    _pending.push_back(child);_ids.push_back(child.calculation);++_derived;return {};
}
std::optional<CalculationContext> ReactionQueue::pop() {if(_pending.empty())return {};auto c=_pending.front();_pending.pop_front();return c;}
Result<EffectManager> EffectManager::restore(const StatSet& set,std::vector<Effect> effects,std::uint64_t tick) {
    if(effects.size()>65536)return fail(Error::Limit,"saved effect limit");
    std::vector<std::uint64_t> ids,handles;
    for(const auto& effect:effects) {
        if(!effect.id || !effect.key || !effect.maxStacks || effect.expires<=tick || effect.modifiers.empty())return fail(Error::Invalid,"invalid saved effect");
        ids.push_back(effect.id);
        for(const auto& m:effect.modifiers) {
            if(m.effect!=effect.id || std::find(set.modifiers().begin(),set.modifiers().end(),m)==set.modifiers().end())return fail(Error::Invalid,"effect ownership differs from saved modifier");
            handles.push_back(m.handle);
        }
    }
    std::sort(ids.begin(),ids.end());std::sort(handles.begin(),handles.end());
    if(std::adjacent_find(ids.begin(),ids.end())!=ids.end() || std::adjacent_find(handles.begin(),handles.end())!=handles.end())return fail(Error::Duplicate,"duplicate saved effect ownership");
    EffectManager manager;manager._effects=std::move(effects);manager._tick=tick;return manager;
}
} // namespace ayt::stats
