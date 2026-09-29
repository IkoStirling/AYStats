#include <AYStats/Calculation.h>
#include <algorithm>
#include <unordered_set>

namespace ayt::stats {
namespace {
auto fail(Error e,std::string m,std::string path={}) {return std::unexpected(Diagnostic{e,std::move(m),std::move(path)});}
std::size_t operands(Op op) {
    switch(op) {
    case Op::Input:case Op::Constant:case Op::Random:return 0;
    case Op::Table:return 1;
    case Op::Clamp:case Op::Select:return 3;
    default:return 2;
    }
}
std::uint64_t mix(std::uint64_t x) {x+=0x9e3779b97f4a7c15ull;x=(x^(x>>30))*0xbf58476d1ce4e5b9ull;x=(x^(x>>27))*0x94d049bb133111ebull;return x^(x>>31);}
}
std::uint64_t randomBits(const CalculationContext& c,std::uint64_t purpose,std::uint64_t draw) noexcept {
    auto v=mix(c.seed);v=mix(v^c.tick);v=mix(v^c.calculation);v=mix(v^c.hit);v=mix(v^purpose);return mix(v^draw);
}
Result<CalculationPlan> CalculationPlan::compile(std::vector<Node> nodes,std::uint32_t output,std::size_t inputs,Extensions extensions) {
    if(nodes.empty() || nodes.size()>4096 || inputs>4096)return fail(Error::Limit,"plan node/input limit");
    std::unordered_map<std::uint32_t,std::size_t> ids;
    for(std::size_t i=0;i<nodes.size();++i) {
        const auto& n=nodes[i];
        if(!n.id || !ids.emplace(n.id,i).second)return fail(Error::Duplicate,"duplicate/zero node id",std::to_string(n.id));
        if(n.op==Op::Input && n.binding>=inputs)return fail(Error::Missing,"input binding outside range",std::to_string(n.id));
        if(n.op==Op::Random && n.value<=0)return fail(Error::Invalid,"random bound must be positive",std::to_string(n.id));
        if(n.op==Op::Table && (n.table.empty() || n.table.size()>65536))return fail(Error::Limit,"table length invalid",std::to_string(n.id));
        if(n.op==Op::Extension && (n.extension.empty() || !extensions.contains(n.extension) || !extensions.at(n.extension)))return fail(Error::Missing,"unregistered extension",n.extension);
    }
    if(!ids.contains(output))return fail(Error::Missing,"output node missing");
    std::vector<std::uint8_t> seen(nodes.size());std::vector<std::size_t> order,path;
    std::function<Result<void>(std::size_t)> visit=[&](std::size_t i)->Result<void>{
        if(seen[i]==2)return {};
        if(seen[i]==1){std::string cycle;for(auto p:path)cycle+=std::to_string(nodes[p].id)+" -> ";return fail(Error::Cycle,"calculation dependency cycle",cycle+std::to_string(nodes[i].id));}
        seen[i]=1;path.push_back(i);const auto& n=nodes[i];const std::uint32_t deps[]{n.a,n.b,n.c};
        for(std::size_t j=0;j<operands(n.op);++j) {
            if(!ids.contains(deps[j]))return fail(Error::Missing,"node operand missing",std::to_string(n.id));
            if(auto r=visit(ids.at(deps[j]));!r)return r;
        }
        path.pop_back();seen[i]=2;order.push_back(i);return {};
    };
    // Validate even unreachable author nodes; execute only the output dependency closure.
    for(std::size_t i=0;i<nodes.size();++i)if(auto r=visit(i);!r)return std::unexpected(r.error());
    std::fill(seen.begin(),seen.end(),std::uint8_t(0));order.clear();if(auto r=visit(ids.at(output));!r)return std::unexpected(r.error());
    CalculationPlan plan;plan._inputs=inputs;
    std::unordered_map<std::uint32_t,std::size_t> offsets;
    for(auto i:order) {
        auto n=std::move(nodes[i]);Instruction inst;inst.node=std::move(n);const auto& node=inst.node;
        if(operands(node.op)>0)inst.a=offsets.at(node.a);
        if(operands(node.op)>1)inst.b=offsets.at(node.b);
        if(operands(node.op)>2)inst.c=offsets.at(node.c);
        if(node.op==Op::Extension)plan._extensions.emplace(node.extension,extensions.at(node.extension));
        offsets.emplace(node.id,plan._instructions.size());plan._instructions.push_back(std::move(inst));
    }
    plan._output=offsets.at(output);return plan;
}
Result<Value> CalculationPlan::evaluate(std::span<const Value> inputs,const CalculationContext& context,std::span<Value> scratch) const {
    if(inputs.size()!=_inputs || scratch.size()<_instructions.size())return fail(Error::Invalid,"input/scratch length mismatch");
    for(std::size_t i=0;i<_instructions.size();++i) {
        const auto& inst=_instructions[i];const auto& n=inst.node;
        const auto a=operands(n.op)>0?scratch[inst.a]:0,b=operands(n.op)>1?scratch[inst.b]:0,c=operands(n.op)>2?scratch[inst.c]:0;
        Result<Value> value=Value(0);
        switch(n.op) {
        case Op::Input:value=inputs[n.binding];break;
        case Op::Constant:value=n.value;break;
        case Op::Add:value=add(a,b);break;
        case Op::Subtract:
            if(b==std::numeric_limits<Value>::min()) {
                if(a>=0)value=fail(Error::Overflow,"subtraction overflow");else value=Value(std::uint64_t(a)-std::uint64_t(b));
            } else value=add(a,-b);break;
        case Op::Multiply:value=multiplyDivide(a,b,1);break;
        case Op::Divide:value=multiplyDivide(a,1,b);break;
        case Op::MultiplyRate:value=multiplyDivide(a,b,RateScale);break;
        case Op::Minimum:value=std::min(a,b);break;
        case Op::Maximum:value=std::max(a,b);break;
        case Op::Clamp:if(b>c)value=fail(Error::Invalid,"clamp minimum exceeds maximum");else value=std::clamp(a,b,c);break;
        case Op::Greater:value=Value(a>b);break;
        case Op::Select:value=a?b:c;break;
        case Op::Table:if(a<0 || std::uint64_t(a)>=n.table.size())value=fail(Error::Invalid,"table index outside range");else value=n.table[std::size_t(a)];break;
        case Op::Random: {
            const auto bound=std::uint64_t(n.value), threshold=(0ull-bound)%bound;
            std::uint64_t draw=0,bits=0;do {bits=randomBits(context,n.binding,draw++);}while(bits<threshold);
            value=Value(bits%bound);break;
        }
        case Op::Extension: {
            const Value arguments[]{a,b};
            try {value=_extensions.at(n.extension)(arguments,context);}catch(const std::exception& e){value=fail(Error::Invalid,std::string("extension exception: ")+e.what());}catch(...){value=fail(Error::Invalid,"extension exception");}break;
        }
        default:value=fail(Error::Unsupported,"unknown instruction");break;
        }
        if(!value){auto error=value.error();error.path=std::to_string(n.id)+(error.path.empty()?"":"/"+error.path);return std::unexpected(std::move(error));}
        scratch[i]=*value;
    }
    return scratch[_output];
}
Result<CalculationTrace> trace(const CalculationPlan& plan,std::span<const Value> inputs,const CalculationContext& context,std::size_t limit) {
    if(plan.instructions().size()>limit)return fail(Error::Limit,"trace capacity exceeded");
    std::vector<Value> scratch(plan.instructions().size());auto result=plan.evaluate(inputs,context,scratch);if(!result)return std::unexpected(result.error());
    CalculationTrace output;output.result=*result;
    for(std::size_t i=0;i<scratch.size();++i)output.stages.emplace_back(plan.instructions()[i].node.id,scratch[i]);return output;
}
} // namespace ayt::stats
