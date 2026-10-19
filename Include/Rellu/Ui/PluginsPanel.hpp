#ifndef RELLU_UI_PLUGINS_PANEL_HPP
#define RELLU_UI_PLUGINS_PANEL_HPP
#include <QWidget>

namespace Rellu {

  /** The Plugins panel listing the installed plugins. */
  class PluginsPanel : public QWidget {
    public:

      /**
       * Constructs a PluginsPanel.
       * @param parent The parent widget.
       */
      explicit PluginsPanel(QWidget* parent = nullptr);
  };
}

#endif
