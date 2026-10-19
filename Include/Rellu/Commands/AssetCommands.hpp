#ifndef RELLU_COMMANDS_ASSET_COMMANDS_HPP
#define RELLU_COMMANDS_ASSET_COMMANDS_HPP
#include <optional>
#include <string>
#include <vector>
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Model/Resources.hpp"

namespace Rellu {

  /** The id of the section of the document's assets listing colors. */
  inline const auto COLOR_SECTION = std::string("groupColors");

  /** The id of the section of the document's assets listing text styles. */
  inline const auto CHARACTER_STYLE_SECTION =
    std::string("groupCharacterStyles");

  /** An entry listed within a section or group of the document's assets. */
  struct AssetEntry {

    /** The id of the group listed, or empty if the entry is an asset. */
    std::string m_group;

    /** The id of the asset listed, or nil if the entry is a group. */
    NodeId m_asset;

    bool operator ==(const AssetEntry&) const = default;
  };

  /**
   * Returns the key Adobe XD orders an asset by between two others.
   * @param lower The key before, or empty for the start.
   * @param upper The key after, or empty for the end.
   */
  std::string make_order_between(
    const std::string& lower, const std::string& upper);

  /**
   * Returns ascending keys spread between two others as Adobe XD makes
   * them for several assets at once.
   * @param count The number of keys.
   * @param lower The key before, or empty for the start.
   * @param upper The key after, or empty for the end.
   */
  std::vector<std::string> make_orders_between(
    std::size_t count, const std::string& lower, const std::string& upper);

  /**
   * Returns the id of the section listing an asset: COLOR_SECTION,
   * CHARACTER_STYLE_SECTION, or empty if there is no such asset.
   * @param resources The resources holding the asset.
   * @param asset The id of a color swatch or character style.
   */
  std::string find_section(const Resources& resources, const NodeId& asset);

  /**
   * Returns the id of the group directly holding an asset, its section's
   * id if it is in no group.
   * @param resources The resources holding the asset.
   * @param asset The id of a color swatch or character style.
   */
  std::string find_asset_group(
    const Resources& resources, const NodeId& asset);

  /**
   * Lists the groups and assets directly within a section or group in the
   * order Adobe XD's Document Assets panel shows them: those without an
   * ordering key first, latest first, then by their keys.
   * @param resources The resources holding the assets.
   * @param group The id of the section or group.
   */
  std::vector<AssetEntry> list_assets(
    const Resources& resources, const std::string& group);

  /**
   * Lists the visible nodes on the canvas painted with a color swatch's
   * color or gradient as a fill or border.
   * @param document The document.
   * @param swatch The swatch.
   */
  std::vector<NodeId> find_swatch_instances(
    const Document& document, const ColorSwatch& swatch);

  /**
   * Lists the visible text on the canvas styled by a character style.
   * @param document The document.
   * @param style The style.
   */
  std::vector<NodeId> find_character_style_instances(
    const Document& document, const CharacterStyle& style);

  /**
   * Places assets of one section in a new group of assets named New group
   * within the group holding the first of them, labelled Create Asset
   * Group.
   */
  class GroupAssetsCommand : public Command {
    public:

      /**
       * Constructs a GroupAssetsCommand.
       * @param assets The ids of the color swatches or character styles.
       */
      explicit GroupAssetsCommand(std::vector<NodeId> assets) noexcept;

      /** Returns the id of the group made. */
      const std::string& get_group() const;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::vector<NodeId> m_assets;
      std::string m_group;
  };

  /**
   * Moves assets of one section to the start of a group of that section or
   * to the section itself, labelled Set Order in Group.
   */
  class MoveAssetsCommand : public Command {
    public:

      /**
       * Constructs a MoveAssetsCommand.
       * @param assets The ids of the color swatches or character styles.
       * @param group The id of the group, or of the section.
       */
      MoveAssetsCommand(std::vector<NodeId> assets, std::string group) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::vector<NodeId> m_assets;
      std::string m_group;
  };

  /**
   * Adds a group of assets named New group first within another group,
   * labelled Create Asset Group, as Create subgroup does.
   */
  class AddAssetGroupCommand : public Command {
    public:

      /**
       * Constructs an AddAssetGroupCommand.
       * @param parent The id of the group to hold the new group.
       */
      explicit AddAssetGroupCommand(std::string parent) noexcept;

      /** Returns the id of the group made. */
      const std::string& get_group() const;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::string m_parent;
      std::string m_group;
  };

  /**
   * Removes a group of assets, labelled Delete Asset Group: as Ungroup
   * does, its assets and groups then belonging to its parent, or as Delete
   * does, deleting them too.
   */
  class RemoveAssetGroupCommand : public Command {
    public:

      /**
       * Constructs a RemoveAssetGroupCommand.
       * @param group The group's id.
       * @param is_content_deleted Whether the assets and groups within the
       *        group are deleted.
       */
      RemoveAssetGroupCommand(
        std::string group, bool is_content_deleted) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::string m_group;
      bool m_is_content_deleted;
  };

  /** Renames a group of assets, labelled Rename Asset Group. */
  class RenameAssetGroupCommand : public Command {
    public:

      /**
       * Constructs a RenameAssetGroupCommand.
       * @param group The group's id.
       * @param name The new name.
       */
      RenameAssetGroupCommand(std::string group, std::string name) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::string m_group;
      std::string m_name;
  };

  /**
   * Appends a color swatch to the document's assets, labelled Add Color to
   * Assets, ordering it first among the assets of its section.
   */
  class AddSwatchCommand : public Command {
    public:

      /**
       * Constructs an AddSwatchCommand.
       * @param swatch The swatch, whose id is generated if nil.
       */
      explicit AddSwatchCommand(ColorSwatch swatch) noexcept;

      /** Returns the id of the swatch added. */
      const SwatchId& get_id() const;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      ColorSwatch m_swatch;
  };

  /**
   * Replaces a color swatch, recoloring the solid fills linked to it,
   * labelled Edit Color.
   */
  class ReplaceSwatchCommand : public Command {
    public:

      /**
       * Constructs a ReplaceSwatchCommand.
       * @param swatch The new swatch, replacing the one with its id.
       */
      explicit ReplaceSwatchCommand(ColorSwatch swatch) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /** Takes the swatch of a ReplaceSwatchCommand replacing the same one. */
      bool merge(const Command& command) override;

    private:
      ColorSwatch m_swatch;
  };

  /** Renames a color swatch, labelled Rename Color. */
  class RenameSwatchCommand : public Command {
    public:

      /**
       * Constructs a RenameSwatchCommand.
       * @param id The swatch's id.
       * @param name The new name, empty for none.
       */
      RenameSwatchCommand(const SwatchId& id, std::string name) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      SwatchId m_id;
      std::string m_name;
  };

  /**
   * Removes a color swatch, unlinking the fills linked to it while keeping
   * their color, labelled Delete Color.
   */
  class RemoveSwatchCommand : public Command {
    public:

      /**
       * Constructs a RemoveSwatchCommand.
       * @param id The swatch's id.
       */
      explicit RemoveSwatchCommand(const SwatchId& id) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      SwatchId m_id;
  };

  /**
   * Appends a character style to the document's assets, labelled Add
   * Character Style to Assets, ordering it first among the character
   * styles.
   */
  class AddCharacterStyleCommand : public Command {
    public:

      /**
       * Constructs an AddCharacterStyleCommand.
       * @param style The style, whose id is generated if nil.
       */
      explicit AddCharacterStyleCommand(CharacterStyle style) noexcept;

      /** Returns the id of the style added. */
      const CharacterStyleId& get_id() const;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      CharacterStyle m_style;
  };

  /**
   * Replaces a character style, restyling the text linked to it, labelled
   * Edit Character Style.
   */
  class ReplaceCharacterStyleCommand : public Command {
    public:

      /**
       * Constructs a ReplaceCharacterStyleCommand.
       * @param style The new style, replacing the one with its id.
       */
      explicit ReplaceCharacterStyleCommand(CharacterStyle style) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /**
       * Takes the style of a ReplaceCharacterStyleCommand replacing the same
       * one.
       */
      bool merge(const Command& command) override;

    private:
      CharacterStyle m_style;
  };

  /** Renames a character style, labelled Rename Character Style. */
  class RenameCharacterStyleCommand : public Command {
    public:

      /**
       * Constructs a RenameCharacterStyleCommand.
       * @param id The style's id.
       * @param name The new name.
       */
      RenameCharacterStyleCommand(
        const CharacterStyleId& id, std::string name) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      CharacterStyleId m_id;
      std::string m_name;
  };

  /**
   * Removes a character style, unlinking the text linked to it while
   * keeping its styling, labelled Delete Character Style.
   */
  class RemoveCharacterStyleCommand : public Command {
    public:

      /**
       * Constructs a RemoveCharacterStyleCommand.
       * @param id The style's id.
       */
      explicit RemoveCharacterStyleCommand(
        const CharacterStyleId& id) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      CharacterStyleId m_id;
  };

  /**
   * Styles the whole of text nodes with a character style and links them to
   * it, labelled Apply Character Style.
   */
  class ApplyCharacterStyleCommand : public Command {
    public:

      /**
       * Constructs an ApplyCharacterStyleCommand.
       * @param nodes The nodes to style; those that are not text are left
       *        unchanged.
       * @param style The style's id.
       */
      ApplyCharacterStyleCommand(
        std::vector<NodeId> nodes, const CharacterStyleId& style) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::vector<NodeId> m_nodes;
      CharacterStyleId m_style;
  };

  /**
   * Makes the command adding the distinct fill and border colors of nodes
   * to the document's assets, labelled Add Color to Assets or Add Colors to
   * Assets, skipping colors already there and ordering the last color
   * added first.
   * @param document The document holding the nodes.
   * @param nodes The nodes, the shapes and text within groups included.
   * @return The command, or <code>nullptr</code> if there is no new color.
   */
  std::unique_ptr<Command> make_add_colors_command(
    const Document& document, const std::vector<NodeId>& nodes);

  /**
   * Makes the command adding the distinct character styles of text nodes to
   * the document's assets, labelled Add Character Style to Assets or Add
   * Character Styles to Assets, skipping styles already there and ordering
   * the last style added first.
   * @param document The document holding the nodes.
   * @param nodes The nodes, the text within groups included.
   * @return The command, or <code>nullptr</code> if there is no new style.
   */
  std::unique_ptr<Command> make_add_character_styles_command(
    const Document& document, const std::vector<NodeId>& nodes);

  /**
   * Adds the commands object.add_color and object.add_character_style.
   * @param registry The registry to add to.
   */
  void add_asset_commands(CommandRegistry& registry);

  /**
   * Returns the fill painting a color swatch: a solid fill linked to it, or
   * a left to right linear gradient of its stops.
   * @param swatch The swatch.
   */
  Fill make_swatch_fill(const ColorSwatch& swatch);

  /**
   * Returns the character style describing the first character of a text.
   * @param text The text.
   */
  CharacterStyle make_character_style(const Text& text);

  /**
   * Finds the swatch a fill shows: the one it is linked to, otherwise the
   * first whose color or stops it paints.
   * @param resources The resources holding the swatches.
   * @param fill The fill.
   * @return The swatch, or <code>nullptr</code> if there is none.
   */
  const ColorSwatch* find_swatch(const Resources& resources, const Fill& fill);

  /**
   * Finds the character style the first character of a text shows: the one
   * it is linked to, otherwise the first describing it.
   * @param resources The resources holding the styles.
   * @param text The text.
   * @return The style, or <code>nullptr</code> if there is none.
   */
  const CharacterStyle* find_character_style(
    const Resources& resources, const Text& text);
}

#endif
