#include <AYTest.h>
#include <AYStats/Authoring.h>
#include "Fixtures.h"
#include <array>
TEST_SUITE(StatsAuthoringTests)
TEST_CASE(atomic_edit_preview_and_invalid_conversion) {
    const auto original=encodeDefinitions(*fixture().schema()).value();
    CHECK_FALSE(editDefinitionField(original,statId("health"),"capacity","10").has_value());
    CHECK_TRUE(previewDefinitions(original)->read(statId("health"))->current==100);
    auto changed=editDefinitionField(original,statId("attack"),"initial","250");CHECK_TRUE(changed.has_value());CHECK_TRUE(previewDefinitions(*changed)->read(statId("attack"))->effective==250);
    CHECK_TRUE(previewDefinitions(original)->read(statId("attack"))->effective==100);
    CHECK_FALSE(editDefinitionField(original,statId("attack"),"mode","basic").has_value());
    CHECK_TRUE(previewDefinitions(original,std::array{modifier(1,1,20)})->read(statId("attack"))->effective==120);
}
TEST_CASE(recipe_trace_validation_preserves_exact_numbers) {
    auto text=encodePlan(damagePlan()).value();auto preview=previewRecipe(text,"100,15000");CHECK_TRUE(preview.has_value());CHECK_TRUE(preview->result==150);CHECK_TRUE(preview->stages.size()==3);
    CHECK_FALSE(previewRecipe(text,"100,15000,").has_value());CHECK_FALSE(previewRecipe(text,"100.5,15000").has_value());CHECK_FALSE(previewRecipe(text,"100").has_value());
}
TEST_SUITE_END
