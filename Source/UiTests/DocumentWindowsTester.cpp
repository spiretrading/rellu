#include <memory>
#include <optional>
#include <vector>
#include <doctest/doctest.h>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPointer>
#include <QRect>
#include <QTemporaryDir>
#include <QWidget>
#include "Rellu/Model/Document.hpp"
#include "Rellu/Ui/DocumentWindows.hpp"

using namespace Rellu;

namespace {
  QString make_path(const QString& name) {
    return QDir::toNativeSeparators(QDir::current().absoluteFilePath(name));
  }

  class TestWindow : public QWidget, public DocumentWindow {
    public:
      DocumentWindows* m_windows;
      QString m_path;
      bool m_is_closable;
      int m_closes;
      std::optional<QRect> m_bounds;
      const QWidget* m_host;

      TestWindow(DocumentWindows& windows, QString path)
        : m_windows(&windows),
          m_path(std::move(path)),
          m_is_closable(true),
          m_closes(0),
          m_host(nullptr) {}

      QWidget& get_widget() override {
        return *this;
      }

      QString get_path() const override {
        return m_path;
      }

      QString get_name() const override {
        return m_path.isEmpty() ? QString("Untitled") : m_path;
      }

      void place(const QRect& bounds) override {
        m_bounds = bounds;
      }

      void place(const QWidget& host) override {
        m_host = &host;
      }

    protected:
      void closeEvent(QCloseEvent* event) override {
        ++m_closes;
        if(!m_is_closable) {
          event->ignore();
          return;
        }
        m_windows->remove(*this);
        QWidget::closeEvent(event);
      }
  };

  struct Fixture {
    DocumentWindows m_windows;
    std::vector<QString> m_requests;
    std::vector<Document*> m_documents;
    std::vector<std::unique_ptr<Document>> m_kept;
    std::vector<TestWindow*> m_made;
    bool m_is_failing;

    Fixture()
        : m_is_failing(false) {
      m_windows.set_empty_handler([] {});
      m_windows.set_area_provider([] {
        return QRect(0, 0, 2560, 1392);
      });
      m_windows.set_factory([this] (const QString& path, auto document) {
        m_requests.push_back(path);
        m_documents.push_back(document.get());
        if(document) {
          m_kept.push_back(std::move(document));
        }
        auto window = std::unique_ptr<DocumentWindow>();
        if(!m_is_failing) {
          auto made = std::make_unique<TestWindow>(m_windows, path);
          m_made.push_back(made.get());
          window = std::move(made);
        }
        return window;
      });
    }
  };

  void flush() {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();
  }
}

TEST_SUITE("DocumentWindows") {
  TEST_CASE("open") {
    auto fixture = Fixture();
    auto& windows = fixture.m_windows;
    REQUIRE(windows.open("a.xd"));
    REQUIRE(windows.open("b.xd"));
    REQUIRE(fixture.m_requests == std::vector<QString>({"a.xd", "b.xd"}));
    REQUIRE(fixture.m_documents == std::vector<Document*>({nullptr, nullptr}));
    REQUIRE(windows.get_windows() == std::vector<DocumentWindow*>(
      {fixture.m_made[0], fixture.m_made[1]}));
    REQUIRE(fixture.m_made[0]->isVisible());
    REQUIRE(fixture.m_made[1]->isVisible());
    REQUIRE(windows.get_active() == fixture.m_made[1]);
    REQUIRE(windows.get_recent_files() ==
      QStringList({make_path("b.xd"), make_path("a.xd")}));
    REQUIRE(windows.open(make_path("a.xd")));
    REQUIRE(fixture.m_requests.size() == 2);
    REQUIRE(windows.get_windows().size() == 2);
    REQUIRE(windows.get_active() == fixture.m_made[0]);
    REQUIRE(windows.get_recent_files() ==
      QStringList({make_path("a.xd"), make_path("b.xd")}));
  }

  TEST_CASE("open_failure") {
    auto fixture = Fixture();
    auto& windows = fixture.m_windows;
    windows.set_recent_files(QStringList({"missing.xd", "a.xd"}));
    fixture.m_is_failing = true;
    REQUIRE(!windows.open("missing.xd"));
    REQUIRE(windows.get_windows().empty());
    REQUIRE(windows.get_recent_files() ==
      QStringList({make_path("missing.xd"), make_path("a.xd")}));
  }

  TEST_CASE("make_new") {
    auto fixture = Fixture();
    auto& windows = fixture.m_windows;
    REQUIRE(windows.make_new());
    REQUIRE(windows.make_new());
    REQUIRE(fixture.m_requests == std::vector<QString>({"", ""}));
    REQUIRE(fixture.m_documents[0]);
    REQUIRE(fixture.m_documents[0]->get_canvas().size() == 1);
    REQUIRE(windows.get_windows().size() == 2);
    auto document = std::make_unique<Document>();
    auto made = document.get();
    REQUIRE(windows.make_new(std::move(document)));
    REQUIRE(fixture.m_documents.back() == made);
    REQUIRE(windows.get_windows().size() == 3);
    REQUIRE(!windows.find(QString()));
    REQUIRE(windows.get_recent_files().isEmpty());
    fixture.m_is_failing = true;
    REQUIRE(!windows.make_new());
    REQUIRE(windows.get_windows().size() == 3);
  }

  TEST_CASE("find") {
    auto fixture = Fixture();
    auto& windows = fixture.m_windows;
    windows.open("a.xd");
    windows.make_new();
    REQUIRE(windows.find(make_path("a.xd")) == fixture.m_made[0]);
    REQUIRE(windows.find("a.xd") == fixture.m_made[0]);
    REQUIRE(!windows.find("b.xd"));
    fixture.m_made[1]->m_path = "b.xd";
    REQUIRE(windows.find("b.xd") == fixture.m_made[1]);
  }

  TEST_CASE("remove") {
    auto fixture = Fixture();
    auto& windows = fixture.m_windows;
    auto empties = 0;
    windows.set_empty_handler([&] {
      ++empties;
    });
    windows.make_new();
    windows.make_new();
    auto first = QPointer<TestWindow>(fixture.m_made[0]);
    auto second = QPointer<TestWindow>(fixture.m_made[1]);
    REQUIRE(first->close());
    REQUIRE(windows.get_windows() == std::vector<DocumentWindow*>({second}));
    REQUIRE(windows.get_active() == second);
    REQUIRE(empties == 0);
    flush();
    REQUIRE(!first);
    REQUIRE(second->close());
    REQUIRE(windows.get_windows().empty());
    REQUIRE(!windows.get_active());
    REQUIRE(empties == 1);
    flush();
    REQUIRE(!second);
  }

  TEST_CASE("destroyed_window") {
    auto fixture = Fixture();
    auto& windows = fixture.m_windows;
    windows.make_new();
    windows.make_new();
    delete fixture.m_made[0];
    REQUIRE(windows.get_windows() ==
      std::vector<DocumentWindow*>({fixture.m_made[1]}));
    REQUIRE(windows.get_active() == fixture.m_made[1]);
  }

  TEST_CASE("activate") {
    auto fixture = Fixture();
    auto& windows = fixture.m_windows;
    windows.make_new();
    windows.make_new();
    REQUIRE(windows.get_active() == fixture.m_made[1]);
    windows.activate(*fixture.m_made[0]);
    REQUIRE(windows.get_active() == fixture.m_made[0]);
    fixture.m_made[1]->showMinimized();
    windows.activate(*fixture.m_made[1]);
    REQUIRE(!fixture.m_made[1]->isMinimized());
    REQUIRE(windows.get_active() == fixture.m_made[1]);
    fixture.m_made[0]->activateWindow();
    QApplication::processEvents();
    REQUIRE(windows.get_active() == fixture.m_made[0]);
  }

  TEST_CASE("close_all") {
    auto fixture = Fixture();
    auto& windows = fixture.m_windows;
    windows.make_new();
    windows.make_new();
    windows.make_new();
    auto made = fixture.m_made;
    made[1]->m_is_closable = false;
    REQUIRE(!windows.close_all());
    REQUIRE(windows.get_windows() ==
      std::vector<DocumentWindow*>({made[1], made[2]}));
    REQUIRE(made[1]->m_closes == 1);
    REQUIRE(made[2]->m_closes == 0);
    REQUIRE(windows.get_active() == made[1]);
    made[1]->m_is_closable = true;
    REQUIRE(windows.close_all());
    REQUIRE(windows.get_windows().empty());
    flush();
  }

  TEST_CASE("placement") {
    auto fixture = Fixture();
    auto& windows = fixture.m_windows;
    windows.make_new();
    windows.make_new();
    windows.make_new();
    auto& made = fixture.m_made;
    REQUIRE(!made[0]->m_bounds);
    REQUIRE(made[1]->m_bounds == QRect(313, 232, 1090, 761));
    REQUIRE(made[2]->m_bounds == QRect(353, 264, 1090, 761));
    auto host = QWidget();
    windows.make_new(std::make_unique<Document>(), &host);
    REQUIRE(made[3]->m_host == &host);
    REQUIRE(!made[3]->m_bounds);
    windows.make_new();
    REQUIRE(made[4]->m_bounds == QRect(392, 296, 1090, 761));
    for(auto window : windows.get_windows()) {
      window->get_widget().close();
    }
    flush();
    REQUIRE(windows.get_windows().empty());
    windows.make_new();
    windows.make_new();
    REQUIRE(made[6]->m_bounds == QRect(313, 232, 1090, 761));
  }

  TEST_CASE("cascade_bounds") {
    auto area = QRect(0, 0, 2560, 1392);
    REQUIRE(get_cascade_bounds(0, area) == QRect(313, 232, 1090, 761));
    REQUIRE(get_cascade_bounds(1, area) == QRect(353, 264, 1090, 761));
    REQUIRE(get_cascade_bounds(3, area) == QRect(431, 328, 1090, 761));
    REQUIRE(get_cascade_bounds(12, area) == QRect(785, 616, 1090, 761));
    REQUIRE(get_cascade_bounds(13, area) == QRect(313, 232, 1090, 761));
    REQUIRE(get_cascade_bounds(14, area) == QRect(353, 264, 1090, 761));
    REQUIRE(get_cascade_bounds(2, QRect(100, 50, 2560, 1392)) ==
      QRect(492, 346, 1090, 761));
    REQUIRE(get_cascade_bounds(5, QRect(0, 0, 1280, 800)) ==
      QRect(313, 232, 1090, 761));
  }

  TEST_CASE("menu_windows") {
    auto fixture = Fixture();
    auto& windows = fixture.m_windows;
    windows.make_new();
    windows.make_new();
    windows.make_new();
    auto& made = fixture.m_made;
    REQUIRE(windows.get_menu_windows(*made[2]) ==
      std::vector<DocumentWindow*>({made[1], made[0], made[2]}));
    REQUIRE(windows.get_menu_windows(*made[0]) ==
      std::vector<DocumentWindow*>({made[2], made[1], made[0]}));
    auto updates = 0;
    auto subscription = windows.subscribe([&] {
      ++updates;
    });
    windows.activate(*made[0]);
    REQUIRE(updates == 1);
    REQUIRE(windows.get_menu_windows(*made[0]) ==
      std::vector<DocumentWindow*>({made[2], made[1], made[0]}));
    REQUIRE(windows.get_menu_windows(*made[1]) ==
      std::vector<DocumentWindow*>({made[0], made[2], made[1]}));
    windows.activate(*made[0]);
    REQUIRE(updates == 1);
  }

  TEST_CASE("window_title") {
    REQUIRE(make_window_title("x1_a") ==
      QString("x1_a ") + QChar(0x200E) + "- Rellu");
  }

  TEST_CASE("file_tracking") {
    auto directory = QTemporaryDir();
    auto path = [&] (const QString& name) {
      return QDir::toNativeSeparators(directory.filePath(name));
    };
    for(auto name : {"a.xd", "b.xd", "c.xd"}) {
      auto file = QFile(path(name));
      REQUIRE(file.open(QIODevice::WriteOnly));
    }
    auto locations = QHash<QByteArray, QString>();
    auto fixture = Fixture();
    auto& windows = fixture.m_windows;
    windows.set_file_tracking([] (const QString& file) {
      if(!QFileInfo::exists(file)) {
        return QByteArray();
      }
      return QFileInfo(file).fileName().toUtf8();
    }, [&] (const QByteArray& identity) {
      return locations.value(identity);
    });
    windows.set_recent_files(QStringList({path("c.xd")}));
    REQUIRE(windows.get_recent_identities() == QList<QByteArray>({""}));
    windows.add_recent_file(path("a.xd"));
    windows.add_recent_file(path("b.xd"));
    REQUIRE(windows.get_recent_identities() ==
      QList<QByteArray>({"b.xd", "a.xd", ""}));
    REQUIRE(QFile::rename(path("a.xd"), path("renamed.xd")));
    locations.insert("a.xd", path("renamed.xd"));
    REQUIRE(QFile::remove(path("b.xd")));
    windows.locate_recent_files();
    REQUIRE(windows.get_recent_files() ==
      QStringList({path("b.xd"), path("renamed.xd"), path("c.xd")}));
    REQUIRE(windows.get_recent_identities() ==
      QList<QByteArray>({"b.xd", "a.xd", "c.xd"}));
    auto restored = Fixture();
    restored.m_windows.set_recent_files(windows.get_recent_files(),
      windows.get_recent_identities());
    REQUIRE(restored.m_windows.get_recent_identities() ==
      windows.get_recent_identities());
    windows.remove_recent_file(path("renamed.xd"));
    REQUIRE(windows.get_recent_identities() ==
      QList<QByteArray>({"b.xd", "c.xd"}));
  }

  TEST_CASE("recent_files") {
    auto fixture = Fixture();
    auto& windows = fixture.m_windows;
    auto updates = 0;
    auto subscription = windows.subscribe([&] {
      ++updates;
    });
    auto paths = QStringList();
    for(auto i = 0; i != DocumentWindows::MAXIMUM_RECENT_FILES + 1; ++i) {
      paths.push_back(QString("%1.xd").arg(i));
    }
    windows.set_recent_files(paths);
    REQUIRE(updates == 1);
    REQUIRE(windows.get_recent_files().size() ==
      DocumentWindows::MAXIMUM_RECENT_FILES);
    REQUIRE(windows.get_recent_files().front() == make_path("0.xd"));
    windows.add_recent_file("a.xd");
    REQUIRE(updates == 2);
    REQUIRE(windows.get_recent_files().front() == make_path("a.xd"));
    windows.remove_recent_file("a.xd");
    REQUIRE(updates == 3);
    REQUIRE(windows.get_recent_files().front() == make_path("0.xd"));
    windows.clear_recent_files();
    REQUIRE(updates == 4);
    REQUIRE(windows.get_recent_files().isEmpty());
    windows.update();
    REQUIRE(updates == 5);
    windows.make_new();
    REQUIRE(updates == 6);
  }
}
