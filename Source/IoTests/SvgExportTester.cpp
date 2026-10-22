#include <cstddef>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>
#include <doctest/doctest.h>
#include <QTemporaryDir>
#include "Rellu/Io/SvgExport.hpp"
#include "Rellu/Model/Transaction.hpp"

using namespace Rellu;

namespace {
  NodeHeader make_header(std::string name, Matrix transform = Matrix()) {
    auto header = NodeHeader();
    header.m_id = generate_node_id();
    header.m_name = std::move(name);
    header.m_has_custom_name = true;
    header.m_transform = transform;
    return header;
  }

  Shape make_rectangle(double width, double height, const Color& color) {
    auto shape = Shape();
    shape.m_geometry =
      RectangleShape(Rect(0, 0, width, height), CornerRadii(0, 0, 0, 0));
    shape.m_style.m_fill = SolidFill(color);
    return shape;
  }

  Shape make_image(const ImageId& image, double width, double height) {
    auto shape = Shape();
    shape.m_geometry =
      RectangleShape(Rect(0, 0, width, height), CornerRadii(0, 0, 0, 0));
    auto fill = ImageFill();
    fill.m_image = image;
    fill.m_size = Size(width, height);
    shape.m_style.m_fill = fill;
    return shape;
  }

  ImageId add_image(Transaction& transaction, char marker) {
    auto image = ImageResource();
    image.m_id = generate_node_id();
    image.m_media_type = "image/png";
    image.m_data = Shared(std::vector<std::byte>(4, std::byte(marker)));
    return transaction.add_image(image);
  }

  ExportOptions make_options(SvgImages images = SvgImages::EMBED) {
    auto options = ExportOptions();
    options.m_format = ExportFormat::SVG;
    options.m_svg_images = images;
    return options;
  }

  std::string join(std::initializer_list<std::string_view> lines) {
    auto text = std::string();
    for(auto line : lines) {
      text += line;
      text += "\r\n";
    }
    return text;
  }
}

TEST_SUITE("SvgExport") {
  TEST_CASE("artboard") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = Artboard();
    artboard.m_frame = Rect(100, 50, 200, 100);
    artboard.m_background = SolidFill(make_color(255, 255, 255));
    auto board = transaction.append(NodeId(), make_header("Home"), artboard);
    auto solid = make_rectangle(100, 60, make_color(232, 80, 80));
    solid.m_style.m_stroke = Stroke(make_color(32, 32, 128), 4);
    transaction.append(board,
      make_header("Solid", make_translation(10, 20)), std::move(solid));
    transaction.commit();
    auto svg = make_svg_export(document, {board}, make_options(), "Home");
    REQUIRE(svg.m_markup == join({
      R"svg(<svg xmlns="http://www.w3.org/2000/svg" )svg"
        R"svg(xmlns:xlink="http://www.w3.org/1999/xlink" width="200" )svg"
        R"svg(height="100" viewBox="0 0 200 100">)svg",
      R"svg(  <defs>)svg",
      R"svg(    <clipPath id="clip-Home">)svg",
      R"svg(      <rect width="200" height="100"/>)svg",
      R"svg(    </clipPath>)svg",
      R"svg(  </defs>)svg",
      R"svg(  <g id="Home" clip-path="url(#clip-Home)">)svg",
      R"svg(    <rect width="200" height="100" fill="#fff"/>)svg",
      R"svg(    <rect id="Solid" width="100" height="60" )svg"
        R"svg(transform="translate(10 20)" fill="#e85050" stroke="#202080" )svg"
        R"svg(stroke-width="4"/>)svg",
      R"svg(  </g>)svg",
      R"svg(</svg>)svg"}));
    REQUIRE(svg.m_images.empty());
  }

  TEST_CASE("svg_code") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto ellipse = Shape();
    ellipse.m_geometry = EllipseShape(Point(70, 35), 70, 35);
    ellipse.m_style.m_fill = SolidFill(make_color(16, 96, 192));
    auto id = transaction.append(NodeId(),
      make_header("Ellipse", make_translation(183, 66)), std::move(ellipse));
    transaction.commit();
    REQUIRE(make_svg_code(document, {id}) == join({
      R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="140" )svg"
        R"svg(height="70" viewBox="0 0 140 70">)svg",
      R"svg(  <ellipse id="Ellipse" cx="70" cy="35" rx="70" ry="35" )svg"
        R"svg(fill="#1060c0"/>)svg",
      R"svg(</svg>)svg"}));
  }

  TEST_CASE("linked_images") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto first = add_image(transaction, 'a');
    auto second = add_image(transaction, 'b');
    auto group = transaction.append(NodeId(), make_header("Images"), Group());
    transaction.append(group, make_header("A"), make_image(first, 10, 10));
    transaction.append(group,
      make_header("B", make_translation(20, 0)), make_image(first, 20, 20));
    transaction.append(group,
      make_header("C", make_translation(50, 0)), make_image(second, 10, 10));
    transaction.commit();
    auto directory = QTemporaryDir();
    REQUIRE(directory.isValid());
    auto path = std::filesystem::path(
      directory.path().toStdU16String()) / "Images.svg";
    write_svg_export(document, group, path, make_options(SvgImages::LINK));
    auto file = std::ifstream(path, std::ios::binary);
    auto markup = std::string(std::istreambuf_iterator<char>(file), {});
    REQUIRE(markup == join({
      R"svg(<svg id="Images" xmlns="http://www.w3.org/2000/svg" )svg"
        R"svg(xmlns:xlink="http://www.w3.org/1999/xlink" width="60" )svg"
        R"svg(height="20" viewBox="0 0 60 20">)svg",
      R"svg(  <defs>)svg",
      R"svg(    <image id="image" width="10" height="10" )svg"
        R"svg(xlink:href="Images-image.png"/>)svg",
      R"svg(  </defs>)svg",
      R"svg(  <use id="A" xlink:href="#image"/>)svg",
      R"svg(  <use id="B" transform="translate(20) scale(2)" )svg"
        R"svg(xlink:href="#image"/>)svg",
      R"svg(  <image id="C" width="10" height="10" )svg"
        R"svg(transform="translate(50)" xlink:href="Images-image2.png"/>)svg",
      R"svg(</svg>)svg"}));
    auto linked = std::ifstream(
      path.parent_path() / "Images-image2.png", std::ios::binary);
    REQUIRE(std::string(std::istreambuf_iterator<char>(linked), {}) ==
      "bbbb");
    REQUIRE(std::filesystem::exists(path.parent_path() / "Images-image.png"));
  }

  TEST_CASE("outline_stroke") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto solid = make_rectangle(100, 60, make_color(232, 80, 80));
    solid.m_style.m_stroke = Stroke(make_color(32, 32, 128), 4);
    auto id = transaction.append(NodeId(),
      make_header("Solid", make_translation(23, 71)), std::move(solid));
    transaction.commit();
    auto options = make_options();
    options.m_is_svg_outlined = true;
    REQUIRE(make_svg_export(document, {id}, options, "Solid").m_markup ==
      join({
        R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="104" )svg"
          R"svg(height="64" viewBox="0 0 104 64">)svg",
        R"svg(  <rect id="Solid" width="100" height="60" )svg"
          R"svg(transform="translate(2 2)" fill="#e85050"/>)svg",
        R"svg(  <path id="Solid_-_Outline" data-name="Solid - Outline" )svg"
          R"svg(d="M102-2V62H-2V-2ZM2,58H98V2H2Z" )svg"
          R"svg(transform="translate(2 2)" fill="#202080"/>)svg",
        R"svg(</svg>)svg"}));
  }
}
