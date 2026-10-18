#ifndef RELLU_FORMAT_DOCUMENT_COMPARISON_HPP
#define RELLU_FORMAT_DOCUMENT_COMPARISON_HPP
#include <cstddef>
#include <string>
#include <vector>
#include "Rellu/Model/Document.hpp"

namespace Rellu {

  /**
   * Compares two documents structurally: the canvas, every node by id, the
   * resources and the prototype. Undo history and observers are ignored.
   * @param left The first document.
   * @param right The second document.
   * @param limit The largest number of differences reported.
   * @return A description of each difference found, empty iff the documents
   *         are equal.
   */
  std::vector<std::string> find_differences(const Document& left,
    const Document& right, std::size_t limit = 20);

  /**
   * Tests whether two documents are structurally equal.
   * @param left The first document.
   * @param right The second document.
   * @return <code>true</code> iff find_differences reports no difference.
   */
  bool is_equal(const Document& left, const Document& right);
}

#endif
