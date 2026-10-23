#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QImage>
#include <doctest/doctest.h>
#include "Rellu/Io/PdfExport.hpp"
#include "Rellu/Model/Transaction.hpp"

using namespace Rellu;

namespace {
  struct PdfObject {
    std::string m_dictionary;
    std::string m_stream;
  };

  struct PdfDocument {
    std::string m_bytes;
    std::map<int, PdfObject> m_objects;

    std::vector<int> find(std::string_view text) const {
      auto ids = std::vector<int>();
      for(auto& object : m_objects) {
        if(object.second.m_dictionary.find(text) != std::string::npos) {
          ids.push_back(object.first);
        }
      }
      return ids;
    }

    std::string get_contents(std::size_t page) const {
      auto pages = find("/Type /Page\n");
      auto& dictionary = m_objects.at(pages.at(page)).m_dictionary;
      auto start = dictionary.find("/Contents ") + 10;
      auto id = std::stoi(dictionary.substr(start));
      return m_objects.at(id).m_stream;
    }
  };

  std::string inflate(std::string_view data) {
    auto input = QByteArray(4, 0);
    auto size = static_cast<quint32>(64 * data.size() + 1024);
    input[0] = static_cast<char>(size >> 24);
    input[1] = static_cast<char>(size >> 16);
    input[2] = static_cast<char>(size >> 8);
    input[3] = static_cast<char>(size);
    input.append(data.data(), static_cast<qsizetype>(data.size()));
    auto output = qUncompress(input);
    return std::string(output.constData(), output.size());
  }

  PdfDocument parse(const QByteArray& bytes) {
    auto document = PdfDocument();
    document.m_bytes = std::string(bytes.constData(), bytes.size());
    auto& data = document.m_bytes;
    auto start = std::stoul(data.substr(data.rfind("startxref\n") + 10));
    REQUIRE(data.compare(start, 5, "xref\n") == 0);
    auto line = data.find('\n', start + 5);
    auto count = std::stoi(data.substr(data.find(' ', start + 5) + 1));
    for(auto id = 1; id < count; ++id) {
      auto entry = line + 1 + 20 * static_cast<std::size_t>(id);
      auto offset = std::stoul(data.substr(entry, 10));
      auto header = std::to_string(id) + " 0 obj\n";
      REQUIRE(data.compare(offset, header.size(), header) == 0);
      auto body = offset + header.size();
      auto end = data.find("\nendobj\n", body);
      auto object = PdfObject();
      auto stream = data.find(">> stream\n", body);
      if(stream != std::string::npos && stream < end) {
        object.m_dictionary = data.substr(body, stream + 2 - body);
        auto length_start = object.m_dictionary.find("/Length ") + 8;
        auto length = std::stoul(object.m_dictionary.substr(length_start));
        auto raw = std::string_view(data).substr(stream + 10, length);
        REQUIRE(data.compare(stream + 10 + length, 10, "\nendstream") == 0);
        end = data.find("\nendobj\n", stream + 10 + length);
        if(object.m_dictionary.find("/Filter /FlateDecode") !=
            std::string::npos) {
          object.m_stream = inflate(raw);
        } else {
          object.m_stream = std::string(raw);
        }
      } else {
        object.m_dictionary = data.substr(body, end - body);
      }
      document.m_objects.emplace(id, std::move(object));
    }
    return document;
  }

  std::string pdf_contents(const Document& document, const NodeId& id) {
    return parse(write_pdf(document, {id})).get_contents(0);
  }

  bool contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
  }

  NodeHeader make_header(const Matrix& transform = Matrix()) {
    auto header = NodeHeader();
    header.m_transform = transform;
    return header;
  }

  Artboard make_artboard(const Rect& frame) {
    auto artboard = Artboard();
    artboard.m_frame = frame;
    artboard.m_background = SolidFill(make_color(255, 255, 255));
    return artboard;
  }

  Shape make_shape(const Rect& bounds, Color color) {
    auto shape = Shape();
    shape.m_geometry = RectangleShape(bounds);
    shape.m_style.m_fill = SolidFill(color);
    return shape;
  }

  Text make_text(std::u16string content) {
    auto text = Text();
    text.m_content = Shared(std::move(content));
    text.m_base_style.m_font =
      Font("Roboto", "Regular", "Roboto-Regular", 16);
    text.m_base_style.m_color = make_color(0, 0, 0);
    return text;
  }

  ImageId add_image(Transaction& transaction, QSize size, QColor color) {
    auto image = QImage(size, QImage::Format_RGBA8888);
    image.fill(color);
    auto bytes = QByteArray();
    auto buffer = QBuffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    auto data = std::vector<std::byte>(static_cast<std::size_t>(bytes.size()));
    std::memcpy(data.data(), bytes.constData(), data.size());
    auto resource = ImageResource();
    resource.m_id = generate_node_id();
    resource.m_media_type = "image/png";
    resource.m_data = Shared(std::move(data));
    return transaction.add_image(std::move(resource));
  }
}

TEST_SUITE("PdfExport") {
  TEST_CASE("sort_pdf_pages") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto first = transaction.append(
      NodeId(), make_header(), make_artboard(Rect(0, 0, 100, 100)));
    auto second = transaction.append(
      NodeId(), make_header(), make_artboard(Rect(200, 1.5, 100, 100)));
    auto third = transaction.append(
      NodeId(), make_header(), make_artboard(Rect(0, 300, 100, 100)));
    auto fourth = transaction.append(
      NodeId(), make_header(), make_artboard(Rect(-50, 301.4, 100, 100)));
    transaction.commit();
    REQUIRE(sort_pdf_pages(document, {fourth, third, second, first}) ==
      std::vector<NodeId>({first, second, fourth, third}));
    REQUIRE(sort_pdf_pages(document, {third, first}) ==
      std::vector<NodeId>({first, third}));
  }

  TEST_CASE("page") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = transaction.append(
      NodeId(), make_header(), make_artboard(Rect(100, 200, 1428, 938)));
    transaction.append(artboard, make_header(make_translation(10, 20)),
      make_shape(Rect(0, 0, 50, 40), make_color(255, 0, 0)));
    transaction.commit();
    auto pdf = parse(write_pdf(document, {artboard}));
    REQUIRE(pdf.m_bytes.starts_with("%PDF-1.4\n%\xD3\xEB\xE9\xE1\n"));
    REQUIRE(pdf.m_bytes.ends_with("%%EOF"));
    REQUIRE(pdf.m_objects.at(1).m_dictionary == "<</Producer (Rellu)>>");
    REQUIRE(contains(pdf.m_bytes, "/Root "));
    REQUIRE(contains(pdf.m_bytes, "/Info 1 0 R>>"));
    auto pages = pdf.find("/Type /Page\n");
    REQUIRE(pages == std::vector<int>({2}));
    auto& page = pdf.m_objects.at(2).m_dictionary;
    REQUIRE(contains(page, "/MediaBox [0 0 1428 938]"));
    REQUIRE(contains(page, "/StructParents 0"));
    REQUIRE(contains(page, "/ProcSet [/PDF /Text /ImageB /ImageC /ImageI]"));
    REQUIRE(pdf.m_objects.at(3).m_dictionary == "<</ca 1\n/BM /Normal>>");
    REQUIRE(pdf.get_contents(0) == ".25 0 0 -.25 0 938 cm\n"
      "q\n4 0 0 4 0 0 cm\n1 1 1 RG 1 1 1 rg\n/G3 gs\n0 0 1428 938 re\nf\nQ\n"
      "q\n0 0 5712 3752 re\nW* n\nq\n4 0 0 4 40 80 cm\n1 0 0 RG 1 0 0 rg\n"
      "/G3 gs\n0 0 50 40 re\nf\nQ\nQ\n");
    auto catalogs = pdf.find("/Type /Catalog");
    REQUIRE(catalogs.size() == 1);
    REQUIRE(contains(pdf.m_bytes,
      "/Root " + std::to_string(catalogs.front()) + " 0 R"));
  }

  TEST_CASE("page_tree") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboards = std::vector<NodeId>();
    for(auto i = 0; i != 9; ++i) {
      artboards.push_back(transaction.append(NodeId(), make_header(),
        make_artboard(Rect(200 * i, 0, 100 + i, 50))));
    }
    transaction.commit();
    auto pdf = parse(write_pdf(document, artboards));
    REQUIRE(pdf.find("/Type /Page\n").size() == 9);
    auto trees = pdf.find("/Type /Pages");
    REQUIRE(trees.size() == 2);
    REQUIRE(contains(pdf.m_objects.at(trees[0]).m_dictionary, "/Count 8"));
    REQUIRE(contains(pdf.m_objects.at(trees[1]).m_dictionary, "/Count 9"));
    REQUIRE(contains(pdf.m_objects.at(pdf.find("/Type /Page\n")[8]).
      m_dictionary, "/MediaBox [0 0 108 50]"));
  }

  TEST_CASE("text") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = transaction.append(
      NodeId(), make_header(), make_artboard(Rect(0, 0, 200, 100)));
    transaction.append(artboard, make_header(make_translation(10, 30)),
      make_text(u"TT\nT T"));
    transaction.commit();
    auto pdf = parse(write_pdf(document, {artboard}));
    auto contents = pdf.get_contents(0);
    REQUIRE(contains(contents, "q\n4 0 0 4 40 120 cm\n/G3 gs\nBT\n/F"));
    REQUIRE(contains(contents, "> Tj\nET\nBT\n"));
    auto type0 = pdf.find("/Subtype /Type0");
    REQUIRE(type0.size() == 1);
    auto& font = pdf.m_objects.at(type0.front()).m_dictionary;
    REQUIRE(contains(font, "/BaseFont /Roboto-Regular"));
    REQUIRE(contains(font, "/Encoding /Identity-H"));
    auto cid = pdf.find("/Subtype /CIDFontType2");
    REQUIRE(cid.size() == 1);
    REQUIRE(contains(pdf.m_objects.at(cid.front()).m_dictionary,
      "/CIDToGIDMap /Identity"));
    auto descriptors = pdf.find("/Type /FontDescriptor");
    REQUIRE(descriptors.size() == 1);
    REQUIRE(contains(pdf.m_objects.at(descriptors.front()).m_dictionary,
      "/FontFile2 "));
    auto files = pdf.find("/Length1 ");
    REQUIRE(files.size() == 1);
    auto& file = pdf.m_objects.at(files.front());
    REQUIRE(contains(file.m_dictionary,
      "/Length1 " + std::to_string(file.m_stream.size())));
    auto unicode = pdf.m_objects.at(std::stoi(font.substr(
      font.find("/ToUnicode ") + 11))).m_stream;
    REQUIRE(contains(unicode,
      "2 beginbfchar\n<0005> <0020>\n<0039> <0054>\nendbfchar\n"));
    REQUIRE(contains(contents, " 16 Tf\n1 0 0 -1 0 0 Tm\n<0039> Tj\n"));
    REQUIRE(contains(contents, "Tm\n0 -19 Td <0039> Tj\n"
      "9.234375 0 Td <0005> Tj\n3.6484375 0 Td <0039> Tj\nET\nQ\n"));
  }

  TEST_CASE("gradient") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = transaction.append(
      NodeId(), make_header(), make_artboard(Rect(0, 0, 200, 100)));
    auto shape = make_shape(Rect(0, 0, 165, 60), make_color(0, 0, 0));
    auto gradient = GradientFill();
    gradient.m_geometry = LinearGradient(Point(1, 0), Point(0, 1));
    gradient.m_stops = {GradientStop(0, make_color(103, 113, 113)),
      GradientStop(1, make_color(46, 51, 51))};
    shape.m_style.m_fill = gradient;
    transaction.append(artboard, make_header(make_translation(10, 20)), shape);
    gradient.m_stops.insert(gradient.m_stops.begin() + 1,
      GradientStop(0.5, make_color(255, 0, 0, 0.5)));
    shape.m_style.m_fill = gradient;
    transaction.append(artboard, make_header(make_translation(10, 20)), shape);
    transaction.commit();
    auto pdf = parse(write_pdf(document, {artboard}));
    auto axial = pdf.find("/ShadingType 2");
    REQUIRE(axial.size() == 3);
    REQUIRE(pdf.m_objects.at(axial[0]).m_dictionary ==
      "<</Type /Pattern\n/PatternType 2\n/Matrix [1 0 0 -1 10 80]\n"
      "/Shading <</Function <</C0 [.404 .443 .443]\n/C1 [.18 .2 .2]\n"
      "/Domain [0 1]\n/FunctionType 2\n/N 1>>\n/Extend [true true]\n"
      "/Coords [165 0 0 60]\n/ShadingType 2\n/ColorSpace /DeviceRGB>>>>");
    REQUIRE(contains(pdf.m_objects.at(axial[1]).m_dictionary,
      "/FunctionType 3\n/Encode [0 1 0 1]\n/Bounds [.5]\n/Functions [<<"));
    REQUIRE(contains(pdf.m_objects.at(axial[2]).m_dictionary,
      "/C1 [.502 .502 .502]"));
    REQUIRE(pdf.find("/S /Luminosity").size() == 1);
    auto contents = pdf.get_contents(0);
    REQUIRE(contains(contents, "/Pattern CS/Pattern cs/P" +
      std::to_string(axial[0])));
  }

  TEST_CASE("image") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = transaction.append(
      NodeId(), make_header(), make_artboard(Rect(0, 0, 200, 100)));
    auto shape = make_shape(Rect(0, 0, 40, 20), make_color(0, 0, 0));
    auto fill = ImageFill();
    fill.m_image = add_image(transaction, QSize(40, 20), QColor(0, 0, 255));
    fill.m_size = Size(40, 20);
    shape.m_style.m_fill = fill;
    transaction.append(artboard, make_header(make_translation(10, 20)), shape);
    transaction.commit();
    auto pdf = parse(write_pdf(document, {artboard}));
    auto images = pdf.find("/Subtype /Image");
    REQUIRE(images.size() == 5);
    REQUIRE(contains(pdf.m_objects.at(images.front()).m_dictionary,
      "/Width 40\n/Height 20\n/ColorSpace /DeviceRGB\n/BitsPerComponent 8\n"
      "/Filter /DCTDecode\n/ColorTransform 0\n"));
    auto patterns = pdf.find("/PatternType 1");
    REQUIRE(patterns.size() == 1);
    auto& pattern = pdf.m_objects.at(patterns.front());
    REQUIRE(contains(pattern.m_dictionary, "/BBox [0 0 200 100]\n"
      "/XStep 200\n/YStep 100\n"));
    REQUIRE(contains(pattern.m_dictionary, "/Matrix [1 0 0 -1 0 100]"));
    REQUIRE(pattern.m_stream.starts_with(
      "q\n40 0 0 -20 10 40 cm\n/G3 gs\n/X" + std::to_string(images[0]) +
      " Do\nQ\nq\n1 0 0 1 10 20 cm\n0 0 1 RG 0 0 1 rg\n/G3 gs\n"
      "-10 -20 10 20 re\nf\n"));
    REQUIRE(contains(pdf.get_contents(0), "q\n4 0 0 4 40 80 cm\n"
      "/Pattern CS/Pattern cs/P" + std::to_string(patterns.front()) +
      " SCN/P" + std::to_string(patterns.front()) + " scn\n/G3 gs\n"
      "0 0 40 20 re\nf\nQ\n"));
  }

  TEST_CASE("stroke") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = transaction.append(
      NodeId(), make_header(), make_artboard(Rect(0, 0, 200, 100)));
    auto shape = Shape();
    shape.m_geometry = LineShape(Point(0, 0), Point(100, 0));
    shape.m_style.m_stroke = Stroke();
    transaction.append(artboard, make_header(make_translation(10, 20)), shape);
    transaction.commit();
    auto pdf = parse(write_pdf(document, {artboard}));
    REQUIRE(pdf.find("<</CA 1\n/ca 1\n/LC 0\n/LJ 0\n/LW 1\n/ML 4\n/SA true\n"
      "/BM /Normal>>").size() == 1);
    REQUIRE(contains(pdf.get_contents(0),
      "q\n4 0 0 4 40 80 cm\n/G4 gs\n0 0 m\n100 0 l\nS\nQ\n"));
  }

  TEST_CASE("ellipse") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = transaction.append(
      NodeId(), make_header(), make_artboard(Rect(0, 0, 375, 200)));
    auto shape = Shape();
    shape.m_geometry = EllipseShape(Point(70, 35), 70, 35);
    shape.m_style.m_fill = SolidFill(make_color(255, 0, 0));
    transaction.append(artboard, make_header(make_translation(183, 66)), shape);
    transaction.commit();
    auto contents = pdf_contents(document, artboard);
    REQUIRE(contains(contents, "q\n4 0 0 4 732 264 cm\n1 0 0 RG 1 0 0 rg\n"
      "/G3 gs\n70 0 m\n"
      "72.292587 0 74.579651 .056178175 76.861198 .16853453 c\n"
      "79.142746 .28089088 81.407784 .44888437 83.656311 .67251503 c\n"));
    REQUIRE(contains(contents,
      "65.420349 .056178175 67.707413 0 70 0 c\nh\nf\nQ\n"));
  }

  TEST_CASE("layer") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = transaction.append(
      NodeId(), make_header(), make_artboard(Rect(0, 0, 200, 100)));
    auto header = make_header(make_translation(10, 20));
    header.m_blend_mode = BlendMode::MULTIPLY;
    transaction.append(artboard, header,
      make_shape(Rect(0, 0, 50, 40), make_color(255, 0, 0)));
    transaction.commit();
    auto pdf = parse(write_pdf(document, {artboard}));
    auto forms = pdf.find("/Subtype /Form");
    REQUIRE(forms.size() == 1);
    auto& form = pdf.m_objects.at(forms.front());
    REQUIRE(contains(form.m_dictionary, "/BBox [0 0 800 400]\n"
      "/Group <</Type /Group\n/S /Transparency\n/I true>>"));
    REQUIRE(
      contains(form.m_stream, "4 0 0 4 40 80 cm\n1 0 0 RG 1 0 0 rg\n"));
    auto states = pdf.find("/BM /Multiply");
    REQUIRE(states.size() == 1);
    REQUIRE(contains(pdf.get_contents(0), "q\n0 0 800 400 re\nW* n\n/G" +
      std::to_string(states.front()) + " gs\n/X" +
      std::to_string(forms.front()) + " Do\nQ\n"));
  }

  TEST_CASE("drop_shadow") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = transaction.append(
      NodeId(), make_header(), make_artboard(Rect(0, 0, 800, 600)));
    auto add = [&] (double x, double y, double blur, Point offset) {
      auto shape = make_shape(Rect(0, 0, 60, 60), make_color(48, 128, 224));
      shape.m_style.m_effects.push_back(
        DropShadow(offset, blur, make_color(0, 0, 0, 0.16)));
      transaction.append(
        artboard, make_header(make_translation(x, y)), shape);
    };
    add(100.5, 60, 0.5, Point(0, 0));
    add(300, 60, 3, Point(0, 0));
    add(520, 60, 10, Point(0, 0));
    add(660, 60, 3, Point(5, -7));
    add(20, 60, 0, Point(0, 3));
    add(520, 520, 8, Point(0, 3));
    add(40, 40, 9, Point(0, 3));
    transaction.commit();
    auto pdf = parse(write_pdf(document, {artboard}));
    auto contents = pdf.get_contents(0);
    REQUIRE(contains(contents, "q\n2 0 0 2 388 226 cm\n"));
    REQUIRE(contains(contents, " scn\n/G3 gs\n0 0 136 136 re\nf\nQ\n"));
    REQUIRE(contains(contents, "q\n2 0 0 2 1116 156 cm\n"));
    REQUIRE(contains(contents, " scn\n/G3 gs\n0 0 196 196 re\nf\nQ\n"));
    REQUIRE(contains(contents, "q\n4 0 0 4 1800 -40 cm\n"));
    REQUIRE(contains(contents, " scn\n/G3 gs\n0 0 182 182 re\nf\nQ\n"));
    REQUIRE(contains(contents, "q\n2 0 0 2 2576 128 cm\n"));
    REQUIRE(contains(contents, "q\n2 0 0 2 79.839996 239.84"));
    REQUIRE(contains(contents, " gs\n0 0 126 132 re\nf\nQ\n"));
    REQUIRE(contains(contents, "q\n2 0 0 2 1856 1868 cm\n"));
    REQUIRE(contains(contents, "q\n2 0 0 2 -92 -80 cm\n"));
    REQUIRE(contains(contents, " gs\n0 0 340 340 re\nf\nQ\n"));
    REQUIRE(contains(contents, " gs\n0 0 316 302 re\nf\nQ\n"));
    REQUIRE(contents.find("2 0 0 2 1116 156 cm") <
      contents.find("4 0 0 4 1200 240 cm"));
    REQUIRE(!pdf.find("/ColorSpace /DeviceGray").empty());
    REQUIRE(pdf.find("<</ca 0\n").empty());
  }

  TEST_CASE("object_blur") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = transaction.append(
      NodeId(), make_header(), make_artboard(Rect(0, 0, 800, 600)));
    auto shape = make_shape(Rect(0, 0, 60, 60), make_color(224, 64, 128));
    shape.m_style.m_effects.push_back(Blur(1));
    transaction.append(artboard, make_header(make_translation(40, 220)),
      shape);
    transaction.commit();
    auto contents = pdf_contents(document, artboard);
    REQUIRE(contains(contents, "q\n4 0 0 4 124 844 cm\n"
      "/Pattern CS/Pattern cs/P"));
    REQUIRE(contains(contents, " scn\n/G3 gs\n0 0 74 74 re\nf\nQ\n"));
  }

  TEST_CASE("inner_shadow") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = transaction.append(
      NodeId(), make_header(), make_artboard(Rect(0, 0, 800, 600)));
    auto add = [&] (Point position, Color color, Point offset) {
      auto shape = make_shape(Rect(0, 0, 80, 80), color);
      shape.m_style.m_effects.push_back(
        InnerShadow(offset, 2, make_color(0, 0, 0)));
      transaction.append(artboard,
        make_header(make_translation(position.m_x, position.m_y)), shape);
    };
    add(Point(580, 220), make_color(240, 192, 64), Point(2, 2));
    add(Point(100, 220), make_color(255, 255, 255), Point(-3, 1));
    transaction.commit();
    auto pdf = parse(write_pdf(document, {artboard}));
    auto contents = pdf.get_contents(0);
    REQUIRE(contains(contents, "q\n4 0 0 4 2320 880 cm\n"
      ".9412 .7529 .251 RG .9412 .7529 .251 rg\n/G3 gs\n0 0 80 80 re\nf\n"
      "Q\nQ\nq\n2320 880 320 320 re\nW* n\nq\n2 0 0 2 2320 880 cm\n"));
    REQUIRE(contains(contents, "q\n2 0 0 2 400 880 cm\n"));
    REQUIRE(contains(contents, " scn\n/G3 gs\n0 0 164 164 re\nf\nQ\nQ\n"));
    auto masks = pdf.find("/Width 164\n/Height 164\n/ColorSpace /DeviceGray");
    REQUIRE(masks.size() == 2);
    auto at = [&] (std::size_t mask, int x, int y) {
      auto& alpha = pdf.m_objects.at(masks[mask]).m_stream;
      return static_cast<int>(static_cast<unsigned char>(alpha[164 * y + x]));
    };
    auto near = std::array<int, 6>({202, 183, 162, 139, 116, 93});
    for(auto x = 0; x != 6; ++x) {
      REQUIRE(std::abs(at(0, x, 80) - near[x]) <= 1);
      REQUIRE(std::abs(at(0, 80, x) - near[x]) <= 1);
    }
    auto far = std::array<int, 6>({0, 2, 4, 9, 15, 25});
    for(auto x = 0; x != 6; ++x) {
      REQUIRE(std::abs(at(0, 155 + x, 80) - far[x]) <= 1);
      REQUIRE(std::abs(at(0, 80, 155 + x) - far[x]) <= 1);
    }
    REQUIRE(std::abs(at(1, 0, 80) - 4) <= 1);
    REQUIRE(std::abs(at(1, 157, 80) - 202) <= 1);
    REQUIRE(std::abs(at(1, 159, 80) - 230) <= 1);
    REQUIRE(std::abs(at(1, 80, 0) - 162) <= 1);
    REQUIRE(std::abs(at(1, 80, 159) - 37) <= 1);
  }

  TEST_CASE("rect_mask") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = transaction.append(
      NodeId(), make_header(), make_artboard(Rect(0, 0, 800, 600)));
    auto group = Group();
    group.m_mask = Mask(std::vector<MaskShape>(
      {MaskShape(Matrix(), RectangleShape(Rect(0, 0, 80, 80)))}));
    auto masked = transaction.append(
      artboard, make_header(make_translation(610, 390)), group);
    auto shape = make_shape(Rect(0, 0, 120, 120), make_color(0, 0, 0));
    auto fill = ImageFill();
    fill.m_image = add_image(transaction, QSize(48, 48), QColor(0, 128, 0));
    fill.m_size = Size(48, 48);
    shape.m_style.m_fill = fill;
    transaction.append(
      masked, make_header(make_translation(-20, -20)), shape);
    transaction.commit();
    auto pdf = parse(write_pdf(document, {artboard}));
    auto forms = pdf.find("/Subtype /Form");
    REQUIRE(forms.size() == 1);
    REQUIRE(contains(pdf.m_objects.at(forms.front()).m_dictionary,
      "/BBox [0 0 320 320]\n"));
    REQUIRE(contains(pdf.m_objects.at(forms.front()).m_stream,
      "q\n4 0 0 4 -80 -80 cm\n/Pattern CS/Pattern cs/P"));
    REQUIRE(contains(pdf.get_contents(0), "q\n2440 1560 320 320 re\nW* n\n"
      "q\n1 0 0 1 2440 1560 cm\n/G3 gs\n/X" + std::to_string(forms.front()) +
      " Do\nQ\nQ\n"));
    REQUIRE(pdf.find("/Width 34\n/Height 34\n").size() == 1);
    REQUIRE(contains(pdf.m_objects.at(pdf.find("/BBox [-1 -1 33 33]").at(0)).
      m_dictionary, "/XStep 34\n/YStep 34\n"));
  }

  TEST_CASE("node_page") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto artboard = transaction.append(
      NodeId(), make_header(), make_artboard(Rect(0, 0, 800, 600)));
    auto shape = make_shape(Rect(0, 0, 60, 60), make_color(48, 128, 224));
    shape.m_style.m_effects.push_back(
      DropShadow(Point(0, 0), 3, make_color(0, 0, 0, 0.16)));
    auto node = transaction.append(
      artboard, make_header(make_translation(300, 60)), shape);
    transaction.commit();
    auto pdf = parse(write_pdf(document, {node}));
    REQUIRE(contains(pdf.m_objects.at(pdf.find("/Type /Page\n").at(0)).
      m_dictionary, "/MediaBox [0 0 78 78]"));
    REQUIRE(pdf.get_contents(0).starts_with(".25 0 0 -.25 0 78 cm\nq\n"
      "2 0 0 2 -48 -48 cm\n"));
    REQUIRE(contains(pdf.get_contents(0), "Q\nq\n4 0 0 4 36 36 cm\n"
      ".1882 .502 .8784 RG .1882 .502 .8784 rg\n/G3 gs\n0 0 60 60 re\nf\nQ\n"));
  }

  TEST_CASE("write_pdf_export") {
    auto document = Document();
    auto transaction = Transaction(document);
    auto second = transaction.append(
      NodeId(), make_header(), make_artboard(Rect(300, 0, 30, 30)));
    auto first = transaction.append(
      NodeId(), make_header(), make_artboard(Rect(0, 0, 20, 20)));
    transaction.commit();
    auto path = std::filesystem::temp_directory_path() / "RelluPdfExport.pdf";
    auto options = ExportOptions();
    options.m_format = ExportFormat::PDF;
    write_pdf_export(document, {second, first}, path, options);
    auto file = std::ifstream(path, std::ios::binary);
    auto bytes = std::string(std::istreambuf_iterator<char>(file), {});
    file.close();
    std::filesystem::remove(path);
    auto pdf = parse(QByteArray(bytes.data(), bytes.size()));
    auto pages = pdf.find("/Type /Page\n");
    REQUIRE(pages.size() == 2);
    REQUIRE(contains(pdf.m_objects.at(pages[0]).m_dictionary,
      "/MediaBox [0 0 20 20]"));
    REQUIRE(contains(pdf.m_objects.at(pages[1]).m_dictionary,
      "/MediaBox [0 0 30 30]"));
  }
}
