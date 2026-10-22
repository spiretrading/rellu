#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QtPlugin>
#include <doctest/doctest.h>
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Ui/AssetExport.hpp"
#include "Rellu/Ui/NodeDescription.hpp"
#include "TestDocument.hpp"

using namespace Rellu;
using namespace Rellu::Tests;

Q_IMPORT_PLUGIN(QJpegPlugin)

namespace {
  NodeId add_rectangle(Transaction& transaction, const NodeId& parent,
      const std::string& name, const Rect& bounds) {
    auto header = make_header(name);
    header.m_transform = make_translation(bounds.m_x, bounds.m_y);
    return transaction.append(parent, std::move(header),
      make_rectangle_shape(bounds.m_width, bounds.m_height));
  }

  NodeId add_artboard(Transaction& transaction, const std::string& name,
      const Rect& frame) {
    auto artboard = Artboard();
    artboard.m_frame = frame;
    artboard.m_background = SolidFill(make_color(255, 255, 255));
    return transaction.append(NodeId(), make_header(name), artboard);
  }

  std::vector<ExportTask> make_tasks(const Document& document,
      const std::vector<NodeId>& nodes, const ExportOptions& options,
      const QString& folder, const QString& name) {
    return make_export_tasks(
      document, to_export_assets(nodes), options, folder, name);
  }

  std::vector<QString> get_names(const std::vector<ExportTask>& tasks,
      const QString& folder) {
    auto names = std::vector<QString>();
    for(auto& task : tasks) {
      names.push_back(QDir(folder).relativeFilePath(task.m_path));
    }
    return names;
  }
}

TEST_SUITE("AssetExport") {
  TEST_CASE("parse_export_scale") {
    REQUIRE(parse_export_scale("2.5x") == 2.5);
    REQUIRE(parse_export_scale(" 2.5 ") == 2.5);
    REQUIRE(*parse_export_scale("1/3") == doctest::Approx(1.0 / 3));
    REQUIRE(parse_export_scale("(1+2)*2X") == 6);
    REQUIRE(!parse_export_scale("Web"));
    REQUIRE(!parse_export_scale("0"));
    REQUIRE(!parse_export_scale("-2"));
    REQUIRE(!parse_export_scale("1/0"));
    REQUIRE(!parse_export_scale("2xx"));
    REQUIRE(!parse_export_scale(""));
  }

  TEST_CASE("sanitize_file_name") {
    REQUIRE(sanitize_file_name("a/b:c?*") == "a-bc");
    REQUIRE(sanitize_file_name("  Icon <1>  ") == "Icon 1");
    REQUIRE(sanitize_file_name("con").isEmpty());
    REQUIRE(sanitize_file_name("LPT1.txt").isEmpty());
    REQUIRE(sanitize_file_name("console") == "console");
    REQUIRE(sanitize_file_name("...").isEmpty());
    REQUIRE(sanitize_file_name("a//b") == "a-b");
    REQUIRE(sanitize_file_name(QString("x\ny") + QChar(0x85)) == "xy");
  }

  TEST_CASE("export_namer") {
    auto namer = ExportNamer();
    REQUIRE(namer.make("Home") == "Home");
    REQUIRE(namer.make("home") == "home-1");
    REQUIRE(namer.make("Home") == "Home-2");
    REQUIRE(namer.make("Icon 2") == "Icon 2");
    REQUIRE(namer.make("Icon 2") == "Icon -1");
    REQUIRE(namer.make("") == "asset");
    REQUIRE(namer.make("asset") == "asset-1");
    REQUIRE(namer.make("asset1") == "asset1");
    REQUIRE(namer.make("asset1") == "asset-2");
    REQUIRE(namer.make("Longer name", 6) == "Longer");
  }

  TEST_CASE("export_scales") {
    auto options = ExportOptions();
    auto scales = get_export_scales(options);
    REQUIRE(scales.size() == 1);
    REQUIRE(scales[0].m_factor == 1);
    REQUIRE(scales[0].m_suffix.isEmpty());
    options.m_scale = 1.5;
    REQUIRE(get_export_scales(options)[0].m_factor == 1.5);
    options.m_preset = ExportPreset::IOS;
    scales = get_export_scales(options);
    REQUIRE(scales.size() == 3);
    REQUIRE(scales[2].m_factor == 3);
    REQUIRE(scales[2].m_suffix == "@3x");
    options.m_preset = ExportPreset::ANDROID;
    scales = get_export_scales(options);
    REQUIRE(scales.size() == 6);
    REQUIRE(scales[0].m_factor == 0.75);
    REQUIRE(scales[0].m_folder == "drawable-ldpi");
    REQUIRE(scales[5].m_folder == "drawable-xxxhdpi");
    options.m_format = ExportFormat::SVG;
    scales = get_export_scales(options);
    REQUIRE(scales.size() == 1);
    REQUIRE(scales[0].m_factor == 1);
  }

  TEST_CASE("batch_export_nodes") {
    auto document = Document();
    auto ids = build_test_document(document);
    REQUIRE(!has_batch_export_nodes(document));
    REQUIRE(get_batch_export_assets(document).empty());
    auto transaction = Transaction(document);
    for(auto& id : {ids.m_card, ids.m_background, ids.m_loose}) {
      auto header = document.get(id).m_header;
      header.m_is_marked_for_export = true;
      transaction.replace_header(header);
    }
    transaction.commit();
    REQUIRE(has_batch_export_nodes(document));
    auto assets = get_batch_export_assets(document);
    REQUIRE(assets.size() == 3);
    REQUIRE(assets[0].m_node == ids.m_card);
    REQUIRE(assets[1].m_node == ids.m_background);
    REQUIRE(assets[2].m_node == ids.m_loose);
    REQUIRE(assets[2].m_name.isEmpty());
  }

  TEST_CASE("batch_export_states") {
    auto document = Document();
    auto ids = build_test_document(document);
    auto transaction = Transaction(document);
    auto tree = transaction.insert_root(make_header("Button"), Group());
    transaction.append(tree, make_header("Fill"),
      make_rectangle_shape(80, 30));
    auto component = document.get_resources().m_components.at(
      ids.m_component);
    auto state = ComponentState();
    state.m_id = generate_node_id();
    state.m_name = "State 2";
    state.m_tree = tree;
    component.m_states.push_back(state);
    transaction.replace_component(component);
    auto header = document.get(ids.m_instance).m_header;
    header.m_is_marked_for_export = true;
    transaction.replace_header(header);
    transaction.commit();
    auto assets = get_batch_export_assets(document);
    REQUIRE(assets.size() == 2);
    REQUIRE(assets[0].m_node == ids.m_instance);
    REQUIRE(assets[0].m_name.isEmpty());
    REQUIRE(assets[1].m_node == tree);
    REQUIRE(assets[1].m_name == get_node_name(
      document, document.get(ids.m_instance)) + " (State 2)");
  }

  TEST_CASE("export_tasks") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto first = add_artboard(transaction, "Home", Rect(0, 0, 100, 50));
    add_rectangle(transaction, first, "Fill", Rect(0, 0, 10, 10));
    auto second = add_artboard(transaction, "home", Rect(200, 0, 100, 50));
    add_rectangle(transaction, second, "Fill", Rect(0, 0, 10, 10));
    auto empty = add_artboard(transaction, "Empty", Rect(400, 0, 10, 10));
    auto icon = add_rectangle(transaction, NodeId(), "a/b:c?*",
      Rect(0, 100, 10, 20));
    auto line = add_rectangle(transaction, NodeId(), "Line",
      Rect(0, 200, 10, 0));
    transaction.commit();
    auto nodes = std::vector{first, second, empty, icon, line};
    auto options = ExportOptions();
    auto tasks = make_tasks(document, nodes, options, "C:/out", "");
    REQUIRE(get_names(tasks, "C:/out") ==
      std::vector<QString>{"Home.png", "home-1.png", "a-bc.png"});
    REQUIRE(tasks[2].m_scale == 1);
    options.m_preset = ExportPreset::ANDROID;
    tasks = make_tasks(document, {icon}, options, "C:/out", "");
    REQUIRE(tasks.size() == 6);
    REQUIRE(get_names(tasks, "C:/out")[1] == "drawable-mdpi/a-bc.png");
    REQUIRE(tasks[0].m_scale == 0.75);
    options.m_preset = ExportPreset::WEB;
    tasks = make_tasks(document, {icon}, options, "C:/out", "");
    REQUIRE(get_names(tasks, "C:/out") ==
      std::vector<QString>{"a-bc.png", "a-bc@2x.png"});
    options.m_preset = ExportPreset::NONE;
    options.m_scale = 4000;
    tasks = make_tasks(document, {first}, options, "C:/out", "");
    REQUIRE(tasks.size() == 1);
    REQUIRE(tasks[0].m_is_too_big);
    options.m_format = ExportFormat::PDF;
    tasks = make_tasks(
      document, {first, icon}, options, "C:/out", "Spire");
    REQUIRE(get_names(tasks, "C:/out") ==
      std::vector<QString>{"Spire.pdf", "Spire.pdf"});
    REQUIRE(tasks[0].m_scale == 1);
    options.m_is_pdf_single = false;
    tasks = make_tasks(document, {first, line}, options, "C:/out", "");
    REQUIRE(get_names(tasks, "C:/out") ==
      std::vector<QString>{"Home.pdf", "Line.pdf"});
    options.m_is_pdf_single = true;
    tasks = make_tasks(document, {first, icon}, options, "C:/out", "");
    REQUIRE(get_names(tasks, "C:/out")[0] == "Untitled.pdf");
  }

  TEST_CASE("run_export") {
    auto directory = QTemporaryDir();
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = add_artboard(transaction, "Home", Rect(0, 0, 20, 10));
    add_rectangle(transaction, artboard, "Fill", Rect(0, 0, 5, 5));
    auto icon = add_rectangle(transaction, NodeId(), "Icon",
      Rect(0, 100, 10, 20));
    transaction.commit();
    auto options = ExportOptions();
    options.m_preset = ExportPreset::ANDROID;
    auto tasks = make_tasks(
      document, {artboard, icon}, options, directory.path(), "");
    REQUIRE(find_existing_files(tasks).empty());
    auto steps = std::vector<int>();
    auto result = run_export(document, tasks, options, estimate_text_bounds,
      [&] (int done, int total) {
        REQUIRE(total == 12);
        steps.push_back(done);
        return true;
      });
    REQUIRE(steps.size() == 13);
    REQUIRE(result.m_written.size() == 12);
    REQUIRE(result.m_failure == ExportFailure::NONE);
    REQUIRE(!result.m_is_cancelled);
    auto image = QImage(directory.filePath("drawable-xhdpi/Home.png"));
    REQUIRE(image.size() == QSize(40, 20));
    REQUIRE(image.pixelColor(1, 1) == QColor(255, 0, 0));
    REQUIRE(image.pixelColor(30, 15) == QColor(255, 255, 255));
    REQUIRE(QImage(directory.filePath("drawable-ldpi/Icon.png")).size() ==
      QSize(8, 15));
    REQUIRE(find_existing_files(tasks) ==
      std::vector<QString>{"Home.png", "Icon.png"});
    options = ExportOptions();
    options.m_format = ExportFormat::JPG;
    tasks = make_tasks(
      document, {artboard}, options, directory.path(), "");
    result = run_export(document, tasks, options, estimate_text_bounds,
      ExportProgress());
    REQUIRE(result.m_written.size() == 1);
    auto jpg = QImage(directory.filePath("Home.jpg"));
    REQUIRE(jpg.size() == QSize(20, 10));
    REQUIRE(!jpg.hasAlphaChannel());
  }

  TEST_CASE("cancel_export") {
    auto directory = QTemporaryDir();
    auto document = Document();
    auto transaction = Transaction(document);
    auto icon = add_rectangle(transaction, NodeId(), "Icon",
      Rect(0, 0, 10, 20));
    transaction.commit();
    auto options = ExportOptions();
    options.m_preset = ExportPreset::ANDROID;
    auto tasks =
      make_tasks(document, {icon}, options, directory.path(), "");
    auto result = run_export(document, tasks, options, estimate_text_bounds,
      [&] (int done, int total) {
        return done < 2;
      });
    REQUIRE(result.m_is_cancelled);
    REQUIRE(result.m_written.empty());
    REQUIRE(QDir(directory.path()).entryList(
      QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
  }

  TEST_CASE("export_failures") {
    auto directory = QTemporaryDir();
    auto document = Document();
    auto transaction = Transaction(document);
    auto big = add_rectangle(transaction, NodeId(), "Big",
      Rect(0, 0, 100, 100));
    auto locked = add_rectangle(transaction, NodeId(), "Locked",
      Rect(0, 0, 10, 10));
    auto icon = add_rectangle(transaction, NodeId(), "Icon",
      Rect(0, 0, 1, 1));
    transaction.commit();
    auto options = ExportOptions();
    options.m_scale = 161;
    auto tasks = make_tasks(
      document, {big, icon}, options, directory.path(), "");
    REQUIRE(tasks[0].m_is_too_big);
    auto result = run_export(document, tasks, options, estimate_text_bounds,
      ExportProgress());
    REQUIRE(result.m_failure == ExportFailure::GENERIC);
    REQUIRE(result.m_failed_nodes == std::vector{big});
    REQUIRE(result.m_written.size() == 1);
    REQUIRE(!QFileInfo(directory.filePath("Big.png")).exists());
    REQUIRE(QImage(directory.filePath("Icon.png")).size() == QSize(161, 161));
    options.m_scale = 1;
    auto path = directory.filePath("Locked.png");
    auto file = QFile(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.close();
    REQUIRE(file.setPermissions(QFileDevice::ReadOwner));
    tasks = make_tasks(
      document, {big, locked}, options, directory.path(), "");
    result = run_export(document, tasks, options, estimate_text_bounds,
      ExportProgress());
    REQUIRE(result.m_failure == ExportFailure::READ_ONLY);
    REQUIRE(result.m_failed_nodes == std::vector{locked});
    REQUIRE(result.m_failed_paths == std::vector{path});
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  }
}
