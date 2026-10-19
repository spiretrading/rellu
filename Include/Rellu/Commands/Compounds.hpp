#ifndef RELLU_COMMANDS_COMPOUNDS_HPP
#define RELLU_COMMANDS_COMPOUNDS_HPP
#include <functional>
#include "Rellu/Model/ChangeSet.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/Node.hpp"

namespace Rellu {

  /**
   * Tests whether a compound shape's children are all of its operands, so
   * that its result can be computed from them.
   */
  using CompoundFilter = std::function<bool (const Node& compound)>;

  /**
   * Recomputes the stored results of the compound shapes whose operands
   * changed, as Adobe XD does when an operand is edited, as part of the most
   * recent revision so that a single undo reverts the change along with the
   * new results. Nested compound shapes are recomputed before the compound
   * shapes containing them, and a compound shape is recomputed only if it
   * has operands and the filter accepts it.
   * @param document The document changed.
   * @param changes The changes the most recent revision made.
   * @param filter The test of the compound shapes that can be recomputed,
   *        or empty to accept all of them.
   * @return <code>true</code> iff a stored result changed.
   */
  bool refresh_compounds(Document& document, const ChangeSet& changes,
    const CompoundFilter& filter);
}

#endif
