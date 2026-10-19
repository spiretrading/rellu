#ifndef RELLU_UI_PREVIEW_WINDOW_HPP
#define RELLU_UI_PREVIEW_WINDOW_HPP
#include <functional>
#include <memory>
#include <vector>
#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Subscription.hpp"
#include "Rellu/Prototype/PreviewComposer.hpp"
#include "Rellu/Prototype/PreviewPlayer.hpp"
#include "Rellu/Rendering/Canvas.hpp"

namespace Rellu {

  /**
   * Returns the artboard Adobe XD's Desktop Preview starts on: the artboard
   * of the selection, otherwise the start of the first flow, otherwise the
   * home artboard, otherwise the first artboard.
   * @param document The document previewed.
   * @param selection The selected nodes.
   * @return The artboard, or nil if the document has none.
   */
  NodeId find_preview_start(
    const Document& document, const std::vector<NodeId>& selection);

  /**
   * Adobe XD's Desktop Preview: a window playing a document's prototype,
   * sized to the artboard shown and drawing its frames on the GPU at the
   * screen's refresh rate while something animates. The pointer, wheel and
   * keys go to the PreviewPlayer, and the preview follows the document's
   * changes, keeping its place.
   */
  class PreviewWindow : public QWidget {
    public:

      /** The interval in milliseconds between animated frames. */
      static constexpr auto FRAME_INTERVAL = 16;

      /** The delay in milliseconds before following a document change. */
      static constexpr auto CHANGE_DELAY = 150;

      /**
       * Constructs a PreviewWindow playing nothing.
       * @param parent The parent widget.
       */
      explicit PreviewWindow(QWidget* parent = nullptr);

      ~PreviewWindow() override;

      /**
       * Plays a document from an artboard.
       * @param document The document, which must outlive the preview or be
       *        replaced first.
       * @param artboard The artboard to start on.
       */
      void play(Document& document, const NodeId& artboard);

      /** Stops playing, releasing the document. */
      void stop();

      /** Returns the document played, or <code>nullptr</code> if none. */
      Document* get_document() const;

      /** Returns the player, or <code>nullptr</code> if nothing plays. */
      PreviewPlayer* get_player() const;

      /** Returns the view drawing the frames. */
      Canvas& get_view() const;

      /** Returns the number of logical units per viewport unit. */
      double get_zoom() const;

      /**
       * Maps a point of the view to the preview's viewport.
       * @param point The point in the view's logical units.
       */
      Point to_viewport(Point point) const;

      /** Follows the document's changes at once, keeping the place shown. */
      void reload();

      /**
       * Advances the preview's time and draws the frame reached.
       * @param seconds The time elapsed.
       */
      void advance(double seconds);

      /** Composes the current frame for the view to draw. */
      void update_frame();

      /**
       * Sets the function called when the window closes.
       * @param handler The function.
       */
      void set_close_handler(std::function<void ()> handler);

    protected:
      void resizeEvent(QResizeEvent* event) override;
      void closeEvent(QCloseEvent* event) override;

    private:
      class View;
      Document* m_document;
      std::unique_ptr<PreviewPlayer> m_player;
      std::unique_ptr<PreviewComposer> m_composer;
      View* m_view;
      Subscription m_subscription;
      QTimer m_frames;
      QTimer m_changes;
      QElapsedTimer m_clock;
      std::function<void ()> m_close_handler;
      NodeId m_shown;
      double m_zoom;
      Point m_offset;

      void rebuild(const PreviewPosition& position);
      void fit();
      void resize_to_artboard();
      void update_timer();
      void handle_changed(bool is_changed);
  };
}

#endif
