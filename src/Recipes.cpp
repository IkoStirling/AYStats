#include <AYStats/Recipes.h>
#include <algorithm>
#include <tuple>

namespace ayt::stats {
namespace {
auto fail(Error code, const char* message) { return std::unexpected(Diagnostic{code, message, {}}); }
bool valid(AttributeOp op) { return op >= AttributeOp::Increase && op <= AttributeOp::Set; }
}
Result<std::shared_ptr<const RecipeBook>> RecipeBook::compile(std::vector<Recipe> recipes) {
    if (recipes.size() > 4096) return fail(Error::Limit, "recipe count limit");
    for (std::size_t i = 0; i < recipes.size(); ++i) {
        const auto& r = recipes[i];
        if (r.name.empty() || r.name.size() > 128 || !r.target || !valid(r.operation) ||
            r.plan.instructions().empty() || r.inputs.size() != r.plan.inputCount())
            return fail(Error::Invalid, "invalid recipe name/target/operation/input shape");
        if (r.inputs.size() > 64) return fail(Error::Limit, "recipe input count limit");
        for (const auto& input : r.inputs) {
            if (input.source < InputSource::Amount || input.source > InputSource::Constant ||
                input.field < InputField::Effective || input.field > InputField::Capacity ||
                ((input.source == InputSource::SourceStat || input.source == InputSource::TargetStat) && !input.stat))
                return fail(Error::Invalid, "invalid recipe input binding");
        }
        for (std::size_t j = 0; j < i; ++j) {
            const auto& other = recipes[j];
            if (r.name == other.name) return fail(Error::Duplicate, "recipe name already exists");
            if (r.isDefault && other.isDefault && r.target == other.target && r.operation == other.operation)
                return fail(Error::Duplicate, "multiple defaults for stat/operation");
        }
    }
    auto book = std::make_shared<RecipeBook>(); book->_recipes = std::move(recipes);
    for(std::size_t i=0;i<book->_recipes.size();++i){book->_names.push_back(i);if(book->_recipes[i].isDefault)book->_defaults.push_back(i);}
    std::sort(book->_names.begin(),book->_names.end(),[&](auto a,auto b){return book->_recipes[a].name<book->_recipes[b].name;});
    std::sort(book->_defaults.begin(),book->_defaults.end(),[&](auto a,auto b){const auto& first=book->_recipes[a];const auto& second=book->_recipes[b];return std::tie(first.target,first.operation)<std::tie(second.target,second.operation);});
    return std::shared_ptr<const RecipeBook>(std::move(book));
}
Result<const Recipe*> RecipeBook::select(StatId target, AttributeOp operation, std::string_view name) const {
    if (!target || !valid(operation)) return fail(Error::Invalid, "invalid recipe selection");
    if(!name.empty()) {
        auto found=std::lower_bound(_names.begin(),_names.end(),name,[&](auto index,std::string_view key){return _recipes[index].name<key;});
        if(found==_names.end() || _recipes[*found].name!=name)return fail(Error::Missing,"explicit recipe not found");
        const auto& r=_recipes[*found];
        if(r.target!=target || r.operation!=operation)return fail(Error::Invalid,"recipe is incompatible with stat/operation");
        return &r;
    }
    const auto key=std::pair{target,operation};
    auto found=std::lower_bound(_defaults.begin(),_defaults.end(),key,[&](auto index,const auto& selected){const auto& r=_recipes[index];return std::pair{r.target,r.operation}<selected;});
    if(found!=_defaults.end()){const auto& r=_recipes[*found];if(r.target==target && r.operation==operation)return &r;}
    return nullptr; // Direct amount is the basic default, selected by the integration.
}
Result<std::vector<Value>> sampleRecipe(const Recipe& recipe, Value amount, const StatSet& target, const StatSet* source) {
    if(recipe.inputs.size()!=recipe.plan.inputCount())return fail(Error::Invalid,"recipe input shape mismatch");
    std::vector<Value> inputs; inputs.reserve(recipe.inputs.size());
    for (const auto& binding : recipe.inputs) {
        if(binding.source<InputSource::Amount || binding.source>InputSource::Constant ||
           binding.field<InputField::Effective || binding.field>InputField::Capacity)
            return fail(Error::Invalid,"invalid recipe input binding");
        if (binding.source == InputSource::Amount) { inputs.push_back(amount); continue; }
        if (binding.source == InputSource::Constant) { inputs.push_back(binding.constant); continue; }
        const auto* state = binding.source == InputSource::SourceStat ? source : &target;
        if (!state) return fail(Error::Missing, "recipe requires a source state");
        auto row = state->read(binding.stat); if (!row) return std::unexpected(row.error());
        auto index = state->schema()->index(binding.stat);
        if ((binding.field == InputField::Current || binding.field == InputField::Capacity) &&
            state->schema()->definitions()[*index].shape != Shape::Pool)
            return fail(Error::Invalid, "current/capacity recipe input requires pool");
        switch (binding.field) {
        case InputField::Effective: inputs.push_back(row->effective); break;
        case InputField::Current: inputs.push_back(row->current); break;
        case InputField::Base: inputs.push_back(row->base); break;
        case InputField::Capacity: inputs.push_back(row->capacity); break;
        }
    }
    return inputs;
}
Result<Mutation> attributeMutation(const StatSet& state, StatId target, AttributeOp operation, Value calculated) {
    if (!valid(operation)) return fail(Error::Invalid, "invalid attribute operation");
    auto index = state.schema()->index(target); if (!index) return fail(Error::Missing, "attribute not found");
    const auto& definition = state.schema()->definitions()[*index];
    if (definition.lifetime != Lifetime::Persistent) return fail(Error::Invalid, "cannot mutate intermediate attribute");
    if (operation != AttributeOp::Set && calculated < 0) return fail(Error::Invalid, "negative increase/decrease output");
    if (definition.shape == Shape::Pool) {
        const auto op = operation == AttributeOp::Set ? MutationOp::SetCurrent :
            (operation == AttributeOp::Increase ? MutationOp::Increase : MutationOp::Decrease);
        return Mutation{target, op, calculated};
    }
    auto row = state.read(target); if (!row) return std::unexpected(row.error());
    auto next = operation == AttributeOp::Set ? Result<Value>(calculated) :
        add(row->base, operation == AttributeOp::Decrease ? -calculated : calculated);
    if (!next) return std::unexpected(next.error());
    return Mutation{target, MutationOp::SetBase, *next};
}
} // namespace ayt::stats
