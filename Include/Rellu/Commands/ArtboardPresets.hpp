#ifndef RELLU_COMMANDS_ARTBOARD_PRESETS_HPP
#define RELLU_COMMANDS_ARTBOARD_PRESETS_HPP
#include <optional>
#include <string>
#include <vector>
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Geometry/Size.hpp"

namespace Rellu {

  /** An artboard size Adobe XD offers for new artboards. */
  struct ArtboardPreset {

    /** The name, which new artboards are named after. */
    std::string m_name;

    /** The size of new artboards. */
    Size m_size;
  };

  /** Lists the kinds of device Adobe XD groups artboard presets by. */
  enum class ArtboardDevice : unsigned char {

    /** A phone. */
    PHONE,

    /** A tablet. */
    TABLET,

    /** A desktop screen. */
    DESKTOP,

    /** A social media post. */
    SOCIAL,

    /** A watch. */
    WATCH
  };

  /** A titled list of artboard presets. */
  struct ArtboardPresetGroup {

    /** The title, in capitals. */
    std::string m_name;

    /** The kind of device the presets are for. */
    ArtboardDevice m_device;

    /** The presets. */
    std::vector<ArtboardPreset> m_presets;
  };

  /** Returns the artboard presets Adobe XD lists, in its order. */
  const std::vector<ArtboardPresetGroup>& get_artboard_presets();

  /**
   * Returns the preset an artboard was last created from, which a click of
   * the Artboard tool creates: iPhone 14 Pro until another is used.
   */
  const ArtboardPreset& get_last_used_preset();

  /**
   * Records the preset an artboard was last created from.
   * @param preset The preset.
   */
  void set_last_used_preset(const ArtboardPreset& preset);

  /**
   * Inserts an artboard of a preset's size as Adobe XD makes one from a
   * preset: white, named after the preset, scrolling vertically within a
   * viewport of its own height, with the grid of a new artboard.
   * @param document The document inserted into.
   * @param transaction The transaction on the document.
   * @param preset The preset.
   * @param position The position of the artboard's top left corner.
   * @return The artboard inserted.
   */
  NodeId insert_preset_artboard(const Document& document,
    Transaction& transaction, const ArtboardPreset& preset, Point position);

  /**
   * Inserts an artboard of a preset's size where Adobe XD places new
   * artboards, labelled Create Artboard: white, named after the preset and
   * scrolling vertically within a viewport of its own height.
   */
  class CreateArtboardCommand : public Command {
    public:

      /**
       * Constructs a CreateArtboardCommand.
       * @param preset The preset.
       * @param selection The selected nodes.
       * @param view The region of the document in view.
       */
      CreateArtboardCommand(ArtboardPreset preset,
        std::vector<NodeId> selection, const Rect& view);

      /**
       * Constructs a CreateArtboardCommand placing the artboard as a click
       * of Adobe XD's Artboard tool does.
       * @param preset The preset.
       * @param point The point clicked in document space.
       * @param view The region of the document in view.
       */
      CreateArtboardCommand(
        ArtboardPreset preset, Point point, const Rect& view);

      /** Returns the artboard inserted by the last application. */
      const NodeId& get_artboard() const;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;
      std::optional<std::vector<NodeId>> get_selection() const override;

    private:
      ArtboardPreset m_preset;
      std::vector<NodeId> m_selection;
      std::optional<Point> m_point;
      Rect m_view;
      NodeId m_artboard;
  };
}

#endif
