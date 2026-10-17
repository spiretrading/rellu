#include <QApplication>
#include <QMainWindow>
#include "Rellu/Canvas.hpp"

using namespace Rellu;

int main(int argc, char** argv) {
  auto application = QApplication(argc, argv);
  application.setApplicationName("Rellu");
  auto window = QMainWindow();
  window.setWindowTitle("Rellu");
  window.setCentralWidget(new Canvas());
  window.resize(1280, 800);
  window.show();
  return application.exec();
}
