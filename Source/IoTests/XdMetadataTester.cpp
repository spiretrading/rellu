#include <string>
#include <string_view>
#include <doctest/doctest.h>
#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QTimeZone>
#include "Rellu/Io/XdMetadata.hpp"

using namespace Rellu;

namespace {
  std::string make_padding() {
    auto padding = std::string("\n");
    for(auto i = 0; i != 20; ++i) {
      padding.append(100, ' ');
      padding += '\n';
    }
    padding.append(27, ' ');
    padding += "\n<?xpacket end=\"w\"?>";
    return padding;
  }

  QDateTime make_time(int year, int month, int day, int hour, int minute,
      int second, int millisecond, int offset) {
    return QDateTime(QDate(year, month, day),
      QTime(hour, minute, second, millisecond),
      QTimeZone::fromSecondsAheadOfUtc(60 * offset));
  }

  std::string replace(std::string text, std::string_view from,
      std::string_view to) {
    auto position = text.rfind(from);
    REQUIRE(position != std::string::npos);
    text.replace(position, from.size(), to);
    return text;
  }

  const auto BEGIN = std::string(
    "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n");

  const auto PADDING = make_padding();

  const auto SOURCE = BEGIN +
    "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
    " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
    "  <rdf:Description rdf:about=\"\"\n"
    "    xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"\n"
    "    xmlns:dc=\"http://purl.org/dc/elements/1.1/\"\n"
    "    xmlns:xmpMM=\"http://ns.adobe.com/xap/1.0/mm/\"\n"
    "   xmp:CreateDate=\"2026-10-19T04:16:04Z\"\n"
    "   xmp:ModifyDate=\"2026-10-19T04:16:04Z\"\n"
    "   xmp:MetadataDate=\"2026-10-19T04:16:04Z\"\n"
    "   dc:format=\"application/vnd.adobe.sparkler.project+dcx\"\n"
    "   xmpMM:DocumentID=\"467adc2b-c3bb-49d8-8d6a-cc6addb05b78\"\n"
    "   xmpMM:OriginalDocumentID=\"467adc2b-c3bb-49d8-8d6a-cc6addb05b78\"\n"
    "   xmpMM:InstanceID=\"467adc2b-c3bb-49d8-8d6a-cc6addb05b78\">\n"
    "   <dc:title>\n"
    "    <rdf:Alt>\n"
    "     <rdf:li xml:lang=\"x-default\">make_source</rdf:li>\n"
    "    </rdf:Alt>\n"
    "   </dc:title>\n"
    "  </rdf:Description>\n"
    " </rdf:RDF>\n"
    "</x:xmpmeta>"
    "\n<?xpacket end=\"w\"?>";

  const auto SAVED = BEGIN +
    "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"Adobe XMP Core 9.0-c001"
    " 1.000000, 0000/00/00-00:00:00        \">\n"
    " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
    "  <rdf:Description rdf:about=\"\"\n"
    "    xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"\n"
    "    xmlns:dc=\"http://purl.org/dc/elements/1.1/\"\n"
    "    xmlns:xmpMM=\"http://ns.adobe.com/xap/1.0/mm/\"\n"
    "    xmlns:stEvt=\"http://ns.adobe.com/xap/1.0/sType/ResourceEvent#\"\n"
    "   xmp:CreateDate=\"2026-10-19T04:16:04Z\"\n"
    "   xmp:ModifyDate=\"2026-10-19T01:18:36.613-04:00\"\n"
    "   xmp:MetadataDate=\"2026-10-19T01:18:36.613-04:00\"\n"
    "   dc:format=\"application/vnd.adobe.sparkler.project+dcx\"\n"
    "   xmpMM:DocumentID=\"467adc2b-c3bb-49d8-8d6a-cc6addb05b78\"\n"
    "   xmpMM:OriginalDocumentID=\"467adc2b-c3bb-49d8-8d6a-cc6addb05b78\"\n"
    "   xmpMM:InstanceID=\"b6c0040f-2633-4980-a6c4-6e57e487c3f0\">\n"
    "   <dc:title>\n"
    "    <rdf:Alt>\n"
    "     <rdf:li xml:lang=\"x-default\">make_source </rdf:li>\n"
    "    </rdf:Alt>\n"
    "   </dc:title>\n"
    "   <xmpMM:History>\n"
    "    <rdf:Seq>\n"
    "     <rdf:li\n"
    "      stEvt:action=\"saved\"\n"
    "      stEvt:instanceID=\"b6c0040f-2633-4980-a6c4-6e57e487c3f0\"\n"
    "      stEvt:when=\"2026-10-19T01:18:36.613-04:00\"\n"
    "      stEvt:softwareAgent=\"XD\"/>\n"
    "    </rdf:Seq>\n"
    "   </xmpMM:History>\n"
    "  </rdf:Description>\n"
    " </rdf:RDF>\n"
    "</x:xmpmeta>"
    + PADDING;

  const auto FIRST = BEGIN +
    "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"Adobe XMP Core 9.0-c001"
    " 1.000000, 0000/00/00-00:00:00        \">\n"
    " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
    "  <rdf:Description rdf:about=\"\"\n"
    "    xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"\n"
    "    xmlns:dc=\"http://purl.org/dc/elements/1.1/\"\n"
    "    xmlns:xmpMM=\"http://ns.adobe.com/xap/1.0/mm/\"\n"
    "    xmlns:stEvt=\"http://ns.adobe.com/xap/1.0/sType/ResourceEvent#\"\n"
    "   xmp:CreatorTool=\"XD\"\n"
    "   xmp:CreateDate=\"2026-10-19T23:26:16.645-04:00\"\n"
    "   xmp:ModifyDate=\"2026-10-19T23:32:23.236-04:00\"\n"
    "   xmp:MetadataDate=\"2026-10-19T23:32:23.236-04:00\"\n"
    "   dc:format=\"application/vnd.adobe.sparkler.project+dcx\"\n"
    "   xmpMM:DocumentID=\"75bd8ec8-c31b-47f5-8fb3-cd8ef037f160\"\n"
    "   xmpMM:OriginalDocumentID=\"75bd8ec8-c31b-47f5-8fb3-cd8ef037f160\"\n"
    "   xmpMM:InstanceID=\"8f7ed324-d2e4-4650-a7bb-04191a1d4b5f\">\n"
    "   <dc:title>\n"
    "    <rdf:Alt>\n"
    "     <rdf:li xml:lang=\"x-default\">w9_a </rdf:li>\n"
    "    </rdf:Alt>\n"
    "   </dc:title>\n"
    "   <xmpMM:History>\n"
    "    <rdf:Seq>\n"
    "     <rdf:li\n"
    "      stEvt:action=\"created\"\n"
    "      stEvt:instanceID=\"c1430a8b-5ebb-4e04-bffe-53223b919727\"\n"
    "      stEvt:when=\"2026-10-19T23:26:16.645-04:00\"\n"
    "      stEvt:softwareAgent=\"XD\"/>\n"
    "     <rdf:li\n"
    "      stEvt:action=\"saved\"\n"
    "      stEvt:instanceID=\"8f7ed324-d2e4-4650-a7bb-04191a1d4b5f\"\n"
    "      stEvt:when=\"2026-10-19T23:32:23.236-04:00\"\n"
    "      stEvt:softwareAgent=\"XD\"/>\n"
    "    </rdf:Seq>\n"
    "   </xmpMM:History>\n"
    "  </rdf:Description>\n"
    " </rdf:RDF>\n"
    "</x:xmpmeta>"
    + PADDING;

  const auto SECOND = BEGIN +
    "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"Adobe XMP Core 9.0-c001"
    " 1.000000, 0000/00/00-00:00:00        \">\n"
    " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
    "  <rdf:Description rdf:about=\"\"\n"
    "    xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"\n"
    "    xmlns:dc=\"http://purl.org/dc/elements/1.1/\"\n"
    "    xmlns:xmpMM=\"http://ns.adobe.com/xap/1.0/mm/\"\n"
    "    xmlns:stEvt=\"http://ns.adobe.com/xap/1.0/sType/ResourceEvent#\"\n"
    "   xmp:CreatorTool=\"XD\"\n"
    "   xmp:CreateDate=\"2026-10-19T23:26:16.645-04:00\"\n"
    "   xmp:ModifyDate=\"2026-10-19T23:36:50.119-04:00\"\n"
    "   xmp:MetadataDate=\"2026-10-19T23:36:50.119-04:00\"\n"
    "   dc:format=\"application/vnd.adobe.sparkler.project+dcx\"\n"
    "   xmpMM:DocumentID=\"75bd8ec8-c31b-47f5-8fb3-cd8ef037f160\"\n"
    "   xmpMM:OriginalDocumentID=\"75bd8ec8-c31b-47f5-8fb3-cd8ef037f160\"\n"
    "   xmpMM:InstanceID=\"993ff242-d293-4a25-a8f2-63245ccb0f84\">\n"
    "   <dc:title>\n"
    "    <rdf:Alt>\n"
    "     <rdf:li xml:lang=\"x-default\">w9_a </rdf:li>\n"
    "    </rdf:Alt>\n"
    "   </dc:title>\n"
    "   <xmpMM:History>\n"
    "    <rdf:Seq>\n"
    "     <rdf:li\n"
    "      stEvt:action=\"created\"\n"
    "      stEvt:instanceID=\"c1430a8b-5ebb-4e04-bffe-53223b919727\"\n"
    "      stEvt:when=\"2026-10-19T23:26:16.645-04:00\"\n"
    "      stEvt:softwareAgent=\"XD\"/>\n"
    "     <rdf:li\n"
    "      stEvt:action=\"saved\"\n"
    "      stEvt:instanceID=\"993ff242-d293-4a25-a8f2-63245ccb0f84\"\n"
    "      stEvt:when=\"2026-10-19T23:36:50.119-04:00\"\n"
    "      stEvt:softwareAgent=\"XD\"/>\n"
    "    </rdf:Seq>\n"
    "   </xmpMM:History>\n"
    "  </rdf:Description>\n"
    " </rdf:RDF>\n"
    "</x:xmpmeta>"
    + PADDING;

  const auto COPY = BEGIN +
    "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"Adobe XMP Core 9.0-c001"
    " 1.000000, 0000/00/00-00:00:00        \">\n"
    " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
    "  <rdf:Description rdf:about=\"\"\n"
    "    xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"\n"
    "    xmlns:dc=\"http://purl.org/dc/elements/1.1/\"\n"
    "    xmlns:xmpMM=\"http://ns.adobe.com/xap/1.0/mm/\"\n"
    "    xmlns:stEvt=\"http://ns.adobe.com/xap/1.0/sType/ResourceEvent#\"\n"
    "   xmp:CreatorTool=\"XD\"\n"
    "   xmp:CreateDate=\"2025-03-24T17:26:43.351-04:00\"\n"
    "   xmp:ModifyDate=\"2025-03-24T17:26:43.351-04:00\"\n"
    "   xmp:MetadataDate=\"2025-03-24T17:26:43.351-04:00\"\n"
    "   dc:format=\"application/vnd.adobe.sparkler.project+dcx\"\n"
    "   xmpMM:DocumentID=\"50764587-3031-4c48-aca7-4a8bf55dfb1b\"\n"
    "   xmpMM:OriginalDocumentID=\"50764587-3031-4c48-aca7-4a8bf55dfb1b\"\n"
    "   xmpMM:InstanceID=\"5d97705c-fab0-4bae-95b0-c5d14a0cef2b\">\n"
    "   <dc:title>\n"
    "    <rdf:Alt>\n"
    "     <rdf:li xml:lang=\"x-default\">components </rdf:li>\n"
    "    </rdf:Alt>\n"
    "   </dc:title>\n"
    "   <xmpMM:History>\n"
    "    <rdf:Seq>\n"
    "     <rdf:li\n"
    "      stEvt:action=\"created\"\n"
    "      stEvt:instanceID=\"50764587-3031-4c48-aca7-4a8bf55dfb1b\"\n"
    "      stEvt:when=\"2025-03-24T17:26:43.351-04:00\"\n"
    "      stEvt:softwareAgent=\"XD\"/>\n"
    "     <rdf:li\n"
    "      stEvt:action=\"saved\"\n"
    "      stEvt:instanceID=\"5d97705c-fab0-4bae-95b0-c5d14a0cef2b\"\n"
    "      stEvt:when=\"2025-03-24T17:26:43.351-04:00\"\n"
    "      stEvt:softwareAgent=\"XD\"/>\n"
    "    </rdf:Seq>\n"
    "   </xmpMM:History>\n"
    "  </rdf:Description>\n"
    " </rdf:RDF>\n"
    "</x:xmpmeta>"
    + PADDING;
}

TEST_SUITE("XdMetadata") {
  TEST_CASE("dates") {
    REQUIRE(to_xmp_date(make_time(2026, 10, 19, 1, 18, 36, 613, -240)) ==
      "2026-10-19T01:18:36.613-04:00");
    REQUIRE(to_xmp_date(make_time(2026, 10, 19, 23, 45, 45, 200, -240)) ==
      "2026-10-19T23:45:45.2-04:00");
    REQUIRE(to_xmp_date(make_time(2024, 3, 18, 13, 33, 4, 820, -240)) ==
      "2024-03-18T13:33:04.82-04:00");
    REQUIRE(to_xmp_date(make_time(2021, 1, 5, 9, 7, 3, 0, -300)) ==
      "2021-01-05T09:07:03-05:00");
    REQUIRE(to_xmp_date(make_time(2025, 7, 1, 12, 0, 0, 50, 0)) ==
      "2025-07-01T12:00:00.05Z");
    REQUIRE(to_xmp_date(make_time(2025, 7, 1, 18, 30, 0, 1, 330)) ==
      "2025-07-01T18:30:00.001+05:30");
  }

  TEST_CASE("save") {
    REQUIRE(save_xd_metadata(SOURCE, "make_source\x01",
      "3715a5ef-5ae3-4bfe-997a-f895f88af208",
      "b6c0040f-2633-4980-a6c4-6e57e487c3f0",
      make_time(2026, 10, 19, 1, 18, 36, 613, -240)) == SAVED);
  }

  TEST_CASE("repeated_save") {
    auto time = make_time(2026, 10, 19, 23, 36, 50, 119, -240);
    REQUIRE(save_xd_metadata(FIRST, "w9_a\x01",
      "245d5114-76f4-4e0d-b5ac-4b9f16c0fe4a",
      "993ff242-d293-4a25-a8f2-63245ccb0f84", time) == SECOND);
    auto other = replace(FIRST, "softwareAgent=\"XD\"",
      "softwareAgent=\"xctest\"");
    auto appended = replace(replace(SECOND, "993ff242-d293-4a25-a8f2-"
      "63245ccb0f84\"\n      stEvt:when=\"2026-10-19T23:36:50.119-04:00\"\n"
      "      stEvt:softwareAgent=\"XD\"", "8f7ed324-d2e4-4650-a7bb-"
      "04191a1d4b5f\"\n      stEvt:when=\"2026-10-19T23:32:23.236-04:00\"\n"
      "      stEvt:softwareAgent=\"xctest\""), "    </rdf:Seq>",
      "     <rdf:li\n"
      "      stEvt:action=\"saved\"\n"
      "      stEvt:instanceID=\"993ff242-d293-4a25-a8f2-63245ccb0f84\"\n"
      "      stEvt:when=\"2026-10-19T23:36:50.119-04:00\"\n"
      "      stEvt:softwareAgent=\"XD\"/>\n"
      "    </rdf:Seq>");
    REQUIRE(save_xd_metadata(other, "w9_a\x01",
      "245d5114-76f4-4e0d-b5ac-4b9f16c0fe4a",
      "993ff242-d293-4a25-a8f2-63245ccb0f84", time) == appended);
  }

  TEST_CASE("new_file") {
    REQUIRE(save_xd_metadata("", "components\x01",
      "50764587-3031-4c48-aca7-4a8bf55dfb1b",
      "5d97705c-fab0-4bae-95b0-c5d14a0cef2b",
      make_time(2025, 3, 24, 17, 26, 43, 351, -240)) == COPY);
  }

  TEST_CASE("titles") {
    auto saved = save_xd_metadata("", "A & <B>\x01",
      "50764587-3031-4c48-aca7-4a8bf55dfb1b",
      "5d97705c-fab0-4bae-95b0-c5d14a0cef2b",
      make_time(2025, 3, 24, 17, 26, 43, 351, -240));
    REQUIRE(saved == replace(COPY, ">components </rdf:li>",
      ">A &amp; &lt;B&gt; </rdf:li>"));
  }

  TEST_CASE("unknown_properties") {
    auto unknown = replace(SECOND, "   <dc:title>",
      "   <xmp:Thumbnails>x</xmp:Thumbnails>\n   <dc:title>");
    REQUIRE(save_xd_metadata(unknown, "w9_a\x01",
      "245d5114-76f4-4e0d-b5ac-4b9f16c0fe4a",
      "f1f2fade-da51-4255-be5c-cdde1ec20b0b",
      make_time(2026, 10, 19, 23, 45, 45, 200, -240)) == unknown);
    auto malformed = SECOND.substr(0, SECOND.find("   <dc:title>"));
    REQUIRE(save_xd_metadata(malformed, "w9_a\x01",
      "245d5114-76f4-4e0d-b5ac-4b9f16c0fe4a",
      "f1f2fade-da51-4255-be5c-cdde1ec20b0b",
      make_time(2026, 10, 19, 23, 45, 45, 200, -240)) == malformed);
  }
}
