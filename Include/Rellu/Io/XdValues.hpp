#ifndef RELLU_IO_XD_VALUES_HPP
#define RELLU_IO_XD_VALUES_HPP
#include <optional>
#include <string>
#include <QString>
#include <QStringView>
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Io/XdObject.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Text.hpp"

namespace Rellu {

  /**
   * Converts an Adobe XD identifier to a NodeId.
   * @param text The identifier, normally a UUID.
   * @return The parsed UUID, a NodeId derived from the <i>text</i> when it
   *         is not a UUID, the same text always deriving the same NodeId,
   *         or nil if the <i>text</i> is empty.
   */
  NodeId to_node_id(QStringView text);

  /**
   * Derives a NodeId from an identifier and a qualifier, such as an
   * inherited interaction's id and the node inheriting it.
   * @param text The identifier.
   * @param qualifier The qualifier.
   * @return A NodeId, the same arguments always deriving the same NodeId.
   */
  NodeId derive_node_id(QStringView text, QStringView qualifier);

  /**
   * Converts a string to UTF-8.
   * @param text The string to convert.
   */
  std::string to_std_string(const QString& text);

  /**
   * Converts a packed 32-bit color as Adobe XD stores it in a number.
   * @param value The number holding alpha, red, green and blue from the most
   *        significant byte.
   * @return The color, or <code>std::nullopt</code> if the <i>value</i> is
   *         not a 32-bit unsigned integer.
   */
  std::optional<Color> to_argb_color(double value);

  /**
   * Converts the name of a text case conversion.
   * @param name The name, such as uppercase.
   * @return The conversion, or <code>std::nullopt</code> if the <i>name</i>
   *         is unknown.
   */
  std::optional<TextTransform> to_text_transform(QStringView name);

  /**
   * Converts the name of a text script.
   * @param name The name, such as superscript.
   * @return The script, or <code>std::nullopt</code> if the <i>name</i> is
   *         unknown.
   */
  std::optional<TextScript> to_text_script(QStringView name);

  /**
   * Returns the verticalAlign Adobe XD stores in the layout of a run of
   * text: its baseline offset, positive upward, or 0 if it is not raised or
   * lowered.
   * @param style The run's style.
   */
  double get_vertical_align(const TextStyle& style);

  /**
   * Reads a color of the form {mode: RGB, value: {r, g, b}, alpha}.
   * @param color The color's JSON.
   * @return The color, or <code>std::nullopt</code> if it is malformed.
   */
  std::optional<Color> read_color(XdObject& color);

  /**
   * Reads a transform of the form {a, b, c, d, tx, ty}.
   * @param transform The transform's JSON.
   * @return The transform, or <code>std::nullopt</code> if it is malformed.
   */
  std::optional<Matrix> read_matrix(XdObject& transform);

  /**
   * Returns the Adobe XD specific settings of a node, its meta.ux object.
   * @param json The node's JSON.
   * @return The settings, or <code>nullptr</code> if there are none.
   */
  XdObject* get_ux(XdObject& json);
}

#endif
