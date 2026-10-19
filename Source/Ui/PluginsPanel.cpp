#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>
#include "Rellu/Ui/PluginsPanel.hpp"
#include "Rellu/Ui/Translation.hpp"
#include "Icons.hpp"

using namespace Rellu;

PluginsPanel::PluginsPanel(QWidget* parent)
    : QWidget(parent) {
  setObjectName("plugins_panel");
  auto layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  auto header = new QWidget();
  auto header_layout = new QHBoxLayout(header);
  header_layout->setContentsMargins(16, 12, 16, 0);
  auto title = new QLabel(translate("plugins", "PLUGINS"));
  title->setObjectName("panel_heading");
  header_layout->addWidget(title, 1);
  auto add = new QToolButton();
  add->setObjectName("panel_button");
  add->setAutoRaise(true);
  add->setFocusPolicy(Qt::NoFocus);
  add->setIcon(get_icon(Glyph::PLUS));
  add->setIconSize(QSize(16, 16));
  add->setFixedSize(24, 24);
  add->setToolTip(translate("plugins", "Browse Plugins"));
  add->setEnabled(false);
  header_layout->addWidget(add);
  layout->addWidget(header);
  layout->addStretch(1);
}
