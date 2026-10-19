#ifndef RELLU_TOOLS_TEXT_TOOL_HPP
#define RELLU_TOOLS_TEXT_TOOL_HPP
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <QImage>
#include "Rellu/Commands/TextCommands.hpp"
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Geometry/Size.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Text/FontLibrary.hpp"
#include "Rellu/Text/TextLayout.hpp"
#include "Rellu/Tools/Tool.hpp"

namespace Rellu {

  /** A composition update from an input method. */
  struct InputMethodEvent {

    /** The text the composition commits, in UTF-16. */
    std::u16string m_commit;

    /** The text being composed, in UTF-16, empty when composition ends. */
    std::u16string m_preedit;

    /**
     * The position of the caret within the text being composed, or
     * negative to hide the caret.
     */
    int m_cursor = 0;

    /**
     * The offset from the caret of the start of the text the commit
     * replaces.
     */
    int m_replacement_start = 0;

    /** The number of UTF-16 code units the commit replaces. */
    int m_replacement_length = 0;
  };

  /**
   * Finds the caret index nearest a point over a text node.
   * @param context The context worked in.
   * @param id The text node.
   * @param position The point in logical units.
   * @param library The fonts the text is laid out with.
   * @return The caret index, or 0 if there is no such text node.
   */
  std::size_t find_text_caret(const ToolContext& context, const NodeId& id,
    Point position, const FontLibrary& library);

  /**
   * Creates and edits text as Adobe XD's text tool does. A click creates
   * point text whose first baseline starts at the click and a drag creates
   * Auto Height text as wide as the drag, both taking the font, character
   * spacing, color and paragraph style of the text edited or styled most
   * recently. A click on existing text edits it, or when the text lies in a
   * container not entered and Ctrl is up, enters the container and selects
   * the text; Enter edits the selected text, all of it selected. While
   * editing, the pointer places the caret and selects by character, a
   * double click selects a word or punctuation mark with the spaces
   * following it and a triple click a paragraph; keys move the caret by
   * character, word, line and text, Shift extending the selection; typing,
   * deleting, the clipboard and input methods edit the selection, text
   * typed at a caret taking the TextSelection's pending style and hyperlink
   * if set, and typing a run of characters is one undoable step until the
   * caret moves. Enter breaks the paragraph and Shift+Enter inserts a
   * vertical tab, which also starts a paragraph. The selection is
   * highlighted in solid blue with its characters drawn in white. Editing
   * ends, removing the text if it is empty, on Escape or a click outside
   * the text, leaving the text selected, and on selecting other nodes.
   * Ending the editing of text handed over by another tool requests the
   * tool named select, while text this tool began editing leaves this tool
   * active. The text edited and its selection are the editor's
   * TextSelection, so that another tool hands text over for editing by
   * setting it and requesting this tool.
   */
  class TextTool : public Tool {
    public:

      /** The distance in logical units a press moves before it drags. */
      static constexpr auto DRAG_THRESHOLD = 3.0;

      /** The distance in logical units within which text is hit. */
      static constexpr auto HIT_TOLERANCE = 3.0;

      /** The longest time between the clicks of a triple click. */
      static constexpr auto TRIPLE_CLICK_INTERVAL =
        std::chrono::milliseconds(500);

      /** The color the selected range is highlighted in. */
      static constexpr auto HIGHLIGHT_COLOR = make_color(0x33, 0x99, 0xFF);

      /** The color the selected characters are drawn in. */
      static constexpr auto HIGHLIGHTED_TEXT_COLOR =
        make_color(0xFF, 0xFF, 0xFF);

      /**
       * Constructs a TextTool.
       * @param library The fonts text is laid out with, which must outlive
       *        this TextTool.
       */
      explicit TextTool(const FontLibrary& library) noexcept;

      /**
       * Returns <code>true</code> iff a text node is being edited.
       * @param context The context worked in.
       */
      bool is_editing(const ToolContext& context) const;

      /**
       * Starts editing a text node.
       * @param context The context worked in.
       * @param id The text node.
       * @param anchor The index the selection extends from.
       * @param caret The index of the caret.
       */
      void edit(ToolContext& context, const NodeId& id, std::size_t anchor,
        std::size_t caret);

      /**
       * Ends editing, removing the text node if it is empty.
       * @param context The context worked in.
       */
      void commit(ToolContext& context);

      /**
       * Inserts text in place of the selection, as typing does.
       * @param context The context worked in.
       * @param text The text inserted, in UTF-16.
       */
      void insert(ToolContext& context, std::u16string_view text);

      /**
       * Applies a composition update from an input method.
       * @param context The context worked in.
       * @param event The update.
       */
      void input_method(ToolContext& context, const InputMethodEvent& event);

      /**
       * Returns the range of the text being composed by an input method, if
       * any.
       */
      std::optional<TextRange> get_preedit() const;

      /**
       * Tests whether a key is used while editing, so that it reaches the
       * tool rather than an application shortcut.
       * @param context The context worked in.
       * @param event The key event.
       */
      bool is_text_key(const ToolContext& context, const KeyEvent& event) const;

      /**
       * Returns the rect along which the caret is drawn, in logical units,
       * or none when not editing.
       * @param context The context worked in.
       */
      std::optional<Rect> get_caret_rect(const ToolContext& context) const;

      /** Returns <code>true</code> iff the blinking caret is shown. */
      bool is_caret_visible() const;

      /** Shows the blinking caret if hidden, otherwise hides it. */
      void blink();

      void press(ToolContext& context, const PointerEvent& event) override;
      void move(ToolContext& context, const PointerEvent& event) override;
      void release(ToolContext& context, const PointerEvent& event) override;
      void double_click(
        ToolContext& context, const PointerEvent& event) override;
      bool key_press(ToolContext& context, const KeyEvent& event) override;
      void cancel(ToolContext& context) override;
      bool is_busy() const override;
      ToolCursor get_cursor() const override;
      void draw(
        const ToolContext& context, CanvasOverlay& overlay) const override;

    private:
      enum class Gesture : unsigned char {
        NONE,
        PENDING,
        FRAME,
        SELECT
      };
      enum class EditKind : unsigned char {
        NONE,
        INSERT,
        DELETE
      };
      struct Geometry {
        NodeId m_id;
        std::optional<std::uint64_t> m_revision;
        std::vector<LineGeometry> m_lines;
        Rect m_bounds = Rect(0, 0, 0, 0);
        std::optional<std::vector<TextFill>> m_fills;
        std::optional<Rect> m_clip;
      };
      struct Highlight {
        NodeId m_id;
        std::optional<std::uint64_t> m_revision;
        TextRange m_range;
        Matrix m_transform;
        Size m_size = Size(0, 0);
        std::shared_ptr<const QImage> m_image;
        Point m_position = Point(0, 0);
      };
      const FontLibrary* m_library;
      Gesture m_gesture;
      Point m_press;
      Point m_press_document;
      Point m_current_document;
      EditKind m_last_edit;
      std::optional<double> m_goal;
      std::optional<TextRange> m_preedit;
      int m_preedit_cursor;
      bool m_is_caret_visible;
      std::optional<std::chrono::steady_clock::time_point> m_double_click;
      Point m_double_click_position;
      NodeId m_session;
      mutable Geometry m_geometry;
      mutable Highlight m_highlight;

      const Text* find_text(const ToolContext& context) const;
      void sync(ToolContext& context);
      const Geometry& get_geometry(const ToolContext& context) const;
      Matrix get_node_transform(const ToolContext& context) const;
      std::optional<Point> to_local(
        const ToolContext& context, Point position) const;
      bool is_inside(const ToolContext& context, Point position) const;
      std::size_t hit(const ToolContext& context, Point position) const;
      TextRange get_range(const ToolContext& context) const;
      void select(ToolContext& context, std::size_t anchor,
        std::size_t caret, bool is_moving);
      void move_caret(
        ToolContext& context, std::size_t caret, bool is_extending);
      std::optional<OverlayImage> get_highlight(
        const ToolContext& context, TextRange range) const;
      Text type(const ToolContext& context, const Text& text, TextRange range,
        std::u16string_view inserted) const;
      void replace(ToolContext& context, TextRange range,
        std::u16string_view text, EditKind kind);
      void apply(ToolContext& context, Text text, std::size_t caret,
        EditKind kind);
      void create(ToolContext& context, Rect frame, bool is_area);
      void end(ToolContext& context);
      void finish(ToolContext& context);
      void copy(const ToolContext& context) const;
      bool navigate(ToolContext& context, const KeyEvent& event);
  };
}

#endif
