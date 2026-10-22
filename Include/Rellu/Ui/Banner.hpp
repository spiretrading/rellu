#ifndef RELLU_UI_BANNER_HPP
#define RELLU_UI_BANNER_HPP
#include <chrono>
#include <memory>
#include <vector>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QTimer>
#include <QWidget>

namespace Rellu {

  /** A piece of a message in Adobe XD's toast markup. */
  struct BannerSpan {

    /** The text shown. */
    QString m_text;

    /** Whether the text is the label of a link. */
    bool m_is_link = false;

    bool operator ==(const BannerSpan&) const = default;
  };

  /**
   * Splits a message in Adobe XD's toast markup, where a link is written
   * [label|target], into its text and the labels of its links, dropping
   * the targets.
   * @param message The message.
   * @return The pieces in order, adjacent text merged.
   */
  std::vector<BannerSpan> parse_banner_message(const QString& message);

  /**
   * A message shown as Adobe XD shows its toasts: a blue banner along the
   * bottom of the window, its text on one line with its links underlined,
   * and a close button, either of which dismisses it.
   */
  class Banner : public QWidget {
    public:

      /** The height of a banner in logical pixels. */
      static constexpr auto HEIGHT = 48;

      /** The largest width of a banner in logical pixels. */
      static constexpr auto MAXIMUM_WIDTH = 2000;

      /**
       * The width in logical pixels of the window that a banner leaves
       * uncovered beside it.
       */
      static constexpr auto MARGIN = 116;

      /**
       * Constructs a Banner.
       * @param message The message, in Adobe XD's toast markup.
       * @param parent The window's widget the banner is shown over.
       */
      Banner(const QString& message, QWidget* parent);

      /** Returns the message, in Adobe XD's toast markup. */
      const QString& get_message() const;

      /**
       * Sets the message.
       * @param message The message, in Adobe XD's toast markup.
       */
      void set_message(const QString& message);

      /** Returns the region of the close button. */
      QRectF get_close_region() const;

      /** Returns the regions of the links, in order. */
      std::vector<QRectF> get_link_regions() const;

      /** Returns the position of the start of the text's baseline. */
      QPointF get_text_origin() const;

      /** Returns <code>true</code> iff the banner was dismissed. */
      bool is_dismissed() const;

      /** Dismisses the banner, hiding it and deleting it later. */
      void dismiss();

    protected:
      void paintEvent(QPaintEvent* event) override;
      void mouseMoveEvent(QMouseEvent* event) override;
      void mouseReleaseEvent(QMouseEvent* event) override;
      void leaveEvent(QEvent* event) override;

    private:
      QString m_message;
      std::vector<BannerSpan> m_spans;
      int m_hover;
      bool m_is_dismissed;

      int find_target(QPointF position) const;
  };

  /**
   * Shows one banner at a time at the bottom center of a window's widget,
   * as Adobe XD shows its toasts: a message appears after a delay and
   * stays for a time, a message shown meanwhile replacing it and starting
   * the time again.
   */
  class BannerHost {
    public:

      /** How long after being shown a message appears. */
      static constexpr auto DELAY = std::chrono::milliseconds(850);

      /** How long a message stays once it appears. */
      static constexpr auto DURATION = std::chrono::milliseconds(11750);

      /**
       * Constructs a BannerHost with Adobe XD's delay and duration.
       * @param host The widget banners are shown over, which must outlive
       *        this object.
       */
      explicit BannerHost(QWidget& host);

      /**
       * Constructs a BannerHost.
       * @param host The widget banners are shown over, which must outlive
       *        this object.
       * @param delay How long after being shown a message appears.
       * @param duration How long a message stays once it appears.
       */
      BannerHost(QWidget& host, std::chrono::milliseconds delay,
        std::chrono::milliseconds duration);

      ~BannerHost();

      /**
       * Shows a message once the delay passes.
       * @param message The message, in Adobe XD's toast markup.
       */
      void show(const QString& message);

      /** Returns the banner shown, or <code>nullptr</code> if none is. */
      Banner* get_banner() const;

      /** Places the banner after the host widget changes size. */
      void place();

    private:
      QWidget* m_host;
      std::chrono::milliseconds m_duration;
      QPointer<Banner> m_banner;
      QString m_pending;
      QTimer m_delay;
      QTimer m_timer;
      std::unique_ptr<QObject> m_filter;

      BannerHost(const BannerHost&) = delete;
      BannerHost& operator =(const BannerHost&) = delete;
      void reveal();
  };
}

#endif
