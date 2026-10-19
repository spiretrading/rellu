#ifndef RELLU_COMMANDS_ASSET_COMMANDS_HPP
#define RELLU_COMMANDS_ASSET_COMMANDS_HPP
#include <optional>
#include <string>
#include <vector>
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Model/Resources.hpp"

namespace Rellu {

  /** Appends a color swatch to the document's assets, labelled Add Color. */
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

  /** Renames a color swatch, labelled Rename. */
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
   * their color, labelled Delete.
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
   * Character Style.
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

  /** Renames a character style, labelled Rename. */
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
   * keeping its styling, labelled Delete.
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
   * to the document's assets, labelled Add Color, skipping colors already
   * there.
   * @param document The document holding the nodes.
   * @param nodes The nodes, the shapes and text within groups included.
   * @return The command, or <code>nullptr</code> if there is no new color.
   */
  std::unique_ptr<Command> make_add_colors_command(
    const Document& document, const std::vector<NodeId>& nodes);

  /**
   * Makes the command adding the distinct character styles of text nodes to
   * the document's assets, labelled Add Character Style, skipping styles
   * already there.
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
