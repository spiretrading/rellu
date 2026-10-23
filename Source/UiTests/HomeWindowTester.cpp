#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <doctest/doctest.h>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QMouseEvent>
#include <QRect>
#include <QWidget>
#include "Rellu/Commands/ArtboardPresets.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Ui/DocumentWindows.hpp"
#include "Rellu/Ui/HomeWindow.hpp"

using namespace Rellu;

namespace {
  class TestWindow : public QWidget, public DocumentWindow {
    public:
      DocumentWindows* m_windows;
      QString m_path;
      std::unique_ptr<Document> m_document;
      const QWidget* m_host;

      TestWindow(DocumentWindows& windows, QString path,
          std::unique_ptr<Document> document)
        : m_windows(&windows),
          m_path(std::move(path)),
          m_document(std::move(document)),
          m_host(nullptr) {}

      QWidget& get_widget() override {
        return *this;
      }

      QString get_path() const override {
        return m_path;
      }

      QString get_name() const override {
        return m_path;
      }

      void place(const QRect&) override {}

      void place(const QWidget& host) override {
        m_host = &host;
      }

    protected:
      void closeEvent(QCloseEvent* event) override {
        m_windows->remove(*this);
        QWidget::closeEvent(event);
      }
  };

  struct Fixture {
    DocumentWindows m_windows;
    std::vector<TestWindow*> m_made;
    std::unique_ptr<HomeWindow> m_home;
    int m_quits;

    Fixture()
        : m_quits(0) {
      m_windows.set_empty_handler([] {});
      m_windows.set_factory([this] (const QString& path, auto document) {
        auto window = std::make_unique<TestWindow>(
          m_windows, path, std::move(document));
        m_made.push_back(window.get());
        return std::unique_ptr<DocumentWindow>(std::move(window));
      });
      m_home = std::make_unique<HomeWindow>(m_windows);
      m_home->set_quit_handler([this] {
        ++m_quits;
      });
    }
  };

  void click(QWidget& widget) {
    auto position = QPointF(widget.width() / 2.0, widget.height() / 2.0);
    auto press = QMouseEvent(QEvent::MouseButtonPress, position,
      widget.mapToGlobal(position), Qt::LeftButton, Qt::LeftButton,
      Qt::NoModifier);
    QApplication::sendEvent(&widget, &press);
    auto release = QMouseEvent(QEvent::MouseButtonRelease, position,
      widget.mapToGlobal(position), Qt::LeftButton, Qt::NoButton,
      Qt::NoModifier);
    QApplication::sendEvent(&widget, &release);
  }

  QWidget& find(QWidget& parent, const QString& name) {
    auto child = parent.findChild<QWidget*>(name);
    REQUIRE(child);
    return *child;
  }

  void flush() {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();
  }
}

TEST_SUITE("HomeWindow") {
  TEST_CASE("show") {
    auto fixture = Fixture();
    auto& home = *fixture.m_home;
    REQUIRE(!home.is_shown());
    home.show(QByteArray());
    REQUIRE(home.is_shown());
    REQUIRE(home.get_screen().size() ==
      QSize(HomeWindow::DEFAULT_WIDTH, HomeWindow::DEFAULT_HEIGHT));
    REQUIRE(!home.get_screen().is_back_enabled());
    home.hide();
    auto saved = QWidget();
    saved.setGeometry(100, 120, 800, 600);
    home.show(saved.saveGeometry());
    REQUIRE(home.get_screen().size() == QSize(800, 600));
  }

  TEST_CASE("create") {
    auto previous = get_last_used_preset();
    auto fixture = Fixture();
    auto& home = *fixture.m_home;
    home.show(QByteArray());
    auto preset = home.get_screen().get_shown_preset(HomePresetType::MOBILE);
    home.get_screen().activate(HomePresetType::MOBILE);
    REQUIRE(fixture.m_made.size() == 1);
    auto& made = *fixture.m_made[0];
    REQUIRE(made.m_host == &home.get_screen());
    REQUIRE(made.m_path.isEmpty());
    auto& canvas = made.m_document->get_canvas();
    REQUIRE(canvas.size() == 1);
    REQUIRE(made.m_document->get(canvas.front()).m_header.m_name ==
      preset.m_name.toStdString() + " \xE2\x80\x93 1");
    REQUIRE(!home.is_shown());
    REQUIRE(home.get_screen().is_back_enabled());
    set_last_used_preset(previous);
  }

  TEST_CASE("new_file") {
    auto previous = get_last_used_preset();
    auto fixture = Fixture();
    auto& home = *fixture.m_home;
    home.show(QByteArray());
    click(find(home.get_screen(), "new_file"));
    REQUIRE(fixture.m_made.size() == 1);
    REQUIRE(fixture.m_made[0]->m_host == &home.get_screen());
    REQUIRE(fixture.m_made[0]->m_document->get_canvas().size() == 1);
    REQUIRE(!home.is_shown());
    set_last_used_preset(previous);
  }

  TEST_CASE("open") {
    auto fixture = Fixture();
    auto& home = *fixture.m_home;
    auto requested = std::optional<QString>();
    home.set_open_dialog([&] (QWidget& parent) {
      REQUIRE(&parent == &home.get_screen());
      requested = "a.xd";
      return *requested;
    });
    home.show(QByteArray());
    click(find(home.get_screen(), "open"));
    REQUIRE(requested);
    REQUIRE(fixture.m_made.size() == 1);
    REQUIRE(fixture.m_made[0]->m_path == "a.xd");
    REQUIRE(fixture.m_made[0]->m_host == &home.get_screen());
    REQUIRE(!home.is_shown());
    REQUIRE(home.get_screen().get_recent_paths() ==
      QStringList({QDir::toNativeSeparators(
        QDir::current().absoluteFilePath("a.xd"))}));
  }

  TEST_CASE("forwarded_file") {
    auto fixture = Fixture();
    auto& home = *fixture.m_home;
    home.show(QByteArray());
    REQUIRE(home.open("a.xd"));
    REQUIRE(fixture.m_made[0]->m_host == &home.get_screen());
    REQUIRE(!home.is_shown());
    home.show_over(*fixture.m_made[0]);
    REQUIRE(home.open("b.xd"));
    REQUIRE(!fixture.m_made[1]->m_host);
    REQUIRE(home.is_shown());
    REQUIRE(home.open("a.xd"));
    REQUIRE(fixture.m_made.size() == 2);
  }

  TEST_CASE("back") {
    auto fixture = Fixture();
    auto& home = *fixture.m_home;
    fixture.m_windows.make_new();
    auto& window = *fixture.m_made[0];
    window.setGeometry(200, 200, 1400, 900);
    home.show_over(window);
    REQUIRE(home.is_shown());
    REQUIRE(home.get_screen().size() ==
      QSize(HomeWindow::DEFAULT_WIDTH, HomeWindow::DEFAULT_HEIGHT));
    REQUIRE(home.get_screen().is_back_enabled());
    click(find(home.get_screen(), "back"));
    REQUIRE(!home.is_shown());
    REQUIRE(fixture.m_windows.get_active() == &window);
  }

  TEST_CASE("close") {
    auto fixture = Fixture();
    auto& home = *fixture.m_home;
    fixture.m_windows.make_new();
    home.show_over(*fixture.m_made[0]);
    REQUIRE(home.get_screen().close());
    REQUIRE(!home.is_shown());
    REQUIRE(fixture.m_quits == 0);
    fixture.m_made[0]->close();
    flush();
    REQUIRE(fixture.m_windows.get_windows().empty());
    REQUIRE(!home.get_screen().is_back_enabled());
    home.show(QByteArray());
    REQUIRE(home.get_screen().close());
    REQUIRE(fixture.m_quits == 1);
  }
}
