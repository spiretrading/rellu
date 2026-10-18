#ifndef RELLU_UI_PROPERTY_DESCRIPTOR_HPP
#define RELLU_UI_PROPERTY_DESCRIPTOR_HPP
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <QString>
#include <QStringList>
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Size.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /** A value chosen among named options, such as a blend mode. */
  struct Choice {

    /** The index of the option chosen. */
    int m_index = 0;

    /** The names of the options. */
    QStringList m_options;

    bool operator ==(const Choice&) const = default;
  };

  /**
   * The value of a property, where std::monostate stands for a heading
   * without a value of its own.
   */
  using PropertyValue = std::variant<
    std::monostate, bool, int, double, QString, Color, Point, Size, Choice>;

  /** Describes a property of a node, along with its nested properties. */
  struct PropertyDescriptor {

    /**
     * The dot separated path identifying the property within its node, such
     * as <code>appearance.opacity</code>.
     */
    std::string m_path;

    /** The label shown. */
    QString m_label;

    /** The value. */
    PropertyValue m_value;

    /** The nested properties. */
    std::vector<PropertyDescriptor> m_children;
  };

  /**
   * Describes the properties of a node, grouped into top-level sections. A
   * reference is described by what it displays, along with a section
   * describing the reference itself.
   * @param document The document containing the node.
   * @param id The node's id.
   * @return The sections, or empty if there is no such node.
   */
  std::vector<PropertyDescriptor> extract_properties(
    const Document& document, const NodeId& id);

  /**
   * Finds a property by its path.
   * @param properties The properties to search, including nested ones.
   * @param path The property's path.
   * @return The property, or <code>nullptr</code> if there is none.
   */
  const PropertyDescriptor* find_property(
    const std::vector<PropertyDescriptor>& properties, std::string_view path);

  /**
   * Formats a property value for display.
   * @param value The value to format.
   */
  QString to_text(const PropertyValue& value);
}

#endif
