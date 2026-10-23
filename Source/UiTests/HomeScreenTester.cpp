#include <vector>
#include <doctest/doctest.h>
#include <private/qzipwriter_p.h>
#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSettings>
#include <QTemporaryDir>
#include "Rellu/Ui/HomeScreen.hpp"
#include "Rellu/Ui/Translation.hpp"

using namespace Rellu;

namespace {
  void click(QWidget& widget, QPointF position) {
    auto press = QMouseEvent(QEvent::MouseButtonPress, position,
      widget.mapToGlobal(position), Qt::LeftButton, Qt::LeftButton,
      Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &press);
    auto release = QMouseEvent(QEvent::MouseButtonRelease, position,
      widget.mapToGlobal(position), Qt::LeftButton, Qt::NoButton,
      Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &release);
  }

  void click(QWidget& widget) {
    click(widget, QPointF(widget.width() / 2.0, widget.height() / 2.0));
  }

  void type(QWidget& widget, Qt::Key key, const QString& text) {
    auto press = QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier, text);
    QCoreApplication::sendEvent(&widget, &press);
    auto release = QKeyEvent(QEvent::KeyRelease, key, Qt::NoModifier, text);
    QCoreApplication::sendEvent(&widget, &release);
  }

  void write_package(const QString& path, const QImage& thumbnail) {
    auto file = QFile(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    auto writer = QZipWriter(&file);
    writer.addFile("mimetype", QByteArray("application/vnd.adobe.sparkler"
      ".project+dcxucf"));
    writer.addFile("manifest", QByteArray("{}"));
    if(!thumbnail.isNull()) {
      auto bytes = QByteArray();
      auto buffer = QBuffer(&bytes);
      buffer.open(QIODevice::WriteOnly);
      thumbnail.save(&buffer, "PNG");
      writer.addFile("thumbnail.png", bytes);
    }
    writer.close();
  }

  QWidget& find(QWidget& parent, const QString& name) {
    auto child = parent.findChild<QWidget*>(name);
    REQUIRE(child);
    return *child;
  }
}

TEST_SUITE("HomeScreen") {
  TEST_CASE("home_presets") {
    auto& presets = get_home_presets();
    REQUIRE(presets.size() == 20);
    REQUIRE(presets.front() ==
      HomePreset(HomePresetType::MOBILE, "iPhone 14 Pro Max", 430, 932));
    REQUIRE(presets.back() ==
      HomePreset(HomePresetType::CUSTOM, "Custom Size", 0, 0));
    REQUIRE(get_home_presets(HomePresetType::MOBILE).size() == 9);
    auto desktop = get_home_presets(HomePresetType::DESKTOP);
    REQUIRE(desktop.size() == 3);
    REQUIRE(desktop[1] ==
      HomePreset(HomePresetType::DESKTOP, "Web 1366", 1366, 768));
    auto social = get_home_presets(HomePresetType::SOCIAL);
    REQUIRE(social.size() == 7);
    REQUIRE(social.front().m_name == "Instagram Story");
    REQUIRE(social.back().m_name == "YouTube Video");
    REQUIRE(social[2] ==
      HomePreset(HomePresetType::SOCIAL, "Twitter Post", 1024, 512));
    REQUIRE(get_home_preset_label(presets.front()) ==
      QString("iPhone 14 Pro Max (430 ") + QChar(0x00D7) + " 932)");
    REQUIRE(get_home_preset_description(presets.front()) == "430 x 932 px");
    REQUIRE(get_home_preset_label(presets.back()) == "Custom Size");
    REQUIRE(get_home_preset_description(presets.back()).isEmpty());
  }

  TEST_CASE("home_translation") {
    set_language(Language::GERMAN);
    auto label = get_home_preset_label(
      get_home_presets(HomePresetType::SOCIAL).front());
    auto custom = get_home_preset_label(get_home_presets().back());
    auto recent = translate("home", "Recent");
    set_language(Language::ENGLISH);
    REQUIRE(label ==
      QString("Instagram-Story (1080 ") + QChar(0x00D7) + " 1920)");
    REQUIRE(custom == "Benutzerdefiniert");
    REQUIRE(recent == "Zuletzt verwendet");
  }

  TEST_CASE("home_size_text") {
    for(auto text : {"", " ", "12", "+12", "-3", "1.5", "1.", ".5", "1e3",
        "0x1F", "0b101", "0o17", "Infinity", " 7 "}) {
      REQUIRE(is_home_size_text(text));
    }
    for(auto text : {"12a", ".", "-", "1.2.3", "e3", "0x", "1 2"}) {
      REQUIRE(!is_home_size_text(text));
    }
    REQUIRE(to_home_size("") == 0);
    REQUIRE(to_home_size(" 7 ") == 7);
    REQUIRE(to_home_size("1.") == 1);
    REQUIRE(to_home_size("100.5") == 100.5);
    REQUIRE(to_home_size("1e3") == 1000);
    REQUIRE(to_home_size("0x1F") == 31);
    REQUIRE(to_home_size("0b101") == 5);
    REQUIRE(to_home_size("0o17") == 15);
  }

  TEST_CASE("recent_title") {
    REQUIRE(get_recent_title("C:/designs/row_select_prototype.xd") ==
      "row_select_prototype");
    REQUIRE(get_recent_title("C:/designs/notes.txt") == "notes.txt");
    REQUIRE(get_recent_folder("C:/designs/ui/layout.xd") ==
      QDir::toNativeSeparators("C:/designs/ui"));
  }

  TEST_CASE("load_xd_thumbnail") {
    auto directory = QTemporaryDir();
    REQUIRE(directory.isValid());
    auto image = QImage(40, 20, QImage::Format_ARGB32);
    image.fill(Qt::red);
    auto path = directory.filePath("thumbnail.xd");
    write_package(path, image);
    auto thumbnail = load_xd_thumbnail(path);
    REQUIRE(thumbnail.size() == QSize(40, 20));
    REQUIRE(thumbnail.pixelColor(5, 5) == QColor(Qt::red));
    auto bare = directory.filePath("bare.xd");
    write_package(bare, QImage());
    REQUIRE(load_xd_thumbnail(bare).isNull());
    REQUIRE(load_xd_thumbnail(directory.filePath("missing.xd")).isNull());
  }

  TEST_CASE("home_selections") {
    auto directory = QTemporaryDir();
    REQUIRE(directory.isValid());
    auto settings = QSettings(
      directory.filePath("settings.ini"), QSettings::IniFormat);
    REQUIRE(load_home_selections(settings) == HomeSelections());
    auto selections =
      HomeSelections("iPad", "Web 1280", "Twitter Post", "300", "200");
    save_home_selections(settings, selections);
    REQUIRE(load_home_selections(settings) == selections);
  }

  TEST_CASE("preset_tiles") {
    auto screen = HomeScreen();
    screen.resize(1090, 729);
    auto created = std::vector<HomePreset>();
    screen.set_create_handler([&] (const auto& preset) {
      created.push_back(preset);
    });
    auto saved = std::vector<HomeSelections>();
    screen.set_selections_handler([&] (const auto& selections) {
      saved.push_back(selections);
    });
    auto& mobile = find(screen, "preset_mobile");
    click(mobile, QPointF(67, 30));
    REQUIRE(created ==
      std::vector{get_home_presets(HomePresetType::MOBILE).front()});
    type(mobile, Qt::Key_Return, "\r");
    REQUIRE(created.size() == 2);
    auto post = get_home_presets(HomePresetType::SOCIAL)[1];
    screen.choose(post);
    REQUIRE(created.back() == post);
    REQUIRE(screen.get_shown_preset(HomePresetType::SOCIAL) == post);
    REQUIRE(screen.get_selections().m_social == "Instagram Post");
    REQUIRE(saved.size() == 1);
    click(find(screen, "preset_social"), QPointF(67, 30));
    REQUIRE(created.back() == post);
    screen.set_selections(HomeSelections("Unknown", "Web 1280", "", "", ""));
    REQUIRE(screen.get_shown_preset(HomePresetType::MOBILE) ==
      get_home_presets(HomePresetType::MOBILE).front());
    REQUIRE(screen.get_shown_preset(HomePresetType::DESKTOP).m_name ==
      "Web 1280");
    REQUIRE(screen.get_shown_preset(HomePresetType::SOCIAL).m_name ==
      "Instagram Story");
  }

  TEST_CASE("custom_size") {
    auto screen = HomeScreen();
    screen.resize(1090, 729);
    auto created = std::vector<HomePreset>();
    screen.set_create_handler([&] (const auto& preset) {
      created.push_back(preset);
    });
    screen.set_selections(HomeSelections("", "", "", "640", "480"));
    auto& width = static_cast<QLineEdit&>(find(screen, "custom_width"));
    auto& height = static_cast<QLineEdit&>(find(screen, "custom_height"));
    REQUIRE(width.text() == "640");
    REQUIRE(height.text() == "480");
    auto& tile = find(screen, "preset_custom");
    click(tile, QPointF(67, 30));
    REQUIRE(created == std::vector{
      HomePreset(HomePresetType::CUSTOM, "Custom Size", 640, 480)});
    width.clear();
    height.clear();
    screen.set_selections(HomeSelections());
    type(width, Qt::Key_1, "1");
    type(width, Qt::Key_2, "2");
    type(width, Qt::Key_A, "a");
    REQUIRE(width.text() == "12");
    type(height, Qt::Key_9, "9");
    REQUIRE(screen.get_selections().m_width.isEmpty());
    click(tile, QPointF(width.x() + 2, width.y() + 2));
    REQUIRE(created.size() == 1);
    type(width, Qt::Key_Return, "\r");
    REQUIRE(created.back() ==
      HomePreset(HomePresetType::CUSTOM, "Custom Size", 12, 9));
    REQUIRE(screen.get_selections().m_width == "12");
    REQUIRE(screen.get_selections().m_height == "9");
    width.clear();
    screen.set_selections(HomeSelections());
    click(tile, QPointF(67, 30));
    REQUIRE(created.back() ==
      HomePreset(HomePresetType::CUSTOM, "Custom Size", 0, 0));
  }

  TEST_CASE("recent_files") {
    auto directory = QTemporaryDir();
    REQUIRE(directory.isValid());
    auto image = QImage(64, 32, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    auto first = directory.filePath("first.xd");
    write_package(first, image);
    auto second = directory.filePath("second.xd");
    auto screen = HomeScreen();
    screen.resize(1090, 729);
    screen.show();
    auto opened = QStringList();
    screen.set_open_handler([&] (const auto& path) {
      opened.append(path);
    });
    REQUIRE(screen.findChildren<QWidget*>("recent").isEmpty());
    screen.set_recent_paths({first, second});
    REQUIRE(screen.get_recent_paths() == QStringList({first, second}));
    auto cards = screen.findChildren<QWidget*>("recent");
    REQUIRE(cards.size() == 2);
    REQUIRE(cards[0]->size() == QSize(144, 144 + 12 + 21 + 18));
    REQUIRE(cards[0]->pos() == QPoint(16, 398));
    REQUIRE(cards[1]->x() - cards[0]->x() == 168);
    click(*cards[1], QPointF(72, 72));
    REQUIRE(opened == QStringList({second}));
    type(*cards[0], Qt::Key_Return, "\r");
    REQUIRE(opened == QStringList({second, first}));
    auto paths = QStringList();
    for(auto i = 0; i != 7; ++i) {
      paths.append(directory.filePath(QString("file%1.xd").arg(i)));
    }
    screen.set_recent_paths(paths);
    cards = screen.findChildren<QWidget*>("recent");
    REQUIRE(cards.size() == 7);
    REQUIRE(cards[5]->x() == cards[0]->x());
    REQUIRE(cards[5]->y() - cards[0]->y() == 248);
    screen.resize(2560, 1400);
    REQUIRE(cards[0]->width() == 129);
    REQUIRE(cards[0]->x() == 406);
    REQUIRE(cards[1]->x() == 406 + 153);
    REQUIRE(cards[6]->x() == 406 + 6 * 153);
    REQUIRE(cards[6]->y() == cards[0]->y());
    screen.resize(1090, 729);
    REQUIRE(cards[0]->width() == 144);
    screen.set_recent_paths({});
    REQUIRE(screen.findChildren<QWidget*>("recent").isEmpty());
  }

  TEST_CASE("home_actions") {
    auto screen = HomeScreen();
    screen.resize(1090, 729);
    auto actions = std::vector<int>();
    screen.set_new_handler([&] {
      actions.push_back(0);
    });
    screen.set_open_dialog_handler([&] {
      actions.push_back(1);
    });
    screen.set_back_handler([&] {
      actions.push_back(2);
    });
    click(find(screen, "new_file"));
    click(find(screen, "open"));
    auto open = QKeyEvent(QEvent::KeyPress, Qt::Key_O, Qt::ControlModifier,
      "o");
    QCoreApplication::sendEvent(&screen, &open);
    auto release = QKeyEvent(QEvent::KeyRelease, Qt::Key_O,
      Qt::ControlModifier, "o");
    QCoreApplication::sendEvent(&screen, &release);
    auto reset = QKeyEvent(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier);
    QCoreApplication::sendEvent(&screen, &reset);
    REQUIRE(QGuiApplication::keyboardModifiers() == Qt::NoModifier);
    REQUIRE(actions == std::vector{0, 1, 1});
    auto& back = find(screen, "back");
    REQUIRE(!screen.is_back_enabled());
    click(back);
    REQUIRE(actions == std::vector{0, 1, 1});
    screen.set_back_enabled(true);
    REQUIRE(screen.is_back_enabled());
    click(back);
    REQUIRE(actions == std::vector{0, 1, 1, 2});
  }
}
