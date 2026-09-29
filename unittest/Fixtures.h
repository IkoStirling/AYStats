#pragma once
#include <AYStats/Effects.h>
#include <stdexcept>
using namespace ayt::stats;
inline StatSet fixture() {
    Definition attack{.name="attack",.mode=Mode::Advanced,.initial=100,.stage=1,
                      .zones={{1,ZoneOp::Flat},{2,ZoneOp::PercentSum},{3,ZoneOp::FactorProduct},{4,ZoneOp::Override}}};
    Definition health{.name="health",.shape=Shape::Pool,.current=100,.capacity=100};
    auto schema=Schema::compile({attack,health});if(!schema)throw std::runtime_error(schema.error().message);
    auto set=StatSet::create(*schema);if(!set)throw std::runtime_error(set.error().message);return *set;
}
inline Modifier modifier(std::uint64_t handle,std::uint32_t zone,Value value) {
    return {.handle=handle,.effect=1,.rule=handle,.target=statId("attack"),.stage=1,.zone=zone,.value=value};
}
inline CalculationPlan damagePlan() {
    return CalculationPlan::compile({{.id=1,.op=Op::Input,.binding=0},{.id=2,.op=Op::Input,.binding=1},
                                    {.id=3,.op=Op::MultiplyRate,.a=1,.b=2}},3,2).value();
}
