#include <cstddef>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>
#include <doctest/doctest.h>
#include <QTemporaryDir>
#include "Rellu/Geometry/SvgPath.hpp"
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
    REQUIRE(make_svg_export(document, {id}, options, "Solid").m_markup == join({
        R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="104" )svg"
          R"svg(height="64" viewBox="0 0 104 64">)svg",
        R"svg(  <rect id="Solid" width="100" height="60" )svg"
          R"svg(transform="translate(2 2)" fill="#e85050"/>)svg",
        R"svg(  <path id="Solid_-_Outline" data-name="Solid - Outline" )svg"
          R"svg(d="M2,2V58H98V2H2M-2-2H102V62H-2Z" )svg"
          R"svg(transform="translate(2 2)" fill="#202080"/>)svg",
        R"svg(</svg>)svg"}));
  }

  TEST_CASE("drop_shadow_only") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto shape = make_rectangle(300, 200, make_color(0, 0, 0));
    shape.m_style.m_fill = NoFill();
    shape.m_style.m_effects.push_back(
      DropShadow(Point(0, 3), 3, make_color(0, 0, 0, 41.0 / 255)));
    auto id = transaction.append(NodeId(),
      make_header("Shadow", make_translation(150, 150)), std::move(shape));
    transaction.commit();
    REQUIRE(make_svg_code(document, {id}) == join({
      R"svg(<svg xmlns="http://www.w3.org/2000/svg" )svg"
        R"svg(xmlns:xlink="http://www.w3.org/1999/xlink" width="318" )svg"
        R"svg(height="218" viewBox="0 0 318 218">)svg",
      R"svg(  <defs>)svg",
      R"svg(    <filter id="Shadow" x="0" y="0" width="318" height="218" )svg"
        R"svg(filterUnits="userSpaceOnUse">)svg",
      R"svg(      <feOffset dy="3" input="SourceAlpha"/>)svg",
      R"svg(      <feGaussianBlur stdDeviation="3" result="blur"/>)svg",
      R"svg(      <feFlood flood-opacity="0.161"/>)svg",
      R"svg(      <feComposite operator="in" in2="blur"/>)svg",
      R"svg(    </filter>)svg",
      R"svg(  </defs>)svg",
      R"svg(  <g transform="matrix(1, 0, 0, 1, 0, 0)" )svg"
        R"svg(filter="url(#Shadow)">)svg",
      R"svg(    <rect id="Shadow-2" data-name="Shadow" width="300" )svg"
        R"svg(height="200" transform="translate(9 6)" fill="#fff"/>)svg",
      R"svg(  </g>)svg",
      R"svg(</svg>)svg"}));
  }

  TEST_CASE("aligned_strokes") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto inside = Shape();
    inside.m_geometry = PathShape(Shared(*parse_svg_path(
      "M 60 120 L 260 100 L 230 320 L 90 300 L 60 120 Z")),
      FillRule::NON_ZERO);
    inside.m_style.m_fill = SolidFill(make_color(128, 192, 255));
    inside.m_style.m_stroke = Stroke(make_color(0, 48, 128), 12);
    inside.m_style.m_stroke->m_alignment = StrokeAlignment::INSIDE;
    auto outside = inside;
    outside.m_style.m_stroke->m_alignment = StrokeAlignment::OUTSIDE;
    auto group = transaction.append(NodeId(), make_header("Strokes"), Group());
    transaction.append(group, make_header("Inside"), std::move(inside));
    transaction.append(group,
      make_header("Outside", make_translation(280, 0)), std::move(outside));
    transaction.commit();
    REQUIRE(make_svg_code(document, {group}) == join({
      R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="493.946" )svg"
        R"svg(height="247.042" viewBox="0 0 493.946 247.042">)svg",
      R"svg(  <g id="Strokes" transform="translate(-60 -86.546)">)svg",
      R"svg(    <g id="Inside" fill="#80c0ff">)svg",
      R"svg(      <path d="M 224.8708801269531 313.2063598632812 L )svg"
        R"svg(95.19633483886719 294.6814270019531 L 66.9715576171875 )svg"
        R"svg(125.3327713012695 L 253.0271301269531 106.7272109985352 L )svg"
        R"svg(224.8708801269531 313.2063598632812 Z" stroke="none"/>)svg",
      R"svg(      <path d="M 246.0542449951172 113.4544296264648 L )svg"
        R"svg(73.943115234375 130.6655426025391 L 100.3926620483398 )svg"
        R"svg(289.3628234863281 L 219.7417602539062 306.4127197265625 L )svg"
        R"svg(246.0542449951172 113.4544296264648 M 260 100 L 230 320 L 90 )svg"
        R"svg(300 L 60 120 L 260 100 Z" stroke="none" fill="#003080"/>)svg",
      R"svg(    </g>)svg",
      R"svg(    <g id="Outside" transform="translate(280)" fill="#80c0ff">)svg",
      R"svg(      <path d="M 235.1291198730469 326.7936401367188 L )svg"
        R"svg(229.1514739990234 325.939697265625 L 89.15147399902344 )svg"
        R"svg(305.939697265625 L 84.80366516113281 305.3185729980469 L )svg"
        R"svg(84.08163452148438 300.9863891601562 L 54.08163833618164 )svg"
        R"svg(120.9863967895508 L 53.0284423828125 114.6672286987305 L )svg"
        R"svg(59.40297698974609 114.0297775268555 L 259.4029846191406 )svg"
        R"svg(94.02977752685547 L 266.9728698730469 93.27278900146484 L )svg"
        R"svg(265.9449768066406 100.810676574707 L 235.9449768066406 )svg"
        R"svg(320.8106689453125 L 235.1291198730469 326.7936401367188 Z" )svg"
        R"svg(stroke="none"/>)svg",
      R"svg(      <path d="M 260 100 L 60 120 L 90 300 L 230 320 L 260 100 )svg"
        R"svg(M 273.9457397460938 86.54557037353516 L 240.2582397460938 )svg"
        R"svg(333.5872802734375 L 79.60733795166016 310.6371765136719 L )svg"
        R"svg(46.056884765625 109.3344573974609 L 273.9457397460938 )svg"
        R"svg(86.54557037353516 Z" stroke="none" fill="#003080"/>)svg",
      R"svg(    </g>)svg",
      R"svg(  </g>)svg",
      R"svg(</svg>)svg"}));
  }

  TEST_CASE("detached_export") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto group = transaction.append(NodeId(),
      make_header("Outer", make_rotation(std::numbers::pi / 6)), Group());
    auto child = transaction.append(group, make_header("Child"),
      make_rectangle(180, 120, make_color(48, 128, 224)));
    transaction.commit();
    auto directory = QTemporaryDir();
    REQUIRE(directory.isValid());
    auto path = std::filesystem::path(
      directory.path().toStdU16String()) / "Child.svg";
    write_svg_export(document, child, path, make_options());
    auto expected = join({
      R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="180" )svg"
        R"svg(height="120" viewBox="0 0 180 120">)svg",
      R"svg(  <rect id="Child" width="180" height="120" fill="#3080e0"/>)svg",
      R"svg(</svg>)svg"});
    auto file = std::ifstream(path, std::ios::binary);
    REQUIRE(std::string(std::istreambuf_iterator<char>(file), {}) == expected);
    REQUIRE(make_svg_code(document, {child}) == expected);
  }
}
