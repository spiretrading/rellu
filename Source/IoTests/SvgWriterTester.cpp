#include <stdexcept>
#include <string>
#include <string_view>
#include <doctest/doctest.h>
#include "Rellu/Io/SvgWriter.hpp"

using namespace Rellu;

namespace {
  const auto ARTBOARD = std::string_view(
    R"({"children":[{"type":"artboard","id":"a","style":{"fill":{"type")"
    R"(:"solid","color":{"mode":"RGB","value":{"r":255,"g":255,"b":255})"
    R"(}}},"artboard":{"children":[{"type":"shape","name":"Solid","tran)"
    R"(sform":{"a":1,"b":0,"c":0,"d":1,"tx":23,"ty":71},"style":{"fill")"
    R"(:{"type":"solid","color":{"mode":"RGB","value":{"r":232,"g":80,")"
    R"(b":80}}},"stroke":{"type":"solid","color":{"mode":"RGB","value":)"
    R"({"r":32,"g":32,"b":128}},"width":4}},"shape":{"type":"rect","x":)"
    R"(0,"y":0,"width":100,"height":60}},{"type":"shape","name":"Icon",)"
    R"("transform":{"a":1,"b":0,"c":0,"d":1,"tx":153,"ty":51},"style":{)"
    R"("fill":{"type":"solid","color":{"mode":"RGB","value":{"r":51,"g")"
    R"(:51,"b":51}}},"opacity":0.5},"shape":{"type":"circle","cx":20,"c)"
    R"(y":20,"r":20}},{"type":"shape","name":"Icon","transform":{"a":1,)"
    R"("b":0,"c":0,"d":1,"tx":223,"ty":51},"style":{"fill":{"type":"sol)"
    R"(id","color":{"mode":"RGB","value":{"r":102,"g":102,"b":102},"alp)"
    R"(ha":0.5019607843137255}}},"shape":{"type":"rect","x":0,"y":0,"wi)"
    R"(dth":40,"height":40,"r":[4,4,4,4]}}],"ref":"a"}}],"resources":{")"
    R"(gradients":{},"clipPaths":{},"patterns":{}},"artboards":{"a":{"w)"
    R"(idth":375,"height":200,"name":"Home","x":0,"y":0}},"viewSource":)"
    R"({"x":0,"y":0,"width":375,"height":200}})");

  const auto EFFECTS = std::string_view(
    R"({"children":[{"type":"shape","name":"Shadow","transform":{"a":1,)"
    R"("b":0,"c":0,"d":1,"tx":20,"ty":10},"style":{"fill":{"type":"soli)"
    R"(d","color":{"mode":"RGB","value":{"r":80,"g":160,"b":80}}},"filt)"
    R"(ers":[{"type":"dropShadow","global":true,"params":{"dropShadows")"
    R"(:[{"dx":0,"dy":3,"r":3,"color":{"mode":"RGB","value":{"r":0,"g":)"
    R"(0,"b":0},"alpha":0.1607843137254902}}]}}]},"visualBounds":{"x":1)"
    R"(1,"y":4,"width":118,"height":78},"shape":{"type":"rect","x":0,"y)"
    R"(":0,"width":100,"height":60}},{"type":"shape","name":"Inner","tr)"
    R"(ansform":{"a":1,"b":0,"c":0,"d":1,"tx":20,"ty":100},"style":{"fi)"
    R"(ll":{"type":"solid","color":{"mode":"RGB","value":{"r":200,"g":4)"
    R"(0,"b":40},"alpha":0.5}},"stroke":{"type":"solid","color":{"mode")"
    R"(:"RGB","value":{"r":0,"g":0,"b":0}},"width":2,"align":"outside"})"
    R"(,"filters":[{"type":"uxdesign#innerShadow","global":true,"params)"
    R"(":{"innerShadows":[{"dx":2,"dy":2,"r":4,"color":{"mode":"RGB","v)"
    R"(alue":{"r":0,"g":0,"b":0},"alpha":0.5}}]}}]},"shape":{"type":"el)"
    R"(lipse","cx":40,"cy":20,"rx":40,"ry":20}},{"type":"shape","name":)"
    R"("Linear","transform":{"a":0.7071067811865476,"b":0.7071067811865)"
    R"(475,"c":-0.7071067811865475,"d":0.7071067811865476,"tx":200,"ty")"
    R"(:20},"style":{"fill":{"type":"gradient","gradient":{"ref":"g1",")"
    R"(x1":0,"y1":0.5,"x2":1,"y2":0.5,"units":"objectBoundingBox"}}},"s)"
    R"(hape":{"type":"rect","x":0,"y":0,"width":50,"height":50}},{"type)"
    R"(":"shape","name":"Radial","transform":{"a":1,"b":0,"c":0,"d":1,")"
    R"(tx":200,"ty":120},"style":{"fill":{"type":"gradient","gradient":)"
    R"({"ref":"g2","cx":0.5,"cy":0.5,"fx":0.5,"fy":0.5,"r":0.5,"units":)"
    R"("objectBoundingBox"}}},"shape":{"type":"path","path":"M 0 0 C 27)"
    R"(.614 0 50 22.386 50 50 C 50 77.614 27.614 100 0 100 L 0 0 Z"}}],)"
    R"("resources":{"gradients":{"g1":{"type":"linear","stops":[{"offse)"
    R"(t":0,"color":{"mode":"RGB","value":{"r":255,"g":90,"b":0}}},{"of)"
    R"(fset":1,"color":{"mode":"RGB","value":{"r":122,"g":32,"b":224}}})"
    R"(]},"g2":{"type":"radial","stops":[{"offset":0,"color":{"mode":"R)"
    R"(GB","value":{"r":255,"g":255,"b":255}}},{"offset":1,"color":{"mo)"
    R"(de":"RGB","value":{"r":16,"g":96,"b":192},"alpha":0.5}}]}},"clip)"
    R"(Paths":{},"patterns":{}},"artboards":{},"viewSource":{"x":0,"y":)"
    R"(0,"width":300,"height":250}})");

  const auto TEXT = std::string_view(
    R"({"children":[{"type":"text","name":"Hello  world","transform":{")"
    R"(a":1,"b":0,"c":0,"d":1,"tx":20,"ty":40},"style":{"fill":{"type":)"
    R"("solid","color":{"mode":"RGB","value":{"r":112,"g":112,"b":112}})"
    R"(},"font":{"postscriptName":"ArialMT","family":"Arial","style":"R)"
    R"(egular","size":24},"textAttributes":{"letterSpacing":50}},"text")"
    R"(:{"frame":{"type":"positioned"},"paragraphs":[{"lines":[[{"y":0,)"
    R"("from":0,"to":5,"x":0},{"y":0,"from":5,"to":12,"style":{"font":{)"
    R"("postscriptName":"Arial-BoldMT","family":"Arial","style":"Bold"})"
    R"(,"fill":{"type":"solid","color":{"mode":"RGB","value":{"r":255,")"
    R"(g":0,"b":0}}}}}]]},{"lines":[[{"y":29,"from":13,"to":19,"x":0,"s)"
    R"(tyle":{"textAttributes":{"decoration":["underline"]}}}]]}],"rawT)"
    R"(ext":"Hello  world\nAgain "}},{"type":"group","name":"Masked","t)"
    R"(ransform":{"a":1,"b":0,"c":0,"d":1,"tx":3,"ty":51},"style":{"cli)"
    R"(pPath":{"ref":"clip1"},"opacity":0.5},"group":{"children":[{"typ)"
    R"(e":"image","name":"Picture","transform":{"a":1,"b":0,"c":0,"d":1)"
    R"(,"tx":10,"ty":10},"image":{"width":100,"height":100,"href":"data)"
    R"(:image/png;base64,AAAA"}},{"type":"image","name":"Picture","tran)"
    R"(sform":{"a":1,"b":0,"c":0,"d":1,"tx":120,"ty":10},"image":{"widt)"
    R"(h":50,"height":50,"href":"data:image/png;base64,AAAA"}}]}}],"res)"
    R"(ources":{"gradients":{},"clipPaths":{"clip1":{"type":"clipPath",)"
    R"("children":[{"type":"shape","name":"Ellipse 1","transform":{"a":)"
    R"(1,"b":0,"c":0,"d":1,"tx":20,"ty":20},"shape":{"type":"circle","c)"
    R"(x":40,"cy":40,"r":40}}]}},"patterns":{}},"artboards":{},"viewSou)"
    R"(rce":{"x":0,"y":0,"width":200,"height":200}})");

  const auto ARTBOARD_ATTRIBUTES = std::string_view(
    "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www"
    ".w3.org/1999/xlink\" width=\"375\" height=\"200\" viewBox=\"0 0 37"
    "5 200\">\r\n"
    "  <defs>\r\n"
    "    <clipPath id=\"clip-Home\">\r\n"
    "      <rect width=\"375\" height=\"200\"/>\r\n"
    "    </clipPath>\r\n"
    "  </defs>\r\n"
    "  <g id=\"Home\" clip-path=\"url(#clip-Home)\">\r\n"
    "    <rect width=\"375\" height=\"200\" fill=\"#fff\"/>\r\n"
    "    <rect id=\"Solid\" width=\"100\" height=\"60\" transform=\"tra"
    "nslate(23 71)\" fill=\"#e85050\" stroke=\"#202080\" stroke-width="
    "\"4\"/>\r\n"
    "    <circle id=\"Icon\" cx=\"20\" cy=\"20\" r=\"20\" transform=\"t"
    "ranslate(153 51)\" fill=\"#333\" opacity=\"0.5\"/>\r\n"
    "    <rect id=\"Icon-2\" data-name=\"Icon\" width=\"40\" height=\"4"
    "0\" rx=\"4\" transform=\"translate(223 51)\" fill=\"rgba(102,102,1"
    "02,0.5)\"/>\r\n"
    "  </g>\r\n"
    "</svg>\r\n");

  const auto ARTBOARD_CLASSES = std::string_view(
    "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www"
    ".w3.org/1999/xlink\" width=\"375\" height=\"200\" viewBox=\"0 0 37"
    "5 200\">\r\n"
    "  <defs>\r\n"
    "    <style>\r\n"
    "      .cls-1 {\r\n"
    "        clip-path: url(#clip-Home);\r\n"
    "      }\r\n"
    "\r\n"
    "      .cls-2 {\r\n"
    "        fill: #e85050;\r\n"
    "        stroke: #202080;\r\n"
    "        stroke-width: 4px;\r\n"
    "      }\r\n"
    "\r\n"
    "      .cls-3 {\r\n"
    "        fill: #333;\r\n"
    "        opacity: 0.5;\r\n"
    "      }\r\n"
    "\r\n"
    "      .cls-4 {\r\n"
    "        fill: rgba(102,102,102,0.5);\r\n"
    "      }\r\n"
    "\r\n"
    "      .cls-5 {\r\n"
    "        fill: #fff;\r\n"
    "      }\r\n"
    "    </style>\r\n"
    "    <clipPath id=\"clip-Home\">\r\n"
    "      <rect width=\"375\" height=\"200\"/>\r\n"
    "    </clipPath>\r\n"
    "  </defs>\r\n"
    "  <g id=\"Home\" class=\"cls-1\">\r\n"
    "    <rect class=\"cls-5\" width=\"375\" height=\"200\"/>\r\n"
    "    <rect id=\"Solid\" class=\"cls-2\" width=\"100\" height=\"60\""
    " transform=\"translate(23 71)\"/>\r\n"
    "    <circle id=\"Icon\" class=\"cls-3\" cx=\"20\" cy=\"20\" r=\"20"
    "\" transform=\"translate(153 51)\"/>\r\n"
    "    <rect id=\"Icon-2\" data-name=\"Icon\" class=\"cls-4\" width="
    "\"40\" height=\"40\" rx=\"4\" transform=\"translate(223 51)\"/>\r"
    "\n"
    "  </g>\r\n"
    "</svg>\r\n");

  const auto ARTBOARD_MINIFIED = std::string_view(
    "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www"
    ".w3.org/1999/xlink\" width=\"375\" height=\"200\" viewBox=\"0 0 37"
    "5 200\"><defs><clipPath id=\"b\"><rect width=\"375\" height=\"200"
    "\"/></clipPath></defs><g id=\"a\" clip-path=\"url(#b)\"><rect widt"
    "h=\"375\" height=\"200\" fill=\"#fff\"/><rect width=\"100\" height"
    "=\"60\" transform=\"translate(23 71)\" fill=\"#e85050\" stroke=\"#"
    "202080\" stroke-width=\"4\"/><circle cx=\"20\" cy=\"20\" r=\"20\" "
    "transform=\"translate(153 51)\" fill=\"#333\" opacity=\"0.5\"/><re"
    "ct width=\"40\" height=\"40\" rx=\"4\" transform=\"translate(223 5"
    "1)\" fill=\"rgba(102,102,102,0.5)\"/></g></svg>");

  const auto EFFECTS_ATTRIBUTES = std::string_view(
    "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www"
    ".w3.org/1999/xlink\" width=\"300\" height=\"250\" viewBox=\"0 0 30"
    "0 250\">\r\n"
    "  <defs>\r\n"
    "    <filter id=\"Shadow\" x=\"11\" y=\"4\" width=\"118\" height=\""
    "78\" filterUnits=\"userSpaceOnUse\">\r\n"
    "      <feOffset dy=\"3\" input=\"SourceAlpha\"/>\r\n"
    "      <feGaussianBlur stdDeviation=\"3\" result=\"blur\"/>\r\n"
    "      <feFlood flood-opacity=\"0.161\"/>\r\n"
    "      <feComposite operator=\"in\" in2=\"blur\"/>\r\n"
    "      <feComposite in=\"SourceGraphic\"/>\r\n"
    "    </filter>\r\n"
    "    <filter id=\"Inner\">\r\n"
    "      <feOffset dx=\"2\" dy=\"2\" input=\"SourceAlpha\"/>\r\n"
    "      <feGaussianBlur stdDeviation=\"4\" result=\"blur-2\"/>\r\n"
    "      <feFlood flood-opacity=\"0.25\" result=\"color\"/>\r\n"
    "      <feComposite operator=\"out\" in=\"SourceGraphic\" in2=\"blu"
    "r-2\"/>\r\n"
    "      <feComposite operator=\"in\" in=\"color\"/>\r\n"
    "      <feComposite operator=\"in\" in2=\"SourceGraphic\"/>\r\n"
    "    </filter>\r\n"
    "    <linearGradient id=\"linear-gradient\" y1=\"0.5\" x2=\"1\" y2="
    "\"0.5\" gradientUnits=\"objectBoundingBox\">\r\n"
    "      <stop offset=\"0\" stop-color=\"#ff5a00\"/>\r\n"
    "      <stop offset=\"1\" stop-color=\"#7a20e0\"/>\r\n"
    "    </linearGradient>\r\n"
    "    <radialGradient id=\"radial-gradient\" cx=\"0.5\" cy=\"0.5\" r"
    "=\"0.5\" gradientUnits=\"objectBoundingBox\">\r\n"
    "      <stop offset=\"0\" stop-color=\"#fff\"/>\r\n"
    "      <stop offset=\"1\" stop-color=\"#1060c0\" stop-opacity=\"0.5"
    "\"/>\r\n"
    "    </radialGradient>\r\n"
    "  </defs>\r\n"
    "  <g transform=\"matrix(1, 0, 0, 1, 0, 0)\" filter=\"url(#Shadow)"
    "\">\r\n"
    "    <rect id=\"Shadow-2\" data-name=\"Shadow\" width=\"100\" heigh"
    "t=\"60\" transform=\"translate(20 10)\" fill=\"#50a050\"/>\r\n"
    "  </g>\r\n"
    "  <g data-type=\"innerShadowGroup\">\r\n"
    "    <ellipse id=\"Inner-2\" data-name=\"Inner\" cx=\"40\" cy=\"20"
    "\" rx=\"40\" ry=\"20\" transform=\"translate(20 100)\" fill=\"rgba"
    "(200,40,40,0.5)\"/>\r\n"
    "    <g transform=\"matrix(1, 0, 0, 1, 0, 0)\" filter=\"url(#Inner)"
    "\">\r\n"
    "      <ellipse id=\"Inner-3\" data-name=\"Inner\" cx=\"40\" cy=\"2"
    "0\" rx=\"40\" ry=\"20\" transform=\"translate(20 100)\" fill=\"#ff"
    "f\"/>\r\n"
    "    </g>\r\n"
    "    <g id=\"Inner-4\" data-name=\"Inner\" transform=\"translate(20"
    " 100)\" fill=\"none\" stroke=\"#000\" stroke-width=\"2\">\r\n"
    "      <ellipse cx=\"40\" cy=\"20\" rx=\"40\" ry=\"20\" stroke=\"no"
    "ne\"/>\r\n"
    "      <ellipse cx=\"40\" cy=\"20\" rx=\"41\" ry=\"21\" fill=\"none"
    "\"/>\r\n"
    "    </g>\r\n"
    "  </g>\r\n"
    "  <rect id=\"Linear\" width=\"50\" height=\"50\" transform=\"trans"
    "late(200 20) rotate(45)\" fill=\"url(#linear-gradient)\"/>\r\n"
    "  <path id=\"Radial\" d=\"M0,0A50,50,0,0,1,50,50,50,50,0,0,1,0,100"
    "Z\" transform=\"translate(200 120)\" fill=\"url(#radial-gradient)"
    "\"/>\r\n"
    "</svg>\r\n");

  const auto TEXT_CLASSES = std::string_view(
    "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www"
    ".w3.org/1999/xlink\" width=\"200\" height=\"200\" viewBox=\"0 0 20"
    "0 200\">\r\n"
    "  <defs>\r\n"
    "    <style>\r\n"
    "      .cls-1 {\r\n"
    "        fill: none;\r\n"
    "      }\r\n"
    "\r\n"
    "      .cls-2 {\r\n"
    "        fill: #707070;\r\n"
    "        font-size: 24px;\r\n"
    "        font-family: ArialMT, Arial;\r\n"
    "        letter-spacing: 0.05em;\r\n"
    "      }\r\n"
    "\r\n"
    "      .cls-3 {\r\n"
    "        fill: red;\r\n"
    "        font-family: Arial-BoldMT, Arial;\r\n"
    "        font-weight: 700;\r\n"
    "      }\r\n"
    "\r\n"
    "      .cls-4 {\r\n"
    "        text-decoration: underline;\r\n"
    "      }\r\n"
    "\r\n"
    "      .cls-5 {\r\n"
    "        opacity: 0.5;\r\n"
    "        clip-path: url(#clip-path);\r\n"
    "      }\r\n"
    "    </style>\r\n"
    "    <clipPath id=\"clip-path\">\r\n"
    "      <circle id=\"Ellipse_1\" data-name=\"Ellipse 1\" class=\"cls"
    "-1\" cx=\"40\" cy=\"40\" r=\"40\" transform=\"translate(20 20)\"/>"
    "\r\n"
    "    </clipPath>\r\n"
    "    <image id=\"image\" width=\"100\" height=\"100\" xlink:href=\""
    "data:image/png;base64,AAAA\"/>\r\n"
    "  </defs>\r\n"
    "  <text id=\"Hello_world\" data-name=\"Hello  world\" class=\"cls-"
    "2\" transform=\"translate(20 40)\"><tspan x=\"0\" y=\"0\">Hello</t"
    "span><tspan class=\"cls-3\" y=\"0\" xml:space=\"preserve\">  world"
    "</tspan><tspan class=\"cls-4\"><tspan x=\"0\" y=\"29\" xml:space="
    "\"preserve\">Again </tspan></tspan></text>\r\n"
    "  <g id=\"Masked\" class=\"cls-5\" transform=\"translate(3 51)\">"
    "\r\n"
    "    <use id=\"Picture\" transform=\"translate(10 10)\" xlink:href="
    "\"#image\"/>\r\n"
    "    <use id=\"Picture-2\" data-name=\"Picture\" transform=\"transl"
    "ate(120 10) scale(0.5)\" xlink:href=\"#image\"/>\r\n"
    "  </g>\r\n"
    "</svg>\r\n");

  std::string write(std::string_view json, SvgWriterStyling styling,
      bool is_minified = false) {
    auto document = parse_xd_value(json);
    REQUIRE(document);
    auto options = SvgWriterOptions();
    options.m_styling = styling;
    options.m_is_minified = is_minified;
    options.m_has_carriage_returns = true;
    return write_svg(*document, options);
  }
}

TEST_SUITE("SvgWriter") {
  TEST_CASE("presentation_attributes") {
    REQUIRE(write(ARTBOARD, SvgWriterStyling::ATTRIBUTE) ==
      ARTBOARD_ATTRIBUTES);
  }

  TEST_CASE("internal_css") {
    REQUIRE(write(ARTBOARD, SvgWriterStyling::CLASS) == ARTBOARD_CLASSES);
  }

  TEST_CASE("minification") {
    REQUIRE(write(ARTBOARD, SvgWriterStyling::ATTRIBUTE, true) ==
      ARTBOARD_MINIFIED);
  }

  TEST_CASE("effects") {
    REQUIRE(write(EFFECTS, SvgWriterStyling::ATTRIBUTE) ==
      EFFECTS_ATTRIBUTES);
  }

  TEST_CASE("text_and_images") {
    REQUIRE(write(TEXT, SvgWriterStyling::CLASS) == TEXT_CLASSES);
  }

  TEST_CASE("singular_transform") {
    auto json = std::string(R"({"children":[{"type":"shape","transform":)"
      R"({"a":0,"b":0,"c":0,"d":0,"tx":0,"ty":0},"shape":{"type":"rect",)"
      R"("x":0,"y":0,"width":1,"height":1}}],"viewSource":{"x":0,"y":0,)"
      R"("width":1,"height":1}})");
    REQUIRE_THROWS_AS(write(json, SvgWriterStyling::ATTRIBUTE),
      std::runtime_error);
  }
}
