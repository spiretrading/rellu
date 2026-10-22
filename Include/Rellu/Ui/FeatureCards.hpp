#ifndef RELLU_UI_FEATURE_CARDS_HPP
#define RELLU_UI_FEATURE_CARDS_HPP
#include <optional>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QTimer>
#include <QWidget>

namespace Rellu {

  /** Lists the features Adobe XD introduces with a hover card. */
  enum class Feature {

    /** Previewing on a connected device. */
    DEVICE_PREVIEW,

    /** Previewing on the desktop. */
    DESKTOP_PREVIEW,

    /** The Assets panel. */
    ASSETS_PANEL,

    /** The Layers panel. */
    LAYERS_PANEL,

    /** The Plugins panel. */
    PLUGINS_PANEL,

    /** Repeat grids. */
    REPEAT_GRID,

    /** Components. */
    COMPONENTS,

    /** The Transform section. */
    TRANSFORM,

    /** The Layout section. */
    LAYOUT,

    /** Prototype interactions. */
    INTERACTION,

    /** The type of an interaction's action. */
    INTERACTION_ACTION_TYPE
  };

  /** Lists where a hover card opens around its anchor. */
  enum class CardPlacement {

    /** Below the anchor, right edges aligned. */
    BOTTOM_RIGHT,

    /** Below the anchor, left edges aligned. */
    BOTTOM_LEFT,

    /** Right of the anchor, bottom edges aligned. */
    RIGHT_BOTTOM,

    /** Left of the anchor, top edges aligned. */
    LEFT_TOP,

    /** Above the anchor, right edges aligned. */
    TOP_RIGHT
  };

  /** Stores what a feature's hover card shows and where it opens. */
  struct FeatureCard {

    /** The card's title. */
    QString m_title;

    /** The card's description. */
    QString m_description;

    /** Where the card opens around its anchor. */
    CardPlacement m_placement;

    /** The gap between the card and its anchor in logical pixels. */
    double m_offset;

    /** Whether clicking the anchor leaves a pending card to open. */
    bool m_is_click_ignored;
  };

  /**
   * Returns a feature's hover card in the language of the user interface.
   * @param feature The feature.
   */
  FeatureCard get_feature_card(Feature feature);

  /**
   * Makes a widget open a feature's hover card in place of its tool tip.
   * @param widget The widget.
   * @param feature The feature.
   */
  void set_feature(QWidget& widget, Feature feature);

  /**
   * Returns the feature whose hover card a widget opens, if any.
   * @param widget The widget.
   */
  std::optional<Feature> get_feature(const QWidget& widget);

  /**
   * Returns the rectangle a feature's hover card opens around, given the
   * rectangle of the widget it describes.
   * @param feature The feature.
   * @param widget The widget's rectangle.
   */
  QRectF get_card_anchor(Feature feature, const QRectF& widget);

  /**
   * Returns where a hover card opens around its anchor.
   * @param card The card.
   * @param anchor The anchor's rectangle.
   * @param size The card's size.
   * @return The card's top left corner.
   */
  QPointF get_card_position(
    const FeatureCard& card, const QRectF& anchor, const QSizeF& size);

  /**
   * An application event filter opening the hover cards of the widgets
   * given a feature, with Adobe XD's timing.
   */
  class FeatureCardFilter : public QObject {
    public:

      /** The time the pointer rests on an anchor before its card opens. */
      static constexpr auto SHOW_DELAY = 1000;

      /** The time a card stays open after the pointer leaves it. */
      static constexpr auto CLOSE_DELAY = 500;

      /** The longest time a card stays open. */
      static constexpr auto LIFETIME = 15000;

      /**
       * Constructs a FeatureCardFilter.
       * @param parent The filter's parent.
       */
      explicit FeatureCardFilter(QObject* parent = nullptr);

      ~FeatureCardFilter() override;

      /** Returns the open card's window, or <code>nullptr</code>. */
      QWidget* get_card() const;

      bool eventFilter(QObject* watched, QEvent* event) override;

    private:
      QPointer<QWidget> m_anchor;
      QPointer<QWidget> m_pending;
      QWidget* m_window;
      QTimer m_show_timer;
      QTimer m_close_timer;
      QTimer m_lifetime_timer;

      void show_card();
      void close();
      bool is_card(QObject* object) const;
  };
}

#endif
