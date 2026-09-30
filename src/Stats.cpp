#include <AYStats/Stats.h>
#include <algorithm>
#include <functional>
#include <tuple>
#include <unordered_set>

namespace ayt::stats {
namespace {
auto fail(Error code, std::string message, std::string path = {}) { return std::unexpected(Diagnostic{code, std::move(message), std::move(path)}); }
std::uint64_t magnitude(Value v) { return v < 0 ? std::uint64_t(-(v + 1)) + 1 : std::uint64_t(v); }
bool sameIdentity(const Modifier& a, const Modifier& b) {
    return std::tie(a.effect,a.rule,a.hit,a.layer,a.target,a.stage) == std::tie(b.effect,b.rule,b.hit,b.layer,b.target,b.stage);
}
}
StatId statId(std::string_view name) noexcept {
    StatId hash = 14695981039346656037ull;
    for (unsigned char c : name) { hash ^= c; hash *= 1099511628211ull; }
    return hash;
}
Result<Value> add(Value a, Value b) {
    if ((b > 0 && a > std::numeric_limits<Value>::max()-b) || (b < 0 && a < std::numeric_limits<Value>::min()-b))
        return fail(Error::Overflow,"addition overflow");
    return a+b;
}
Result<Value> multiplyDivide(Value a, Value b, Value divisor) {
    if (!divisor) return fail(Error::DivideByZero,"division by zero");
    const bool negative = (a < 0) ^ (b < 0) ^ (divisor < 0);
    const auto x=magnitude(a), y=magnitude(b), d=magnitude(divisor);
    if(!x || !y)return Value(0);
    if(x<=UINT64_MAX/y) {
        const auto quotient=x*y/d;
        const auto limit=std::uint64_t(std::numeric_limits<Value>::max())+(negative?1ull:0ull);
        if(quotient>limit)return fail(Error::Overflow,"signed quotient overflow");
        if(negative && quotient==(1ull<<63))return std::numeric_limits<Value>::min();
        return negative?-Value(quotient):Value(quotient);
    }
    // Portable unsigned 64x64 -> 128 product, followed by long division.
    const auto x0=x&0xffffffffull, x1=x>>32, y0=y&0xffffffffull, y1=y>>32;
    const auto p0=x0*y0, p1=x1*y0, p2=x0*y1, p3=x1*y1;
    const auto middle=(p0>>32)+(p1&0xffffffffull)+(p2&0xffffffffull);
    const std::uint64_t low=(middle<<32)|(p0&0xffffffffull);
    const std::uint64_t high=p3+(p1>>32)+(p2>>32)+(middle>>32);
    if (high >= d) return fail(Error::Overflow,"product quotient exceeds 64 bits");
    std::uint64_t remainder=high, quotient=0;
    for(int bit=63;bit>=0;--bit) {
        const bool carry=(remainder>>63)!=0;
        remainder=(remainder<<1)|((low>>bit)&1ull);
        if(carry || remainder>=d) { remainder-=d; quotient|=1ull<<bit; }
    }
    const auto limit=std::uint64_t(std::numeric_limits<Value>::max())+(negative ? 1ull : 0ull);
    if(quotient>limit) return fail(Error::Overflow,"signed quotient overflow");
    if(negative && quotient==limit && limit==(1ull<<63)) return std::numeric_limits<Value>::min();
    return negative ? -Value(quotient) : Value(quotient);
}
std::optional<std::size_t> Schema::index(StatId id) const noexcept {
    const auto it=std::lower_bound(_definitions.begin(),_definitions.end(),id,[](const Definition& d,StatId key){return d.id<key;});
    if(it==_definitions.end() || it->id!=id) return {};
    return std::size_t(it-_definitions.begin());
}
Result<std::shared_ptr<const Schema>> Schema::compile(std::vector<Definition> defs) {
    if(defs.size()>65536) return fail(Error::Limit,"definition limit exceeded");
    for(auto& d:defs) {
        if(d.shape!=Shape::Scalar && d.shape!=Shape::Pool)return fail(Error::Invalid,"unknown shape",d.name);
        if(d.mode!=Mode::Basic && d.mode!=Mode::Advanced)return fail(Error::Invalid,"unknown mode",d.name);
        if(d.lifetime!=Lifetime::Persistent && d.lifetime!=Lifetime::Intermediate)return fail(Error::Invalid,"unknown lifetime",d.name);
        if(d.capacityPolicy<CapacityPolicy::KeepCurrent || d.capacityPolicy>CapacityPolicy::KeepMissing)return fail(Error::Invalid,"unknown capacity policy",d.name);
        if(d.name.empty() || d.name.size()>256) return fail(Error::Invalid,"invalid definition name",d.name);
        const auto computed=statId(d.name);
        if(d.id && d.id!=computed) return fail(Error::Invalid,"id must match stable name",d.name);
        d.id=computed;
        if(d.minimum<0 || d.maximum<d.minimum || d.initial<d.minimum || d.initial>d.maximum || d.derivedFactor<0)
            return fail(Error::Invalid,"invalid range, initial value or derived factor",d.name);
        if(d.shape==Shape::Pool && (d.minimum!=0 || d.current<0 || d.capacity<0 || d.current>d.capacity || d.capacity>d.maximum))
            return fail(Error::Invalid,"pool requires zero minimum and valid current/capacity",d.name);
        if(d.shape!=Shape::Pool && d.capacityFrom) return fail(Error::Invalid,"capacity reference requires pool",d.name);
        if(d.mode==Mode::Basic && (!d.zones.empty() || d.baseFrom || d.capacityFrom))
            return fail(Error::Invalid,"basic mode cannot use derived references or modifiers",d.name);
        std::unordered_set<std::uint32_t> zones;
        if(d.zones.size()>256)return fail(Error::Limit,"zone count limit",d.name);
        for(const auto& z:d.zones) {if(z.op<ZoneOp::Flat || z.op>ZoneOp::Override)return fail(Error::Invalid,"unknown zone operation",d.name);if(!zones.insert(z.id).second) return fail(Error::Duplicate,"duplicate zone",d.name);}
    }
    std::sort(defs.begin(),defs.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    for(std::size_t i=1;i<defs.size();++i) if(defs[i-1].id==defs[i].id) return fail(Error::Duplicate,"duplicate name or identity collision",defs[i].name);
    auto schema=std::shared_ptr<Schema>(new Schema);
    schema->_definitions=std::move(defs);
    std::vector<std::uint8_t> seen(schema->_definitions.size());
    std::vector<std::string> path;
    std::function<Result<void>(std::size_t)> visit=[&](std::size_t i)->Result<void>{
        if(seen[i]==2) return {};
        if(seen[i]==1) { std::string chain; for(const auto& p:path) chain+=p+" -> "; return fail(Error::Cycle,"dependency cycle",chain+schema->_definitions[i].name); }
        if(path.size()>=256)return fail(Error::Limit,"dependency depth limit",schema->_definitions[i].name);
        seen[i]=1; path.push_back(schema->_definitions[i].name);
        const auto& d=schema->_definitions[i];
        for(auto ref:{d.baseFrom,d.capacityFrom}) if(ref) {
            const auto index=schema->index(ref);
            if(!index) return fail(Error::Missing,"missing dependency",d.name);
            if(schema->_definitions[*index].shape!=Shape::Scalar || schema->_definitions[*index].lifetime!=d.lifetime)
                return fail(Error::Invalid,"dependency must be scalar with same lifetime",d.name);
            if(auto r=visit(*index); !r) return r;
        }
        path.pop_back(); seen[i]=2; return {};
    };
    for(std::size_t i=0;i<seen.size();++i) if(auto r=visit(i);!r) return std::unexpected(r.error());
    return std::shared_ptr<const Schema>(schema);
}
Result<StatSet> StatSet::create(std::shared_ptr<const Schema> schema) {
    if(!schema) return fail(Error::Invalid,"null schema");
    StatSet set; set._schema=std::move(schema);
    for(const auto& d:set._schema->definitions()) set._rows.push_back({d.initial,d.current,d.capacity,d.initial});
    if(auto r=set.rebuild();!r) return std::unexpected(r.error());
    return set;
}
Result<Row> StatSet::read(StatId id) const {
    const auto i=_schema->index(id);
    if(!i) return fail(Error::Missing,"unknown stat",std::to_string(id));
    return _rows[*i];
}
Result<Value> StatSet::evaluate(std::size_t i,std::vector<std::uint8_t>& seen) {
    if(seen[i]==2) return _rows[i].effective;
    if(seen[i]==1) return fail(Error::Cycle,"runtime dependency cycle");
    seen[i]=1; const auto& d=_schema->definitions()[i];
    Value v=_rows[i].base;
    if(d.baseFrom) {
        auto source=evaluate(*_schema->index(d.baseFrom),seen); if(!source) return source;
        auto product=multiplyDivide(*source,d.derivedFactor,RateScale); if(!product) return product;
        auto sum=add(*product,d.derivedAdd); if(!sum) return sum; v=*sum;
    }
    for(const auto& z:d.zones) {
        Value aggregate=0; bool found=false; const Modifier* selected=nullptr;
        for(const auto& m:_modifiers) if(m.target==d.id && m.zone==z.id) {
            found=true;
            if(z.op==ZoneOp::Flat || z.op==ZoneOp::PercentSum) { auto n=add(aggregate,m.value); if(!n) return n; aggregate=*n; }
            else if(z.op==ZoneOp::FactorProduct) { auto n=multiplyDivide(v,m.value,RateScale); if(!n) return n; v=*n; }
            else if(z.op==ZoneOp::Minimum) { if(!selected || m.value<aggregate) {aggregate=m.value;selected=&m;} }
            else if(z.op==ZoneOp::Maximum) { if(!selected || m.value>aggregate) {aggregate=m.value;selected=&m;} }
            else if(!selected || m.priority>selected->priority) selected=&m;
        }
        if(!found) continue;
        if(z.op==ZoneOp::Flat) {auto n=add(v,aggregate);if(!n)return n;v=*n;}
        if(z.op==ZoneOp::PercentSum) {auto rate=add(RateScale,aggregate);if(!rate)return rate;if(*rate<0)return fail(Error::Invalid,"negative percentage zone",d.name);auto n=multiplyDivide(v,*rate,RateScale);if(!n)return n;v=*n;}
        if(z.op==ZoneOp::Minimum) v=std::min(v,aggregate);
        if(z.op==ZoneOp::Maximum) v=std::max(v,aggregate);
        if(z.op==ZoneOp::Override) v=selected->value;
    }
    _rows[i].effective=std::clamp(v,d.minimum,d.maximum);
    if(d.shape==Shape::Pool && d.capacityFrom) {
        auto cap=evaluate(*_schema->index(d.capacityFrom),seen);if(!cap)return cap;
        if(*cap<0 || *cap>d.maximum) return fail(Error::Invalid,"derived capacity out of range",d.name);
        const auto oldCap=_rows[i].capacity, oldCurrent=_rows[i].current;
        Value current=oldCurrent;
        if(d.capacityPolicy==CapacityPolicy::KeepRatio && oldCap>0) {auto n=multiplyDivide(oldCurrent,*cap,oldCap);if(!n)return n;current=*n;}
        if(d.capacityPolicy==CapacityPolicy::KeepMissing) {auto n=add(*cap,-(oldCap-oldCurrent));if(!n)return n;current=*n;}
        _rows[i].capacity=*cap; _rows[i].current=std::clamp(current,Value(0),*cap);
    }
    seen[i]=2; return _rows[i].effective;
}
Result<void> StatSet::rebuild() {
    std::vector<std::uint8_t> visited;
    return rebuild(visited);
}
Result<void> StatSet::rebuild(std::vector<std::uint8_t>& visited) {
    std::sort(_modifiers.begin(),_modifiers.end(),[](const auto& a,const auto& b){return std::tie(a.target,a.stage,a.zone,a.effect,a.rule,a.hit,a.layer,a.handle)<std::tie(b.target,b.stage,b.zone,b.effect,b.rule,b.hit,b.layer,b.handle);});
    visited.assign(_rows.size(),0);
    for(std::size_t i=0;i<_rows.size();++i) if(auto r=evaluate(i,visited);!r) return std::unexpected(r.error());
    return {};
}
Result<void> StatSet::validateModifier(const Modifier& m,std::optional<std::uint64_t> replacing) const {
    const auto i=_schema->index(m.target);
    if(!i) return fail(Error::Missing,"modifier target missing");
    const auto& d=_schema->definitions()[*i];
    if(!m.handle || !m.effect || !m.rule || d.mode!=Mode::Advanced || d.shape!=Shape::Scalar || m.stage!=d.stage)
        return fail(Error::Invalid,"invalid modifier identity, mode, shape or stage",d.name);
    const auto zone=std::find_if(d.zones.begin(),d.zones.end(),[&](const auto& z){return z.id==m.zone;});
    if(zone==d.zones.end()) return fail(Error::Missing,"modifier zone missing",d.name);
    if(zone->op==ZoneOp::FactorProduct && m.value<0) return fail(Error::Invalid,"negative factor",d.name);
    for(const auto& old:_modifiers) if(!replacing || old.handle!=*replacing)
        if(old.handle==m.handle || sameIdentity(old,m)) return fail(Error::Duplicate,"duplicate modifier handle/application identity",d.name);
    return {};
}
Result<void> StatSet::insert(Modifier m) {
    if(_modifiers.size()>=65536)return fail(Error::Limit,"modifier count limit");
    if(auto r=validateModifier(m);!r)return r;
    auto next=*this;next._modifiers.push_back(m);if(auto r=next.rebuild();!r)return r;
    if(_version==UINT64_MAX)return fail(Error::Overflow,"state version exhausted");
    next._version++;*this=std::move(next);return {};
}
Result<void> StatSet::erase(std::uint64_t handle) {
    auto next=*this;const auto it=std::find_if(next._modifiers.begin(),next._modifiers.end(),[&](const auto& m){return m.handle==handle;});
    if(it==next._modifiers.end())return fail(Error::Missing,"modifier handle missing");
    next._modifiers.erase(it);if(auto r=next.rebuild();!r)return r;
    if(_version==UINT64_MAX)return fail(Error::Overflow,"state version exhausted");
    next._version++;*this=std::move(next);return {};
}
Result<void> StatSet::replace(Modifier m) {
    if(auto r=validateModifier(m,m.handle);!r)return r;
    auto next=*this;auto it=std::find_if(next._modifiers.begin(),next._modifiers.end(),[&](const auto& old){return old.handle==m.handle;});
    if(it==next._modifiers.end())return fail(Error::Missing,"modifier handle missing");
    *it=m;if(auto r=next.rebuild();!r)return r;
    if(_version==UINT64_MAX)return fail(Error::Overflow,"state version exhausted");
    next._version++;*this=std::move(next);return {};
}
Result<void> StatSet::replaceOwnedModifiers(std::span<const Modifier> owned,std::span<const Modifier> modifiers) {
    if(owned.empty() && modifiers.empty())return {};
    if(modifiers.size()>65536 || owned.size()>65536)return fail(Error::Limit,"effect replacement modifier limit");
    std::vector<std::uint64_t> handles;handles.reserve(owned.size());
    for(const auto& previous:owned) {
        auto found=std::find_if(_modifiers.begin(),_modifiers.end(),[&](const auto& current){return current.handle==previous.handle;});
        if(found==_modifiers.end() || *found!=previous)return fail(Error::Stale,"effect contribution differs from owned snapshot");
        handles.push_back(previous.handle);
    }
    std::sort(handles.begin(),handles.end());
    if(std::adjacent_find(handles.begin(),handles.end())!=handles.end())return fail(Error::Duplicate,"duplicate owned modifier handle");
    auto next=*this;
    std::erase_if(next._modifiers,[&](const auto& m){return std::binary_search(handles.begin(),handles.end(),m.handle);});
    if(modifiers.size()>65536-next._modifiers.size())return fail(Error::Limit,"modifier count limit");
    for(const auto& modifier:modifiers) {
        if(auto result=next.validateModifier(modifier);!result)return result;
        next._modifiers.push_back(modifier);
    }
    if(auto result=next.rebuild();!result)return result;
    if(_version==UINT64_MAX)return fail(Error::Overflow,"state version exhausted");
    ++next._version;*this=std::move(next);return {};
}
Result<Settlement> StatSet::commit(std::span<const Mutation> ops,std::uint64_t expected) {
    if(expected!=_version)return fail(Error::Stale,"state version changed");
    if(ops.size()>65536)return fail(Error::Limit,"transaction operation limit");
    if(_version==UINT64_MAX)return fail(Error::Overflow,"state version exhausted");
    auto next=*this;std::vector<std::uint8_t> scratch;
    auto result=next.commitShadow(ops,expected,scratch);
    if(result)*this=std::move(next);
    return result;
}
Result<Settlement> StatSet::commitShadow(std::span<const Mutation> ops,std::uint64_t expected,std::vector<std::uint8_t>& scratch) {
    if(expected!=_version)return fail(Error::Stale,"state version changed");
    if(ops.size()>65536)return fail(Error::Limit,"transaction operation limit");
    if(_version==UINT64_MAX)return fail(Error::Overflow,"state version exhausted");
    Settlement result{_version,_version,{}};
    result.changes.reserve(ops.size()+std::count_if(ops.begin(),ops.end(),[](const auto& op){return op.op==MutationOp::SetCapacity;}));
    for(const auto& op:ops) {
        if(op.op<MutationOp::SetBase || op.op>MutationOp::Restore)return fail(Error::Invalid,"unknown mutation operation");
        const auto i=_schema->index(op.target);if(!i)return fail(Error::Missing,"mutation target missing");
        const auto& d=_schema->definitions()[*i];auto& row=_rows[*i];
        if(d.lifetime!=Lifetime::Persistent)return fail(Error::Invalid,"cannot commit intermediate value",d.name);
        if(op.op==MutationOp::SetBase) {
            if(d.baseFrom || op.amount<d.minimum || op.amount>d.maximum)return fail(Error::Invalid,"invalid base write",d.name);
            const auto beforeBase=row.base;row.base=op.amount;if(auto r=rebuild(scratch);!r)return std::unexpected(r.error());
            Result<Value> actual=beforeBase==INT64_MIN ?
                (row.base>=0?Result<Value>(fail(Error::Overflow,"base delta overflow",d.name)):Result<Value>(row.base-INT64_MIN)):
                add(row.base,-beforeBase);
            if(!actual)return std::unexpected(actual.error());
            result.changes.push_back({op.target,beforeBase,row.base,*actual,0,ChangeField::Base});continue;
        }
        if(d.shape!=Shape::Pool)return fail(Error::Invalid,"resource operation requires pool",d.name);
        const auto before=row.current,capacityBefore=row.capacity;Value excess=0;
        switch(op.op) {
        case MutationOp::SetCurrent:
            if(op.amount<0 || op.amount>row.capacity)return fail(Error::Invalid,"current outside capacity",d.name);
            row.current=op.amount;break;
        case MutationOp::Increase:
            if(op.amount<0)return fail(Error::Invalid,"negative increase",d.name);
            {const auto actual=std::min(op.amount,row.capacity-row.current);row.current+=actual;excess=op.amount-actual;}break;
        case MutationOp::Decrease:
            if(op.amount<0)return fail(Error::Invalid,"negative decrease",d.name);
            {const auto actual=std::min(op.amount,row.current);row.current-=actual;excess=op.amount-actual;}break;
        case MutationOp::Restore: row.current=row.capacity;break;
        case MutationOp::SetCapacity: {
            if(d.capacityFrom || op.amount<0 || op.amount>d.maximum)return fail(Error::Invalid,"invalid direct capacity write",d.name);
            auto current=row.current;
            if(d.capacityPolicy==CapacityPolicy::KeepRatio && row.capacity>0) {auto n=multiplyDivide(current,op.amount,row.capacity);if(!n)return std::unexpected(n.error());current=*n;}
            if(d.capacityPolicy==CapacityPolicy::KeepMissing) {auto n=add(op.amount,-(row.capacity-current));if(!n)return std::unexpected(n.error());current=*n;}
            row.capacity=op.amount;row.current=std::clamp(current,Value(0),row.capacity);break;
        }
        default:break;
        }
        result.changes.push_back({op.target,before,row.current,row.current-before,excess});
        if(op.op==MutationOp::SetCapacity)result.changes.push_back({op.target,capacityBefore,row.capacity,row.capacity-capacityBefore,0,ChangeField::Capacity});
    }
    if(ops.empty())return result;
    _version++;result.afterVersion=_version;return result;
}
Result<StatSet> StatSet::restore(std::shared_ptr<const Schema> schema,std::span<const Row> rows,std::span<const Modifier> mods,std::uint64_t version) {
    if(mods.size()>65536)return fail(Error::Limit,"saved modifier count limit");
    auto set=create(std::move(schema));if(!set)return set;
    if(rows.size()!=set->_rows.size() || !version || version==UINT64_MAX)return fail(Error::Invalid,"invalid restore rows/version");
    for(std::size_t i=0;i<rows.size();++i) {
        const auto& d=set->_schema->definitions()[i];const auto& r=rows[i];
        if(r.base<d.minimum || r.base>d.maximum || r.capacity<0 || r.capacity>d.maximum || r.current<0 || r.current>r.capacity)
            return fail(Error::Invalid,"invalid saved row",d.name);
        set->_rows[i]=r;
    }
    for(const auto& m:mods) {if(auto r=set->validateModifier(m);!r)return std::unexpected(r.error());set->_modifiers.push_back(m);}
    if(auto r=set->rebuild();!r)return std::unexpected(r.error());
    for(std::size_t i=0;i<rows.size();++i)if(set->_schema->definitions()[i].capacityFrom && rows[i].capacity!=set->_rows[i].capacity)return fail(Error::Invalid,"saved derived capacity mismatch");
    set->_version=version;return set;
}
} // namespace ayt::stats
