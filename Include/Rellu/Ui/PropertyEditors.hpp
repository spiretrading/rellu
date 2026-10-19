#ifndef RELLU_UI_PROPERTY_EDITORS_HPP
#define RELLU_UI_PROPERTY_EDITORS_HPP
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <QString>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Ui/PropertyDescriptor.hpp"

namespace Rellu {

  /**
   * Makes the command setting a property of nodes to a value, or returns
   * <code>nullptr</code> when the value does not apply to them.
   */
  using PropertySetter = std::function<std::unique_ptr<Command> (
    const Document& document, const std::vector<NodeId>& nodes,
    const PropertyValue& value)>;

  /** Describes how a property is edited. */
  struct PropertyEditor {

    /** The function making the command setting the property. */
    PropertySetter m_setter;

    /** The least numeric value accepted, if any. */
    std::optional<double> m_minimum;

    /** The greatest numeric value accepted, if any. */
    std::optional<double> m_maximum;

    /** The factor a numeric value is multiplied by when displayed. */
    double m_scale = 1;

    /** The unit shown after a numeric value, such as %. */
    QString m_suffix;
  };

  /** Keeps the editors of properties by property path. */
  class PropertyEditors {
    public:

      /** Constructs an empty PropertyEditors. */
      PropertyEditors() = default;

      /**
       * Adds or replaces the editor of a property.
       * @param path The property's path, such as appearance.opacity.
       * @param editor The editor.
       */
      void add(std::string path, PropertyEditor editor);

      /**
       * Finds the editor of a property.
       * @param path The property's path.
       * @return The editor, or <code>nullptr</code> if the property is not
       *         editable.
       */
      const PropertyEditor* find(std::string_view path) const;

    private:
      std::map<std::string, PropertyEditor, std::less<>> m_editors;
  };

  /**
   * Makes the editors of the position, size, rotation, opacity, blend mode,
   * fill, border, name, visibility and lock of nodes.
   * @param measurer The function measuring text.
   */
  PropertyEditors make_standard_property_editors(TextMeasurer measurer);
}

#endif
