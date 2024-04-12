
#include "backends/p4tools/modules/flay/targets/bmv2/table_executor.h"

#include <boost/multiprecision/cpp_int.hpp>

#include "backends/p4tools/common/control_plane/symbolic_variables.h"
#include "backends/p4tools/common/lib/variables.h"
#include "backends/p4tools/modules/flay/core/expression_resolver.h"
#include "backends/p4tools/modules/flay/targets/bmv2/constants.h"
#include "ir/irutils.h"
#include "lib/cstring.h"
#include "lib/exceptions.h"

namespace P4Tools::Flay::V1Model {

V1ModelTableExecutor::V1ModelTableExecutor(const IR::P4Table &table,
                                           ExpressionResolver &callingResolver)
    : TableExecutor(table, callingResolver) {}

const IR::Expression *V1ModelTableExecutor::computeTargetMatchType(
    const IR::KeyElement *keyField) const {
    auto tableName = getP4Table().controlPlaneName();
    const auto *keyExpr = keyField->expression;
    const auto matchType = keyField->matchType->toString();
    const auto *nameAnnot = keyField->getAnnotation("name");
    bool isTainted = false;
    // Some hidden tables do not have any key name annotations.
    BUG_CHECK(nameAnnot != nullptr /* || properties.tableIsImmutable*/,
              "Non-constant table key without an annotation");
    cstring fieldName;
    if (nameAnnot != nullptr) {
        fieldName = nameAnnot->getName();
    }

    // TODO: We consider optional match types to be a no-op, but we could make them exact matches.
    if (matchType == V1ModelConstants::MATCH_KIND_OPT) {
        // We can recover from taint by simply not adding the optional match.
        // Create a new symbolic variable that corresponds to the key expression.
        const auto *ctrlPlaneKey =
            ControlPlaneState::getTableKey(tableName, fieldName, keyExpr->type);
        if (isTainted) {
            return IR::getBoolLiteral(true);
        }
        return new IR::Equ(keyExpr, ctrlPlaneKey);
    }
    // Action selector entries are not part of the match but we still need to create a key.
    if (matchType == V1ModelConstants::MATCH_KIND_SELECTOR) {
        cstring keyName = tableName + "_selector_" + fieldName;
        const auto *ctrlPlaneKey = ToolsVariables::getSymbolicVariable(keyExpr->type, keyName);
        return new IR::Equ(keyExpr, ctrlPlaneKey);
    }
    if (matchType == V1ModelConstants::MATCH_KIND_RANGE) {
        // We can recover from taint by matching on the entire possible range.
        const IR::Expression *minKey = nullptr;
        const IR::Expression *maxKey = nullptr;
        if (isTainted) {
            minKey = IR::getConstant(keyExpr->type, 0);
            maxKey = IR::getConstant(keyExpr->type, IR::getMaxBvVal(keyExpr->type));
            keyExpr = minKey;
        } else {
            auto symbolicTableRange =
                Bmv2ControlPlaneState::getTableRange(tableName, fieldName, keyExpr->type);
            minKey = symbolicTableRange.first;
            maxKey = symbolicTableRange.second;
        }
        return new IR::LAnd(new IR::LAnd(new IR::Lss(minKey, maxKey), new IR::Leq(minKey, keyExpr)),
                            new IR::Leq(keyExpr, maxKey));
    }
    // If the custom match type does not match, delete to the core match types.
    return TableExecutor::computeTargetMatchType(keyField);
}

}  // namespace P4Tools::Flay::V1Model
