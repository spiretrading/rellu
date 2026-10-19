#ifndef RELLU_COMMANDS_TEXT_COMMANDS_HPP
#define RELLU_COMMANDS_TEXT_COMMANDS_HPP
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Model/NodeHeader.hpp"
#include "Rellu/Model/Text.hpp"

namespace Rellu {

  /** The longest name, in UTF-16 code units, Adobe XD derives from text. */
  inline constexpr auto MAXIMUM_TEXT_NAME_LENGTH = std::size_t(256);

  /** Computes the layout Adobe XD stores for a text payload. */
  using TextLayouter = std::function<StoredLayout (const Text& text)>;

  /** A range of a text's content in UTF-16 code units. */
  struct TextRange {

    /** The index of the range's first code unit. */
    std::size_t m_start = 0;

    /** The index one past the range's last code unit. */
    std::size_t m_end = 0;

    bool operator ==(const TextRange&) const = default;
  };

  /** The character properties a text style edit sets, absent ones kept. */
  struct TextStyleEdit {

    /** The font family. */
    std::optional<std::string> m_family;

    /** The style within the font family. */
    std::optional<std::string> m_style;

    /** The PostScript name of the font. */
    std::optional<std::string> m_postscript_name;

    /** The font size. */
    std::optional<double> m_size;

    /** The extra space between characters, in thousandths of an em. */
    std::optional<double> m_letter_spacing;

    /** The color of the glyphs. */
    std::optional<Color> m_color;

    /** Whether the text is underlined. */
    std::optional<bool> m_is_underlined;

    /** Whether the text is struck through. */
    std::optional<bool> m_is_struck_through;

    /** The case conversion. */
    std::optional<TextTransform> m_transform;

    /** The placement relative to the baseline. */
    std::optional<TextScript> m_script;

    bool operator ==(const TextStyleEdit&) const = default;
  };

  /** The paragraph properties a paragraph style edit sets, absent ones kept. */
  struct ParagraphStyleEdit {

    /** The alignment of lines. */
    std::optional<TextAlignment> m_alignment;

    /** The distance between baselines, holding none for automatic. */
    std::optional<std::optional<double>> m_line_height;

    /** The extra space after each paragraph. */
    std::optional<double> m_paragraph_spacing;

    bool operator ==(const ParagraphStyleEdit&) const = default;
  };

  /** Lists the frames Adobe XD's text section switches between. */
  enum class TextFrameKind : unsigned char {

    /** A point frame, Auto Width. */
    AUTO_WIDTH,

    /** A frame of fixed width growing in height, Auto Height. */
    AUTO_HEIGHT,

    /** A frame of fixed size, Fixed Size. */
    FIXED_SIZE
  };

  /**
   * Returns the style Adobe XD on Windows gives new text: Segoe UI Regular
   * at 20 pixels in #707070.
   */
  TextStyle get_default_text_style();

  /**
   * Returns the name Adobe XD gives a text node with a given content: the
   * content, cut to MAXIMUM_TEXT_NAME_LENGTH code units.
   * @param content The text's content.
   */
  std::string make_text_name(std::u16string_view content);

  /**
   * Returns the runs of a text covering its content exactly, one run of the
   * base style when the text has none.
   * @param text The text.
   */
  std::vector<TextRun> get_runs(const Text& text);

  /**
   * Returns the run whose style text typed at a caret takes: that of the
   * character before the caret, or of the first character at the start.
   * @param text The text.
   * @param index The caret's index.
   */
  TextRun get_typing_run(const Text& text, std::size_t index);

  /**
   * Returns the style of the first character of a range, or of the text
   * typed there when the range is empty.
   * @param text The text.
   * @param range The range.
   */
  TextStyle get_style(const Text& text, const TextRange& range);

  /**
   * Replaces a range of a text's content, the characters inserted extending
   * the run of the first character replaced, or when nothing is replaced,
   * the run text typed at the start of the range takes. Runs left empty are
   * removed and neighbouring runs left equal are merged. The stored layout
   * is discarded.
   * @param text The text.
   * @param range The range to replace, clamped to the content.
   * @param replacement The characters inserted.
   * @return The edited text.
   */
  Text replace_text(
    const Text& text, TextRange range, std::u16string_view replacement);

  /**
   * Applies a style edit to a range of a text, splitting runs at its ends
   * and merging neighbouring runs left equal. Editing the whole content also
   * edits the base style.
   * @param text The text.
   * @param range The range to style, clamped to the content.
   * @param edit The properties to set.
   * @return The edited text.
   */
  Text edit_text_style(
    const Text& text, TextRange range, const TextStyleEdit& edit);

  /**
   * Applies a paragraph style edit to a text.
   * @param text The text.
   * @param edit The properties to set.
   * @return The edited text.
   */
  Text edit_paragraph_style(const Text& text, const ParagraphStyleEdit& edit);

  /**
   * Tests whether a style edit can change where text breaks and lies.
   * @param edit The edit.
   */
  bool is_layout_affected(const TextStyleEdit& edit);

  /** Where a text node created by an EditTextCommand is inserted. */
  struct TextInsertion {

    /** The parent, or nil for the canvas. */
    NodeId m_parent;

    /** The header of the node, whose id is the command's. */
    NodeHeader m_header;
  };

  /**
   * Sets the content and styles of a text node as typing does, optionally
   * renaming it, inserting it as the topmost child of a parent, or removing
   * it. Labelled Add Text when inserting, Delete when removing, and Edit Text
   * otherwise. Typing into a text node merges into the step that created or
   * last edited it.
   */
  class EditTextCommand : public Command {
    public:

      /**
       * Constructs an EditTextCommand replacing a text node's payload.
       * @param id The text node's id.
       * @param text The new payload.
       * @param name The new name, or none to keep the name.
       */
      EditTextCommand(const NodeId& id, Text text,
        std::optional<std::string> name = std::nullopt) noexcept;

      /**
       * Constructs an EditTextCommand inserting a text node.
       * @param insertion Where the node is inserted and its header.
       * @param text The node's payload.
       */
      EditTextCommand(TextInsertion insertion, Text text) noexcept;

      /**
       * Constructs an EditTextCommand removing a text node.
       * @param id The text node's id.
       */
      explicit EditTextCommand(const NodeId& id) noexcept;

      /** Returns the text node's id. */
      const NodeId& get_node() const;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /** Takes the payload of an EditTextCommand editing the same node. */
      bool merge(const Command& command) override;

      /** Returns the node inserted, or none. */
      std::optional<std::vector<NodeId>> get_selection() const override;

    private:
      NodeId m_id;
      std::optional<TextInsertion> m_insertion;
      std::optional<Text> m_text;
      std::optional<std::string> m_name;
  };

  /**
   * Applies a character style edit to text nodes, or to a range of one,
   * labelled after the property set, such as Change Font Size.
   */
  class SetTextStyleCommand : public Command {
    public:

      /**
       * Constructs a SetTextStyleCommand styling whole text nodes.
       * @param nodes The nodes to change; those that are not text are left
       *        unchanged.
       * @param edit The properties to set.
       * @param layouter Computes the layout stored after an edit that
       *        changes the layout, or empty to discard the stored layout.
       */
      SetTextStyleCommand(std::vector<NodeId> nodes, TextStyleEdit edit,
        TextLayouter layouter = TextLayouter()) noexcept;

      /**
       * Constructs a SetTextStyleCommand styling a range of a text node.
       * @param id The text node's id.
       * @param range The range to style.
       * @param edit The properties to set.
       * @param layouter Computes the layout stored after an edit that
       *        changes the layout, or empty to discard the stored layout.
       */
      SetTextStyleCommand(const NodeId& id, TextRange range,
        TextStyleEdit edit, TextLayouter layouter = TextLayouter()) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /**
       * Takes the properties set by a SetTextStyleCommand changing the same
       * nodes and range.
       */
      bool merge(const Command& command) override;

    private:
      std::vector<NodeId> m_nodes;
      std::optional<TextRange> m_range;
      TextStyleEdit m_edit;
      TextLayouter m_layouter;
  };

  /**
   * Applies a paragraph style edit to text nodes, labelled after the
   * property set, such as Change Alignment.
   */
  class SetParagraphStyleCommand : public Command {
    public:

      /**
       * Constructs a SetParagraphStyleCommand.
       * @param nodes The nodes to change; those that are not text are left
       *        unchanged.
       * @param edit The properties to set.
       * @param layouter Computes the layout stored after the edit, or empty
       *        to discard the stored layout.
       */
      SetParagraphStyleCommand(std::vector<NodeId> nodes,
        ParagraphStyleEdit edit,
        TextLayouter layouter = TextLayouter()) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

      /**
       * Takes the properties set by a SetParagraphStyleCommand changing the
       * same nodes.
       */
      bool merge(const Command& command) override;

    private:
      std::vector<NodeId> m_nodes;
      ParagraphStyleEdit m_edit;
      TextLayouter m_layouter;
  };

  /**
   * Switches text nodes between Auto Width, Auto Height and Fixed Size,
   * keeping where their text appears, labelled Change Text Frame.
   */
  class SetTextFrameCommand : public Command {
    public:

      /**
       * Constructs a SetTextFrameCommand.
       * @param nodes The nodes to change; those that are not text are left
       *        unchanged.
       * @param kind The frame to switch to.
       * @param measurer The function measuring text.
       */
      SetTextFrameCommand(std::vector<NodeId> nodes, TextFrameKind kind,
        TextMeasurer measurer) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::vector<NodeId> m_nodes;
      TextFrameKind m_kind;
      TextMeasurer m_measurer;
  };
}

#endif
