#ifndef RELLU_UI_TOOL_TIPS_HPP
#define RELLU_UI_TOOL_TIPS_HPP
#include <optional>
#include <QObject>
#include <QPointF>
#include <QRect>
#include <QPointer>
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
