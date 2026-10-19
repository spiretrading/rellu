#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>
#include <utility>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDrag>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QScrollArea>
#include <QToolButton>
#include "Rellu/Commands/ComponentCommands.hpp"
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Ui/AssetsPanel.hpp"
#include "Colors.hpp"
#include "Icons.hpp"

using namespace Rellu;

namespace {
  constexpr auto THUMBNAIL_SIZE = 28;
  constexpr auto THUMBNAIL_RATIO = 3.0;
  constexpr auto ROW_HEIGHT = 38;
  constexpr auto MAXIMUM_CHECKED_CHANGES = 4096;
  constexpr auto MAXIMUM_DEPTH = 256;
  const auto EM_DASH = QString(QChar(0x2014));
  const auto HIGHLIGHT = QString("background: #E0F0FA;");

  QColor to_qcolor(const Color& color) {
    return QColor::fromRgbF(static_cast<float>(color.m_red),
      static_cast<float>(color.m_green), static_cast<float>(color.m_blue),
      static_cast<float>(color.m_alpha));
  }

  int to_byte(double component) {
    return std::clamp(static_cast<int>(std::lround(component * 255)), 0, 255);
  }

  QPixmap make_thumbnail(auto paint) {
    auto ratio = THUMBNAIL_RATIO;
    auto pixmap = QPixmap(static_cast<int>(THUMBNAIL_SIZE * ratio),
      static_cast<int>(THUMBNAIL_SIZE * ratio));
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    auto painter = QPainter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    auto bounds = QRectF(0.5, 0.5, THUMBNAIL_SIZE - 1, THUMBNAIL_SIZE - 1);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawRect(bounds);
    paint(painter, bounds);
    painter.setPen(QPen(Colors::BORDER, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(bounds);
    return pixmap;
  }

  QPixmap make_swatch_thumbnail(const ColorSwatch& swatch) {
    return make_thumbnail([&] (QPainter& painter, const QRectF& bounds) {
      if(swatch.m_gradient.empty()) {
        painter.setBrush(to_qcolor(swatch.m_color));
      } else {
        auto gradient = QLinearGradient(bounds.topLeft(), bounds.topRight());
        for(auto& stop : swatch.m_gradient) {
          gradient.setColorAt(std::clamp(stop.m_offset, 0.0, 1.0),
            to_qcolor(stop.m_color));
        }
        painter.setBrush(gradient);
      }
      painter.drawRect(bounds);
    });
  }

  QPixmap make_component_thumbnail() {
    return make_thumbnail([] (QPainter& painter, const QRectF& bounds) {
      painter.setBrush(QColor(0xE1, 0xE1, 0xE1));
      painter.drawRect(bounds);
      painter.setBrush(QColor(0xC4, 0xC4, 0xC4));
      painter.drawRect(bounds.adjusted(6, 11, -6, -11));
    });
  }

  QPixmap make_component_thumbnail(const QImage& image) {
    return make_thumbnail([&] (QPainter& painter, const QRectF& bounds) {
      auto inner = bounds.adjusted(2, 2, -2, -2);
      auto size =
        QSizeF(image.size()).scaled(inner.size(), Qt::KeepAspectRatio);
      auto target = QRectF(inner.center().x() - size.width() / 2,
        inner.center().y() - size.height() / 2, size.width(), size.height());
      painter.setRenderHint(QPainter::SmoothPixmapTransform);
      painter.drawImage(target, image);
    });
  }

  QToolButton* make_tool_button(Glyph glyph, const QString& tip) {
    auto button = new QToolButton();
    button->setObjectName("panel_button");
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setIcon(get_icon(glyph));
    button->setIconSize(QSize(16, 16));
    button->setFixedSize(24, 24);
    button->setToolTip(tip);
    button->setAccessibleName(tip);
    return button;
  }

  void fill_row(QWidget& row, const QPixmap& thumbnail,
      const QString& thumbnail_text, const QString& label) {
    row.setObjectName("asset_row");
    row.setFixedHeight(ROW_HEIGHT);
    auto layout = new QHBoxLayout(&row);
    layout->setContentsMargins(16, 0, 16, 0);
    layout->setSpacing(10);
    auto image = new QLabel();
    image->setObjectName("asset_thumbnail");
    image->setFixedSize(THUMBNAIL_SIZE, THUMBNAIL_SIZE);
    if(thumbnail_text.isEmpty()) {
      image->setPixmap(thumbnail);
    } else {
      image->setObjectName("character_style_preview");
      image->setText(thumbnail_text);
      image->setAlignment(Qt::AlignCenter);
    }
    layout->addWidget(image);
    auto text = new QLabel(label);
    text->setObjectName("asset_label");
    layout->addWidget(text, 1);
  }

  QWidget* make_row(const QPixmap& thumbnail, const QString& thumbnail_text,
      const QString& label) {
    auto row = new QWidget();
    fill_row(*row, thumbnail, thumbnail_text, label);
    return row;
  }

  class ComponentRow : public QWidget {
    public:
      ComponentRow(AssetsPanel& panel, const ComponentId& component)
        : m_panel(&panel),
          m_component(component) {}

    protected:
      void mousePressEvent(QMouseEvent* event) override {
        if(event->button() == Qt::LeftButton) {
          m_press = event->position().toPoint();
          m_is_pressed = true;
        }
        QWidget::mousePressEvent(event);
      }

      void mouseReleaseEvent(QMouseEvent* event) override {
        m_is_pressed = false;
        QWidget::mouseReleaseEvent(event);
      }

      void mouseMoveEvent(QMouseEvent* event) override {
        if(!m_is_pressed || !m_panel->get_editor() ||
            (event->position().toPoint() - m_press).manhattanLength() <
              QApplication::startDragDistance()) {
          QWidget::mouseMoveEvent(event);
          return;
        }
        m_is_pressed = false;
        auto drag = new QDrag(this);
        drag->setMimeData(make_component_mime_data(m_component));
        auto thumbnail = m_panel->get_thumbnail(m_component);
        if(!thumbnail.isNull()) {
          drag->setPixmap(thumbnail);
        }
        drag->exec(Qt::CopyAction);
      }

      void contextMenuEvent(QContextMenuEvent* event) override {
        auto menu = m_panel->make_component_menu(m_component, this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(event->globalPos());
      }

    private:
      AssetsPanel* m_panel;
      ComponentId m_component;
      QPoint m_press;
      bool m_is_pressed = false;
  };
}

AssetsPanel::AssetsPanel(QWidget* parent)
    : QWidget(parent),
      m_document(nullptr),
      m_editor(nullptr),
      m_entries(4),
      m_content(new QWidget()),
      m_content_layout(new QVBoxLayout(m_content)),
      m_is_empty_shown(false) {
  setObjectName("assets_panel");
  m_thumbnail_timer.setSingleShot(true);
  m_thumbnail_timer.setInterval(0);
  QObject::connect(&m_thumbnail_timer, &QTimer::timeout, this, [this] {
    render_thumbnails();
  });
  auto layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  auto header = new QWidget();
  auto header_layout = new QHBoxLayout(header);
  header_layout->setContentsMargins(10, 10, 16, 0);
  header_layout->setSpacing(2);
  auto back = make_tool_button(Glyph::CHEVRON_LEFT, "Back");
  back->setEnabled(false);
  header_layout->addWidget(back);
  auto title = new QLabel("DOCUMENT ASSETS");
  title->setObjectName("panel_heading");
  header_layout->addWidget(title, 1);
  auto share = make_tool_button(Glyph::SHARE, "Share");
  share->setEnabled(false);
  header_layout->addWidget(share);
  layout->addWidget(header);
  auto search = new QWidget();
  auto search_layout = new QHBoxLayout(search);
  search_layout->setContentsMargins(46, 6, 16, 0);
  search_layout->setSpacing(4);
  auto field = new QLineEdit();
  field->setObjectName("search_field");
  field->setPlaceholderText("Search document assets");
  field->addAction(get_icon(Glyph::SEARCH), QLineEdit::LeadingPosition);
  field->setEnabled(false);
  search_layout->addWidget(field, 1);
  auto filter_menu = make_tool_button(Glyph::CHEVRON_DOWN, "Search Filter");
  filter_menu->setEnabled(false);
  search_layout->addWidget(filter_menu);
  layout->addWidget(search);
  auto controls = new QWidget();
  auto controls_layout = new QHBoxLayout(controls);
  controls_layout->setContentsMargins(14, 10, 16, 6);
  controls_layout->setSpacing(10);
  for(auto [glyph, tip] : {std::pair(Glyph::FILTER, "Filter"),
      std::pair(Glyph::SORT, "Sort")}) {
    auto button = make_tool_button(glyph, tip);
    button->setEnabled(false);
    controls_layout->addWidget(button);
  }
  controls_layout->addStretch(1);
  for(auto [glyph, tip] : {std::pair(Glyph::LIST_VIEW, "List View"),
      std::pair(Glyph::GRID_VIEW, "Grid View")}) {
    auto button = make_tool_button(glyph, tip);
    button->setEnabled(false);
    controls_layout->addWidget(button);
  }
  layout->addWidget(controls);
  auto scroll = new QScrollArea();
  scroll->setObjectName("assets_scroll");
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_content->setObjectName("assets_content");
  m_content_layout->setContentsMargins(0, 0, 0, 16);
  m_content_layout->setSpacing(0);
  scroll->setWidget(m_content);
  layout->addWidget(scroll, 1);
  rebuild();
}

Document* AssetsPanel::get_document() const {
  return m_document;
}

void AssetsPanel::set_document(Document* document) {
  m_editor = nullptr;
  attach(document);
  rebuild();
}

Editor* AssetsPanel::get_editor() const {
  return m_editor;
}

void AssetsPanel::set_editor(Editor* editor) {
  m_editor = editor;
  attach(editor ? &editor->get_document() : nullptr);
  rebuild();
}

void AssetsPanel::set_thumbnailer(ComponentThumbnailer thumbnailer) {
  m_thumbnailer = std::move(thumbnailer);
  m_thumbnails.clear();
  rebuild();
}

void AssetsPanel::set_main_component_handler(
    std::function<void (const ComponentId& component)> handler) {
  m_main_component_handler = std::move(handler);
}

std::vector<QString> AssetsPanel::get_entries(AssetSection section) const {
  return m_entries[static_cast<int>(section)];
}

std::vector<ComponentId> AssetsPanel::get_components() const {
  return m_components;
}

QPixmap AssetsPanel::get_thumbnail(const ComponentId& component) const {
  if(auto entry = m_thumbnails.find(component); entry != m_thumbnails.end()) {
    return entry->second;
  }
  return QPixmap();
}

void AssetsPanel::render_thumbnails() {
  auto pending =
    std::exchange(m_pending, std::unordered_set<ComponentId>());
  if(!m_document || !m_thumbnailer) {
    return;
  }
  auto& components = m_document->get_resources().m_components;
  for(auto& id : pending) {
    if(!components.contains(id)) {
      continue;
    }
    auto image = QImage();
    try {
      image = m_thumbnailer(*m_document, id,
        static_cast<int>(THUMBNAIL_SIZE * THUMBNAIL_RATIO));
    } catch(const std::exception&) {}
    auto thumbnail = image.isNull() ? make_component_thumbnail() :
      make_component_thumbnail(image);
    m_thumbnails.insert_or_assign(id, thumbnail);
    if(auto label = m_thumbnail_labels.find(id);
        label != m_thumbnail_labels.end() && label->second) {
      label->second->setPixmap(thumbnail);
    }
  }
}

QMenu* AssetsPanel::make_component_menu(
    const ComponentId& component, QWidget* parent) {
  auto menu = new QMenu(parent);
  auto linked = m_document && is_linked(*m_document, component);
  auto edit = menu->addAction("Edit Main Component");
  edit->setEnabled(static_cast<bool>(m_main_component_handler) && !linked);
  QObject::connect(edit, &QAction::triggered, this, [this, component] {
    if(m_main_component_handler) {
      m_main_component_handler(component);
    }
  });
  auto swap = menu->addAction("Swap Component");
  auto is_swappable = false;
  if(m_editor) {
    auto& document = m_editor->get_document();
    for(auto& id : m_editor->get_selection().get_nodes()) {
      if(auto node = document.find(id)) {
        if(auto role = get_instance_role(*node); role && !role->m_is_main &&
            role->m_component != component) {
          is_swappable = true;
        }
      }
    }
  }
  swap->setEnabled(is_swappable);
  QObject::connect(swap, &QAction::triggered, this, [this, component] {
    swap_component(component);
  });
  auto rename = menu->addAction("Rename");
  rename->setEnabled(m_editor && !linked);
  QObject::connect(rename, &QAction::triggered, this, [this, component] {
    auto row = m_component_rows.find(component);
    if(row == m_component_rows.end() || !row->second) {
      return;
    }
    auto label = row->second->findChild<QLabel*>("asset_label");
    if(!label) {
      return;
    }
    auto field = new QLineEdit(label->text(), row->second);
    field->setObjectName("asset_rename");
    field->setGeometry(label->geometry());
    field->selectAll();
    field->show();
    field->setFocus();
    QObject::connect(field, &QLineEdit::editingFinished, this,
      [this, component, field = QPointer<QLineEdit>(field)] {
        if(!field) {
          return;
        }
        auto text = field->text();
        field->deleteLater();
        rename_component(component, text);
      });
  });
  return menu;
}

bool AssetsPanel::rename_component(
    const ComponentId& component, const QString& name) {
  if(!m_editor || name.trimmed().isEmpty() ||
      is_linked(m_editor->get_document(), component)) {
    return false;
  }
  try {
    return m_editor->execute(std::make_unique<RenameComponentCommand>(
      component, name.trimmed().toStdString())) != nullptr;
  } catch(const std::exception&) {
    return false;
  }
}

bool AssetsPanel::swap_component(const ComponentId& component) {
  if(!m_editor) {
    return false;
  }
  try {
    return m_editor->execute(std::make_unique<SwapComponentCommand>(
      m_editor->get_selection().get_nodes(), component)) != nullptr;
  } catch(const std::exception&) {
    return false;
  }
}

void AssetsPanel::reveal(const ComponentId& component) {
  for(auto& row : m_component_rows) {
    if(row.second) {
      row.second->setStyleSheet(row.first == component ? HIGHLIGHT : "");
    }
  }
  auto row = m_component_rows.find(component);
  if(row == m_component_rows.end() || !row->second) {
    return;
  }
  if(auto scroll = findChild<QScrollArea*>("assets_scroll")) {
    scroll->ensureWidgetVisible(row->second);
  }
}

bool AssetsPanel::is_empty_shown() const {
  return m_is_empty_shown;
}

void AssetsPanel::attach(Document* document) {
  m_subscription.release();
  m_document = document;
  m_thumbnails.clear();
  m_pending.clear();
  if(m_document) {
    m_subscription = m_document->subscribe([this] (const auto& changes) {
      invalidate_thumbnails(changes);
      if(!changes.m_components.empty() || !changes.m_swatches.empty() ||
          !changes.m_character_styles.empty()) {
        rebuild();
      }
    });
  }
}

void AssetsPanel::rebuild() {
  for(auto& entries : m_entries) {
    entries.clear();
  }
  m_components.clear();
  m_thumbnail_labels.clear();
  m_component_rows.clear();
  while(auto item = m_content_layout->takeAt(0)) {
    if(auto widget = item->widget()) {
      widget->deleteLater();
    }
    delete item;
  }
  if(m_document) {
    auto& resources = m_document->get_resources();
    for(auto& swatch : resources.m_swatches) {
      m_entries[static_cast<int>(AssetSection::COLORS)].push_back(
        get_swatch_label(swatch));
    }
    for(auto& style : resources.m_character_styles) {
      m_entries[static_cast<int>(AssetSection::CHARACTER_STYLES)].push_back(
        get_character_style_label(style));
    }
    for(auto& component : resources.m_components) {
      m_entries[static_cast<int>(AssetSection::COMPONENTS)].push_back(
        QString::fromStdString(component.second.m_name));
      m_components.push_back(component.first);
    }
  }
  add_section(AssetSection::COLORS, "Colors");
  add_section(AssetSection::CHARACTER_STYLES, "Character Styles");
  add_section(AssetSection::COMPONENTS, "Components");
  add_section(AssetSection::VIDEOS, "Videos");
  m_is_empty_shown = std::all_of(m_entries.begin(), m_entries.end(),
    [] (const auto& entries) {
      return entries.empty();
    });
  if(m_is_empty_shown) {
    auto invitation = new QWidget();
    auto layout = new QVBoxLayout(invitation);
    layout->setContentsMargins(24, 40, 24, 0);
    layout->setSpacing(12);
    auto illustration = new QLabel();
    illustration->setAlignment(Qt::AlignCenter);
    illustration->setPixmap(
      make_icon(Glyph::ASSETS, Colors::SECONDARY_TEXT).pixmap(72, 72));
    layout->addWidget(illustration);
    auto message = new QLabel(
      "Add your styles and components, or use elements from existing "
      "libraries.");
    message->setObjectName("empty_message");
    message->setAlignment(Qt::AlignCenter);
    message->setWordWrap(true);
    layout->addWidget(message);
    auto link = new QLabel("<u>View libraries</u>");
    link->setObjectName("empty_link");
    link->setAlignment(Qt::AlignCenter);
    link->setEnabled(false);
    layout->addWidget(link);
    m_content_layout->addWidget(invitation);
  }
  m_content_layout->addStretch(1);
  if(!m_pending.empty()) {
    m_thumbnail_timer.start();
  }
}

void AssetsPanel::add_section(AssetSection section, const QString& title) {
  auto header = new QWidget();
  auto header_layout = new QHBoxLayout(header);
  header_layout->setContentsMargins(10, 10, 16, 4);
  header_layout->setSpacing(4);
  auto toggle = new QToolButton();
  toggle->setObjectName("section_toggle");
  toggle->setAutoRaise(true);
  toggle->setCheckable(true);
  toggle->setChecked(true);
  toggle->setFocusPolicy(Qt::NoFocus);
  toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  toggle->setIcon(get_icon(Glyph::CHEVRON_DOWN));
  toggle->setIconSize(QSize(14, 14));
  toggle->setText(title);
  header_layout->addWidget(toggle, 1, Qt::AlignLeft);
  auto add = make_tool_button(Glyph::PLUS, "Add");
  add->setEnabled(false);
  header_layout->addWidget(add);
  m_content_layout->addWidget(header);
  auto rows = new QWidget();
  auto rows_layout = new QVBoxLayout(rows);
  rows_layout->setContentsMargins(0, 0, 0, 0);
  rows_layout->setSpacing(0);
  if(m_document) {
    auto& resources = m_document->get_resources();
    if(section == AssetSection::COLORS) {
      for(auto& swatch : resources.m_swatches) {
        rows_layout->addWidget(make_row(make_swatch_thumbnail(swatch),
          QString(), get_swatch_label(swatch)));
      }
    } else if(section == AssetSection::CHARACTER_STYLES) {
      for(auto& style : resources.m_character_styles) {
        rows_layout->addWidget(make_row(
          QPixmap(), "Ag", get_character_style_label(style)));
      }
    } else if(section == AssetSection::COMPONENTS) {
      for(auto& component : resources.m_components) {
        rows_layout->addWidget(make_component_row(component.second));
      }
    }
  }
  m_content_layout->addWidget(rows);
  QObject::connect(toggle, &QToolButton::toggled, rows, [=] (bool is_open) {
    rows->setVisible(is_open);
    toggle->setIcon(
      get_icon(is_open ? Glyph::CHEVRON_DOWN : Glyph::CHEVRON_RIGHT));
  });
}

QWidget* AssetsPanel::make_component_row(const Component& component) {
  auto row = new ComponentRow(*this, component.m_id);
  auto thumbnail = get_thumbnail(component.m_id);
  if(thumbnail.isNull()) {
    thumbnail = make_component_thumbnail();
    if(m_thumbnailer) {
      m_pending.insert(component.m_id);
    }
  }
  fill_row(*row, thumbnail, QString(),
    QString::fromStdString(component.m_name));
  row->setToolTip(QString::fromStdString(component.m_name));
  m_thumbnail_labels.emplace(component.m_id,
    row->findChild<QLabel*>("asset_thumbnail"));
  m_component_rows.emplace(component.m_id, row);
  return row;
}

void AssetsPanel::invalidate_thumbnails(const ChangeSet& changes) {
  if(!m_thumbnailer || m_thumbnails.empty()) {
    return;
  }
  auto masters = std::unordered_map<NodeId, ComponentId>();
  for(auto& component : m_document->get_resources().m_components) {
    masters.emplace(component.second.m_master, component.first);
  }
  auto ids = std::vector<NodeId>();
  for(auto& change : changes.m_modified) {
    ids.push_back(change.m_id);
  }
  ids.insert(ids.end(), changes.m_inserted.begin(), changes.m_inserted.end());
  ids.insert(ids.end(), changes.m_moved.begin(), changes.m_moved.end());
  for(auto& id : changes.m_components) {
    if(masters.contains(id) || m_document->get_resources().m_components.
        contains(id)) {
      m_thumbnails.erase(id);
      m_pending.insert(id);
    }
  }
  if(ids.size() > MAXIMUM_CHECKED_CHANGES) {
    for(auto& entry : masters) {
      m_thumbnails.erase(entry.second);
      m_pending.insert(entry.second);
    }
  } else {
    for(auto& id : ids) {
      auto current = id;
      for(auto depth = 0; depth != MAXIMUM_DEPTH; ++depth) {
        auto node = m_document->find(current);
        if(!node) {
          break;
        }
        if(node->m_parent.is_nil()) {
          if(auto master = masters.find(current); master != masters.end()) {
            m_thumbnails.erase(master->second);
            m_pending.insert(master->second);
          }
          break;
        }
        current = node->m_parent;
      }
    }
  }
  if(!m_pending.empty()) {
    m_thumbnail_timer.start();
  }
}

QMimeData* Rellu::make_component_mime_data(const ComponentId& component) {
  auto data = new QMimeData();
  data->setData(COMPONENT_MIME_TYPE,
    QByteArray::fromStdString(to_string(component)));
  return data;
}

std::optional<ComponentId> Rellu::read_component_mime_data(
    const QMimeData& data) {
  if(!data.hasFormat(COMPONENT_MIME_TYPE)) {
    return std::nullopt;
  }
  if(auto id = parse_node_id(data.data(COMPONENT_MIME_TYPE).toStdString())) {
    return *id;
  }
  return std::nullopt;
}

QString Rellu::get_swatch_label(const ColorSwatch& swatch) {
  if(!swatch.m_name.empty()) {
    return QString::fromStdString(swatch.m_name);
  }
  return QString("#%1%2%3").arg(to_byte(swatch.m_color.m_red), 2, 16,
    QChar('0')).arg(to_byte(swatch.m_color.m_green), 2, 16, QChar('0')).arg(
    to_byte(swatch.m_color.m_blue), 2, 16, QChar('0')).toUpper();
}

QString Rellu::get_character_style_label(const CharacterStyle& style) {
  auto& font = style.m_style.m_font;
  auto name = QString::fromStdString(font.m_family);
  if(!font.m_style.empty() && font.m_style != "Regular") {
    name += " " + QString::fromStdString(font.m_style);
  }
  auto size = QString::number(std::round(font.m_size * 100) / 100);
  return QString("%1 %2 %3pt").arg(name, EM_DASH, size);
}
