#include <QApplication>
#include <QClipboard>
#include <QStringList>
#include <QtPlugin>
#include "Rellu/Ui/Clipboard.hpp"
#include "Rellu/Ui/DocumentSession.hpp"
#include "Rellu/Ui/GreyscaleText.hpp"
#include "Rellu/Ui/PixelHitFilter.hpp"
#include "Rellu/Ui/Theme.hpp"
#include "Rellu/Ui/ToolTips.hpp"
#include "MainWindow.hpp"

Q_IMPORT_PLUGIN(QGifPlugin)
Q_IMPORT_PLUGIN(QJpegPlugin)
Q_IMPORT_PLUGIN(QModernWindowsStylePlugin)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)

using namespace Rellu;

int main(int argc, char** argv) {
  auto application = QApplication(argc, argv);
  application.setOrganizationName("Rellu");
  application.setApplicationName("Rellu");
  set_language(load_language());
  apply_theme(application);
  application.installEventFilter(new PixelHitFilter(&application));
  application.installEventFilter(new GreyscaleTextFilter(&application));
  application.installEventFilter(new ToolTipFilter(&application));
  Clipboard::remove_stale_images(Clipboard::get_default_image_directory(),
    QApplication::clipboard()->mimeData());
  auto window = MainWindow();
  window.show();
  open_launched_document(QApplication::arguments(),
    [&] (const QString& path) {
      return window.open(path);
    },
    [&] {
      return window.make_new();
    });
  return application.exec();
}
