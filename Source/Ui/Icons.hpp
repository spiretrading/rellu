#ifndef RELLU_UI_ICONS_HPP
#define RELLU_UI_ICONS_HPP
#include <QColor>
#include <QIcon>
#include "Rellu/Model/Node.hpp"

namespace Rellu {

  /** Lists the monochrome glyphs painted for icons. */
  enum class Glyph : unsigned char {
    SELECT,
    RECTANGLE,
    ELLIPSE,
    POLYGON,
    LINE,
    PEN,
    TEXT,
    ARTBOARD,
    ZOOM,
    ASSETS,
    LAYERS,
    PLUGINS,
    HOME,
    INVITE,
    DEVICE,
    PLAY,
    CHEVRON_DOWN,
    CHEVRON_LEFT,
    CHEVRON_RIGHT,
    SEARCH,
    FILTER,
    SORT,
    LIST_VIEW,
    GRID_VIEW,
    SHARE,
    PLUS,
    INFO,
    ALIGN_TOP,
    ALIGN_MIDDLE,
    ALIGN_BOTTOM,
    DISTRIBUTE_HORIZONTALLY,
    ALIGN_LEFT,
    ALIGN_CENTER,
    ALIGN_RIGHT,
    DISTRIBUTE_VERTICALLY,
    REPEAT_GRID,
    BOOLEAN_ADD,
    BOOLEAN_SUBTRACT,
    BOOLEAN_INTERSECT,
    BOOLEAN_EXCLUDE,
    CUBE,
    ROTATION,
    LOCK_ASPECT,
    FLIP_HORIZONTAL,
    FLIP_VERTICAL,
    SCROLL_HORIZONTAL,
    SCROLL_VERTICAL,
    SCROLL_PANNING,
    PORTRAIT,
    LANDSCAPE,
    CORNERS_SAME,
    CORNERS_DIFFERENT,
    EYEDROPPER,
    STROKE_INNER,
    STROKE_OUTER,
    STROKE_CENTER,
    CAP_BUTT,
    CAP_ROUND,
    CAP_PROJECTING,
    JOIN_MITER,
    JOIN_ROUND,
    JOIN_BEVEL,
    CHARACTER_SPACING,
    LINE_SPACING,
    PARAGRAPH_SPACING,
    TEXT_LEFT,
    TEXT_CENTER,
    TEXT_RIGHT,
    AUTO_WIDTH,
    AUTO_HEIGHT,
    FIXED_SIZE,
    UPPERCASE,
    LOWERCASE,
    TITLE_CASE,
    SUPERSCRIPT,
    SUBSCRIPT,
    UNDERLINE,
    STRIKETHROUGH,
    HYPERLINK,
    MARGINS_LINKED,
    MARGINS_SEPARATE,
    LAYER_ARTBOARD,
    LAYER_RECTANGLE,
    LAYER_ELLIPSE,
    LAYER_POLYGON,
    LAYER_LINE,
    LAYER_PATH,
    LAYER_BOOLEAN,
    LAYER_TEXT,
    LAYER_GROUP,
    LAYER_OPEN_GROUP,
    LAYER_PASTEBOARD,
    LAYER_COMPONENT,
    LAYER_REFERENCE,
    EYE,
    EYE_CLOSED,
    LOCK
  };

  /**
   * Returns the icon painting a glyph: grey, blue when checked and light
   * grey when disabled, crisp at any size and scale.
   * @param glyph The glyph to paint.
   */
  const QIcon& get_icon(Glyph glyph);

  /**
   * Returns the icon painting a glyph in a single color.
   * @param glyph The glyph to paint.
   * @param color The color to paint it in.
   */
  QIcon make_icon(Glyph glyph, const QColor& color);

  /**
   * Returns the icon listing a node in the layers panel.
   * @param node The node displayed.
   * @param is_reference Whether the node listed is a reference.
   */
  const QIcon& get_layer_icon(const Node& node, bool is_reference);

  /** Returns the icon of an expanded group in the layers panel. */
  const QIcon& get_open_group_icon();

  /** Returns the icon of the Pasteboard row in the layers panel. */
  const QIcon& get_pasteboard_icon();

  /**
   * Returns the icon of a kind of node.
   * @param kind The kind of node displayed.
   * @param is_reference Whether the node is a reference.
   */
  const QIcon& get_kind_icon(NodeKind kind, bool is_reference);

  /**
   * Returns the icon showing whether a node is visible.
   * @param is_visible Whether the node is visible.
   */
  const QIcon& get_visibility_icon(bool is_visible);

  /** Returns the icon showing that a node is locked. */
  const QIcon& get_lock_icon();
}

#endif
