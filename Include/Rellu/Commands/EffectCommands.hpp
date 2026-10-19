#ifndef RELLU_COMMANDS_EFFECT_COMMANDS_HPP
#define RELLU_COMMANDS_EFFECT_COMMANDS_HPP
#include <optional>
#include <string>
#include <vector>
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Model/Style.hpp"

namespace Rellu {

  /** Lists the effects Adobe XD's property inspector edits. */
  enum class EffectKind : unsigned char {

    /** The inner shadow. */
    INNER_SHADOW,

    /** The drop shadow. */
    DROP_SHADOW,

    /** The background or object blur. */
    BLUR
  };

  /** The effect properties a SetEffectCommand sets, absent ones kept. */
  struct EffectEdit {

    /** Whether the effect is applied. */
    std::optional<bool> m_is_visible;

    /** The color of a shadow. */
    std::optional<Color> m_color;

    /** The horizontal displacement of a shadow. */
    std::optional<double> m_x;

    /** The vertical displacement of a shadow. */
    std::optional<double> m_y;

    /** The blur radius of a shadow, or the amount of a blur. */
    std::optional<double> m_blur;

    /** The brightness adjustment of a background blur. */
    std::optional<double> m_brightness;

    /** The opacity of the fill over a background blur. */
    std::optional<double> m_fill_opacity;

    /** Whether a blur blurs the backdrop rather than the node itself. */
    std::optional<bool> m_is_background;

    bool operator ==(const EffectEdit&) const = default;
  };

  /**
   * Returns the effect Adobe XD adds when an effect is first applied: a
   * shadow of #000000 at 16% opacity, 3 down with a radius of 3 (shown as
   * B 6), or a background blur of 30 with a brightness of 15 at 0% fill
   * opacity. Changing the type of a blur keeps its values.
   * @param kind The kind of effect.
   */
  Effect make_default_effect(EffectKind kind);

  /**
   * Returns the first effect of a kind among a style's effects.
   * @param effects The effects to search.
   * @param kind The kind of effect.
   * @return The effect, or <code>nullptr</code> if there is none.
   */
  const Effect* find_effect(
    const std::vector<Effect>& effects, EffectKind kind);

  /**
   * Sets properties of the first effect of a kind on shapes and text,
   * labelled Change Effect, adding the default effect to those without one.
   */
  class SetEffectCommand : public Command {
    public:

      /**
       * Constructs a SetEffectCommand.
       * @param nodes The nodes to change.
       * @param kind The kind of effect.
       * @param edit The properties to set.
       */
      SetEffectCommand(
        std::vector<NodeId> nodes, EffectKind kind, EffectEdit edit) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /**
       * Takes the properties set by a SetEffectCommand changing the same
       * effect of the same nodes.
       */
      bool merge(const Command& command) override;

    private:
      std::vector<NodeId> m_nodes;
      EffectKind m_kind;
      EffectEdit m_edit;
  };
}

#endif
