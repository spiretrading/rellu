#ifndef RELLU_UI_GREYSCALE_TEXT_HPP
#define RELLU_UI_GREYSCALE_TEXT_HPP
#include <QObject>

namespace Rellu {

  /**
   * An application event filter making all text the application paints
   * greyscale antialiased, as Adobe XD's user interface text is.
   */
  class GreyscaleTextFilter : public QObject {
    public:

      /**
       * Constructs a GreyscaleTextFilter.
       * @param parent The filter's parent.
       */
      explicit GreyscaleTextFilter(QObject* parent = nullptr);

      bool eventFilter(QObject* watched, QEvent* event) override;

    private:
      bool m_is_refresh_pending;

      void refresh();
  };

  /**
   * Makes every loaded font render greyscale antialiased.
   * @return <code>true</code> iff a font changed.
   */
  bool make_greyscale_fonts();
}

#endif
