#ifndef RELLU_HOME_FRAME_HPP
#define RELLU_HOME_FRAME_HPP
#include <optional>
#include <vector>
#include <QAbstractButton>
#include <QByteArray>
#include <QPoint>
#include <QRect>
#include <QWidget>
#include "Rellu/Ui/HomeScreen.hpp"

namespace Rellu {

  /**
   * The Home screen in a window drawing its own title bar, as Adobe XD's
   * Home window does: its minimize, maximize and close buttons in the white
   * header, which moves the window.
   */
  class HomeFrame : public HomeScreen {
    public:

      /**
       * Constructs a HomeFrame.
       * @param parent The parent widget.
       */
      explicit HomeFrame(QWidget* parent = nullptr);

      /**
       * Places the top left corner of the window's frame when it is next
       * shown.
       * @param position The position in screen device pixels.
       */
      void place_at(const QPoint& position);

    protected:
      void resizeEvent(QResizeEvent* event) override;
      void changeEvent(QEvent* event) override;
      bool nativeEvent(
        const QByteArray& type, void* message, qintptr* result) override;

    private:
      std::vector<QWidget*> m_caption_buttons;
      QAbstractButton* m_maximize;
      std::optional<QPoint> m_position;

      bool is_caption(QPoint position) const;
      void place_caption_buttons();
  };

  /**
   * Places Home before it is shown as Adobe XD places its Home window: over
   * a document window, cascaded from the window's normal top left corner,
   * and at launch, at the top left corner of the last document window.
   * @param screen The Home screen, a HomeFrame.
   * @param window The document window Home is shown over, or
   *        <code>nullptr</code> at launch.
   * @param launch The bounds of the last document window's frame in screen
   *        device pixels, or an empty QRect if there are none.
   */
  void place_home_frame(
    HomeScreen& screen, const QWidget* window, const QRect& launch);
}

#endif
