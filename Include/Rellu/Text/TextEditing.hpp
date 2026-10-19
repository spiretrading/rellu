#ifndef RELLU_TEXT_TEXT_EDITING_HPP
#define RELLU_TEXT_TEXT_EDITING_HPP
#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Text/TextLayout.hpp"

namespace Rellu {

  /**
   * Finds the line a caret is displayed on: the line containing the index,
   * or for an index ending a line that wraps, the line following it.
   * @param lines The lines of the text, as by measure_lines.
   * @param index The caret's index.
   * @return The index of the line.
   */
  std::size_t find_line(
    const std::vector<LineGeometry>& lines, std::size_t index);

  /**
   * Returns the x of a caret on a line, clamping the index to the line.
   * @param line The line.
   * @param index The caret's index.
   */
  double get_caret_x(const LineGeometry& line, std::size_t index);

  /**
   * Returns the zero width rect a caret is drawn along, from the top to the
   * bottom of its line, in the node's local space.
   * @param lines The lines of the text, as by measure_lines.
   * @param index The caret's index.
   */
  Rect get_caret_rect(
    const std::vector<LineGeometry>& lines, std::size_t index);

  /**
   * Returns the rects highlighting a range of text, one per line it covers,
   * in the node's local space.
   * @param lines The lines of the text, as by measure_lines.
   * @param start The index of the range's first UTF-16 code unit.
   * @param end The index one past the range's last UTF-16 code unit.
   */
  std::vector<Rect> get_selection_rects(const std::vector<LineGeometry>& lines,
    std::size_t start, std::size_t end);

  /**
   * Finds the caret index nearest a horizontal position on a line, at a
   * grapheme boundary. Past the end of a line that wraps after a space, the
   * caret goes before that space.
   * @param lines The lines of the text, as by measure_lines.
   * @param content The text's content.
   * @param line The index of the line.
   * @param x The horizontal position in the node's local space.
   */
  std::size_t hit_test_line(const std::vector<LineGeometry>& lines,
    std::u16string_view content, std::size_t line, double x);

  /**
   * Finds the caret index nearest a point.
   * @param lines The lines of the text, as by measure_lines.
   * @param content The text's content.
   * @param point The point in the node's local space.
   */
  std::size_t hit_test(const std::vector<LineGeometry>& lines,
    std::u16string_view content, Point point);

  /**
   * Moves a caret to an adjacent line, nearest a horizontal position, or to
   * the start or end of the text past the first or last line.
   * @param lines The lines of the text, as by measure_lines.
   * @param content The text's content.
   * @param index The caret's index.
   * @param x The horizontal position to keep to.
   * @param delta The number of lines to move by, negative moving up.
   */
  std::size_t move_by_lines(const std::vector<LineGeometry>& lines,
    std::u16string_view content, std::size_t index, double x, int delta);

  /**
   * Returns the index starting the line a caret is displayed on.
   * @param lines The lines of the text, as by measure_lines.
   * @param index The caret's index.
   */
  std::size_t get_line_start(
    const std::vector<LineGeometry>& lines, std::size_t index);

  /**
   * Returns the index ending the line a caret is displayed on, before the
   * space a wrapped line ends with.
   * @param lines The lines of the text, as by measure_lines.
   * @param content The text's content.
   * @param index The caret's index.
   */
  std::size_t get_line_end(const std::vector<LineGeometry>& lines,
    std::u16string_view content, std::size_t index);

  /**
   * Returns the grapheme boundary following an index.
   * @param content The text's content.
   * @param index The index.
   */
  std::size_t get_next_grapheme(std::u16string_view content, std::size_t index);

  /**
   * Returns the grapheme boundary preceding an index.
   * @param content The text's content.
   * @param index The index.
   */
  std::size_t get_previous_grapheme(
    std::u16string_view content, std::size_t index);

  /**
   * Returns the start of the word following an index, past the spaces after
   * the word at the index, as Ctrl+Right does on Windows.
   * @param content The text's content.
   * @param index The index.
   */
  std::size_t get_next_word(std::u16string_view content, std::size_t index);

  /**
   * Returns the start of the word preceding an index, as Ctrl+Left does on
   * Windows.
   * @param content The text's content.
   * @param index The index.
   */
  std::size_t get_previous_word(std::u16string_view content, std::size_t index);

  /**
   * Returns the range of the word, run of spaces or punctuation mark at an
   * index, as a double click selects.
   * @param content The text's content.
   * @param index The index.
   * @return The start and end of the range.
   */
  std::pair<std::size_t, std::size_t> find_word(
    std::u16string_view content, std::size_t index);

  /**
   * Returns the range of the paragraph containing an index, without its
   * line separator, as a triple click selects.
   * @param content The text's content.
   * @param index The index.
   * @return The start and end of the range.
   */
  std::pair<std::size_t, std::size_t> find_paragraph(
    std::u16string_view content, std::size_t index);
}

#endif
