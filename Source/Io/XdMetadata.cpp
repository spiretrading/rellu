#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <QByteArray>
#include <QXmlStreamReader>
#include "Rellu/Io/XdMetadata.hpp"

using namespace Rellu;

namespace {
  const auto TOOLKIT = std::string_view(
    "Adobe XMP Core 9.0-c001 1.000000, 0000/00/00-00:00:00        ");
  const auto AGENT = std::string_view("XD");
  const auto FORMAT =
    std::string_view("application/vnd.adobe.sparkler.project+dcx");
  const auto META = std::string_view("adobe:ns:meta/");
  const auto RDF =
    std::string_view("http://www.w3.org/1999/02/22-rdf-syntax-ns#");
  const auto XMP = std::string_view("http://ns.adobe.com/xap/1.0/");
  const auto DC = std::string_view("http://purl.org/dc/elements/1.1/");
  const auto MM = std::string_view("http://ns.adobe.com/xap/1.0/mm/");
  const auto EVENT =
    std::string_view("http://ns.adobe.com/xap/1.0/sType/ResourceEvent#");

  struct Field {
    std::string m_namespace;
    std::string m_name;
    std::string m_qualified_name;
    std::string m_value;
  };

  struct Namespace {
    std::string m_prefix;
    std::string m_uri;
  };

  struct Metadata {
    std::vector<Namespace> m_namespaces;
    std::vector<Field> m_properties;
    std::vector<std::vector<Field>> m_history;
  };

  std::string to_std(QStringView text) {
    return text.toUtf8().toStdString();
  }

  QLatin1StringView to_latin(std::string_view text) {
    return QLatin1StringView(text.data(), static_cast<qsizetype>(text.size()));
  }

  bool is_element(const QXmlStreamReader& reader, std::string_view uri,
      std::string_view name) {
    return reader.namespaceUri() == to_latin(uri) &&
      reader.name() == to_latin(name);
  }

  void add_namespaces(const QXmlStreamReader& reader, Metadata& metadata) {
    for(auto& declaration : reader.namespaceDeclarations()) {
      auto uri = to_std(declaration.namespaceUri());
      if(uri == RDF || uri == META) {
        continue;
      }
      auto prefix = to_std(declaration.prefix());
      auto is_declared = false;
      for(auto& existing : metadata.m_namespaces) {
        if(existing.m_prefix == prefix) {
          is_declared = true;
          break;
        }
      }
      if(!is_declared) {
        metadata.m_namespaces.push_back(Namespace(prefix, uri));
      }
    }
  }

  std::string ensure_prefix(Metadata& metadata, std::string_view uri,
      std::string_view prefix) {
    for(auto& declaration : metadata.m_namespaces) {
      if(declaration.m_uri == uri) {
        return declaration.m_prefix;
      }
    }
    metadata.m_namespaces.push_back(
      Namespace(std::string(prefix), std::string(uri)));
    return std::string(prefix);
  }

  Field* find_field(std::vector<Field>& fields, std::string_view uri,
      std::string_view name) {
    for(auto& field : fields) {
      if(field.m_namespace == uri && field.m_name == name) {
        return &field;
      }
    }
    return nullptr;
  }

  void set_field(Metadata& metadata, std::vector<Field>& fields,
      std::string_view uri, std::string_view prefix, std::string_view name,
      std::string_view value) {
    if(auto field = find_field(fields, uri, name)) {
      field->m_value = value;
      return;
    }
    auto qualified_name = ensure_prefix(metadata, uri, prefix) + ':';
    qualified_name += name;
    fields.push_back(Field(std::string(uri), std::string(name),
      std::move(qualified_name), std::string(value)));
  }

  std::vector<Field> make_event(Metadata& metadata, std::string_view action,
      std::string_view instance, std::string_view when) {
    auto event = std::vector<Field>();
    set_field(metadata, event, EVENT, "stEvt", "action", action);
    set_field(metadata, event, EVENT, "stEvt", "instanceID", instance);
    set_field(metadata, event, EVENT, "stEvt", "when", when);
    set_field(metadata, event, EVENT, "stEvt", "softwareAgent", AGENT);
    return event;
  }

  std::optional<std::vector<Field>> read_fields(
      const QXmlStreamReader& reader) {
    auto fields = std::vector<Field>();
    for(auto& attribute : reader.attributes()) {
      if(attribute.namespaceUri() == to_latin(RDF)) {
        return std::nullopt;
      }
      fields.push_back(Field(to_std(attribute.namespaceUri()),
        to_std(attribute.name()), to_std(attribute.qualifiedName()),
        to_std(attribute.value())));
    }
    return fields;
  }

  bool read_history(QXmlStreamReader& reader, Metadata& metadata) {
    if(!reader.readNextStartElement() || !is_element(reader, RDF, "Seq")) {
      return false;
    }
    add_namespaces(reader, metadata);
    while(reader.readNextStartElement()) {
      if(!is_element(reader, RDF, "li")) {
        return false;
      }
      add_namespaces(reader, metadata);
      auto event = read_fields(reader);
      if(!event || reader.readNextStartElement()) {
        return false;
      }
      metadata.m_history.push_back(std::move(*event));
    }
    return !reader.readNextStartElement();
  }

  std::optional<Metadata> parse(std::string_view xml) {
    auto reader = QXmlStreamReader(
      QByteArray(xml.data(), static_cast<qsizetype>(xml.size())));
    if(!reader.readNextStartElement() ||
        !is_element(reader, META, "xmpmeta") ||
        !reader.readNextStartElement() || !is_element(reader, RDF, "RDF") ||
        !reader.readNextStartElement() ||
        !is_element(reader, RDF, "Description")) {
      return std::nullopt;
    }
    auto metadata = Metadata();
    add_namespaces(reader, metadata);
    for(auto& attribute : reader.attributes()) {
      if(attribute.namespaceUri() == to_latin(RDF)) {
        if(attribute.name() != to_latin("about")) {
          return std::nullopt;
        }
        continue;
      }
      metadata.m_properties.push_back(Field(to_std(attribute.namespaceUri()),
        to_std(attribute.name()), to_std(attribute.qualifiedName()),
        to_std(attribute.value())));
    }
    while(reader.readNextStartElement()) {
      add_namespaces(reader, metadata);
      if(is_element(reader, DC, "title")) {
        reader.skipCurrentElement();
      } else if(!is_element(reader, MM, "History") ||
          !read_history(reader, metadata)) {
        return std::nullopt;
      }
    }
    if(reader.readNextStartElement()) {
      return std::nullopt;
    }
    while(!reader.atEnd()) {
      reader.readNext();
    }
    if(reader.hasError()) {
      return std::nullopt;
    }
    return metadata;
  }

  std::string escape(std::string_view text, bool is_attribute) {
    auto escaped = std::string();
    for(auto c : text) {
      if(c == '&') {
        escaped += "&amp;";
      } else if(c == '<') {
        escaped += "&lt;";
      } else if(c == '>') {
        escaped += "&gt;";
      } else if(c == '"' && is_attribute) {
        escaped += "&quot;";
      } else if(static_cast<unsigned char>(c) < 0x20) {
        escaped += ' ';
      } else {
        escaped += c;
      }
    }
    return escaped;
  }

  void append_field(std::string& out, const Field& field,
      std::string_view indent) {
    out += '\n';
    out += indent;
    out += field.m_qualified_name;
    out += "=\"";
    out += escape(field.m_value, true);
    out += '"';
  }

  std::string serialize(Metadata& metadata, std::string_view title) {
    auto dc = ensure_prefix(metadata, DC, "dc");
    auto mm = ensure_prefix(metadata, MM, "xmpMM");
    auto out = std::string(
      "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
      "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"");
    out += TOOLKIT;
    out += "\">\n <rdf:RDF xmlns:rdf=\"";
    out += RDF;
    out += "\">\n  <rdf:Description rdf:about=\"\"";
    for(auto& declaration : metadata.m_namespaces) {
      out += "\n    xmlns:" + declaration.m_prefix + "=\"" +
        escape(declaration.m_uri, true) + '"';
    }
    for(auto& property : metadata.m_properties) {
      append_field(out, property, "   ");
    }
    out += ">\n   <" + dc + ":title>\n    <rdf:Alt>\n"
      "     <rdf:li xml:lang=\"x-default\">" + escape(title, false) +
      "</rdf:li>\n    </rdf:Alt>\n   </" + dc + ":title>\n   <" + mm +
      ":History>\n    <rdf:Seq>\n";
    for(auto& event : metadata.m_history) {
      out += "     <rdf:li";
      for(auto& field : event) {
        append_field(out, field, "      ");
      }
      out += "/>\n";
    }
    out += "    </rdf:Seq>\n   </" + mm + ":History>\n"
      "  </rdf:Description>\n </rdf:RDF>\n</x:xmpmeta>\n";
    for(auto i = 0; i != 20; ++i) {
      out.append(100, ' ');
      out += '\n';
    }
    out.append(27, ' ');
    out += "\n<?xpacket end=\"w\"?>";
    return out;
  }

  Metadata make_metadata(std::string_view composite,
      std::string_view instance, std::string_view when) {
    auto metadata = Metadata();
    ensure_prefix(metadata, XMP, "xmp");
    ensure_prefix(metadata, DC, "dc");
    ensure_prefix(metadata, MM, "xmpMM");
    ensure_prefix(metadata, EVENT, "stEvt");
    auto& properties = metadata.m_properties;
    set_field(metadata, properties, XMP, "xmp", "CreatorTool", AGENT);
    set_field(metadata, properties, XMP, "xmp", "CreateDate", when);
    set_field(metadata, properties, XMP, "xmp", "ModifyDate", when);
    set_field(metadata, properties, XMP, "xmp", "MetadataDate", when);
    set_field(metadata, properties, DC, "dc", "format", FORMAT);
    set_field(metadata, properties, MM, "xmpMM", "DocumentID", composite);
    set_field(
      metadata, properties, MM, "xmpMM", "OriginalDocumentID", composite);
    set_field(metadata, properties, MM, "xmpMM", "InstanceID", instance);
    metadata.m_history.push_back(
      make_event(metadata, "created", composite, when));
    metadata.m_history.push_back(
      make_event(metadata, "saved", instance, when));
    return metadata;
  }

  void stamp(Metadata& metadata, std::string_view instance,
      std::string_view when) {
    ensure_prefix(metadata, DC, "dc");
    ensure_prefix(metadata, MM, "xmpMM");
    auto& properties = metadata.m_properties;
    set_field(metadata, properties, XMP, "xmp", "ModifyDate", when);
    set_field(metadata, properties, XMP, "xmp", "MetadataDate", when);
    set_field(metadata, properties, MM, "xmpMM", "InstanceID", instance);
    if(!metadata.m_history.empty()) {
      auto& last = metadata.m_history.back();
      auto action = find_field(last, EVENT, "action");
      auto agent = find_field(last, EVENT, "softwareAgent");
      if(action && action->m_value == "saved" && agent &&
          agent->m_value == AGENT) {
        set_field(metadata, last, EVENT, "stEvt", "instanceID", instance);
        set_field(metadata, last, EVENT, "stEvt", "when", when);
        return;
      }
    }
    metadata.m_history.push_back(make_event(metadata, "saved", instance, when));
  }
}

std::string Rellu::to_xmp_date(const QDateTime& time) {
  auto date = time.date();
  auto clock = time.time();
  auto text = std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}", date.year(),
    date.month(), date.day(), clock.hour(), clock.minute(), clock.second());
  if(auto milliseconds = clock.msec()) {
    auto fraction = std::format("{:03}", milliseconds);
    while(fraction.back() == '0') {
      fraction.pop_back();
    }
    text += '.' + fraction;
  }
  auto offset = time.offsetFromUtc() / 60;
  if(offset == 0) {
    text += 'Z';
  } else {
    text += std::format("{}{:02}:{:02}", offset < 0 ? '-' : '+',
      std::abs(offset) / 60, std::abs(offset) % 60);
  }
  return text;
}

std::string Rellu::save_xd_metadata(std::string_view metadata,
    std::string_view name, std::string_view composite,
    std::string_view instance, const QDateTime& time) {
  auto when = to_xmp_date(time);
  if(metadata.empty()) {
    auto created = make_metadata(composite, instance, when);
    return serialize(created, name);
  }
  auto parsed = parse(metadata);
  if(!parsed) {
    return std::string(metadata);
  }
  stamp(*parsed, instance, when);
  return serialize(*parsed, name);
}
