#ifndef RELLU_GEOMETRY_EQUALITY_HPP
#define RELLU_GEOMETRY_EQUALITY_HPP
#include <algorithm>
#include <map>
#include <optional>
#include <vector>

namespace Rellu {

  /**
   * Tests whether two numbers are equal, treating every NaN as equal to every
   * other NaN.
   * @param left The first number.
   * @param right The second number.
   * @return <code>true</code> iff the numbers are equal or both are NaN.
   */
  constexpr bool equals(double left, double right) {
    return left == right || (left != left && right != right);
  }

  /**
   * Tests whether two optional numbers are equal, treating every NaN as equal
   * to every other NaN.
   * @param left The first number.
   * @param right The second number.
   * @return <code>true</code> iff both are absent, or both are present and
   *         equal.
   */
  constexpr bool equals(
      const std::optional<double>& left, const std::optional<double>& right) {
    if(!left || !right) {
      return !left == !right;
    }
    return equals(*left, *right);
  }

  /**
   * Tests whether two sequences of numbers are equal, treating every NaN as
   * equal to every other NaN.
   * @param left The first sequence.
   * @param right The second sequence.
   * @return <code>true</code> iff the sequences have the same length and
   *         their numbers are pairwise equal.
   */
  inline bool equals(
      const std::vector<double>& left, const std::vector<double>& right) {
    return std::ranges::equal(left, right,
      [] (const auto& left, const auto& right) {
        return equals(left, right);
      });
  }

  /**
   * Tests whether two maps of numbers are equal, treating every NaN as equal
   * to every other NaN.
   * @param left The first map.
   * @param right The second map.
   * @return <code>true</code> iff the maps have the same keys and their
   *         numbers are equal by key.
   */
  template<typename K>
  bool equals(const std::map<K, double>& left,
      const std::map<K, double>& right) {
    return std::ranges::equal(left, right,
      [] (const auto& left, const auto& right) {
        return left.first == right.first && equals(left.second, right.second);
      });
  }
}

#endif
