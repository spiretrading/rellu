#ifndef RELLU_UI_TOOL_TIPS_HPP
#define RELLU_UI_TOOL_TIPS_HPP
#include <optional>
#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

namespace Rellu {

  /** Lists where a widget's tool tip opens. */
  enum class ToolTipPlacement {

    /** Below the pointer. */
    POINTER,

    /** Beside the widget's right edge, centred on it vertically. */
    RIGHT
  };

  /**
   * Returns where a tool tip opening at the pointer places its box.
   * @param pointer The pointer's position in logical pixels.
   * @param size The box's size in device pixels.
   * @param ratio The ratio of device pixels to logical pixels.
   * @return The box's top left corner in device pixels.
   */
  QPoint get_pointer_tip_position(QPointF pointer, QSize size, double ratio);

  /**
   * Sets where a widget's tool tip opens.
   * @param widget The widget.
   * @param placement Where its tool tip opens.
   */
  void set_tool_tip_placement(QWidget& widget, ToolTipPlacement placement);

  /**
   * Returns where a widget's tool tip opens.
   * @param widget The widget.
   */
  ToolTipPlacement get_tool_tip_placement(const QWidget& widget);

  /**
   * Shows a tool tip as Adobe XD shows it, through the application's
   * ToolTipFilter.
   * @param target The widget the tool tip describes.
   * @param text The tool tip's text, wrapped as needed.
   * @param position The pointer's global position.
   * @param area The region of the <i>target</i> the tool tip describes,
   *        leaving which hides it, or nothing for the whole target.
   * @return <code>true</code> iff a ToolTipFilter showed the tool tip.
   */
  bool show_tool_tip(QWidget& target, const QString& text, QPoint position,
    std::optional<QRect> area = std::nullopt);

  /**
   * An application event filter showing widgets' tool tips as Adobe XD
   * shows its tool tips.
   */
  class ToolTipFilter : public QObject {
    public:

      /**
       * Constructs a ToolTipFilter.
       * @param parent The filter's parent.
       */
      explicit ToolTipFilter(QObject* parent = nullptr);

      ~ToolTipFilter() override;

      /**
       * Shows a tool tip.
       * @param target The widget the tool tip describes.
       * @param text The tool tip's text.
       * @param position The pointer's global position.
       * @param area The region of the <i>target</i> the tool tip describes.
       */
      void show_text(QWidget& target, const QString& text, QPoint position,
        std::optional<QRect> area);

      bool eventFilter(QObject* watched, QEvent* event) override;

    private:
      QPointer<QWidget> m_target;
      std::optional<QRect> m_area;
      std::optional<QPointF> m_pointer;
      QWidget* m_window;

      bool is_over_target() const;
      void show(QWidget& target, const QString& text, QPointF pointer,
        std::optional<QRect> area);
      void hide();
  };
}

#endif
