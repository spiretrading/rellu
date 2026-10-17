#include <QApplication>
#include <QMainWindow>
#include "Gesso/Canvas.hpp"

using namespace Gesso;

int main(int argc, char** argv) {
  auto application = QApplication(argc, argv);
  application.setApplicationName("Gesso");
  auto window = QMainWindow();
  window.setWindowTitle("Gesso");
  window.setCentralWidget(new Canvas());
  window.resize(1280, 800);
  window.show();
  return application.exec();
}
