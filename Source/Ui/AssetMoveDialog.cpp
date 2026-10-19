#include <algorithm>
#include <utility>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QScreen>
#include "Rellu/Commands/AssetCommands.hpp"
#include "Rellu/Ui/AssetMoveDialog.hpp"
#include "Icons.hpp"

using namespace Rellu;

namespace {
  constexpr auto WIDTH = 240;
  constexpr auto ROW_HEIGHT = 28;
  constexpr auto ICON_SIZE = 12;
  const auto LIBRARY_NAME = QString("Document Assets");
  const auto NEW_GROUP_NAME = QString("New group");
  const auto NUMBER_SEPARATOR = QString(QChar(0xA0));
  const auto EMPTY_MESSAGE = QString("Move items here or create a subgroup");
  const auto SLASH_ERROR =
    QString("Slashes are used to organize groups in path view.");
  const auto DUPLICATE_ERROR =
    QString("A group with the same name already exists at this level.");

  QString get_section_name(const std::string& section) {
    if(section == COLOR_SECTION) {
      return "Colors";
    } else if(section == CHARACTER_STYLE_SECTION) {
      return "Character Styles";
    }
    return "Components";
  }

  const AssetGroup* find_group(
      const Resources& resources, const std::string& id) {
    auto group = std::find_if(resources.m_asset_groups.begin(),
      resources.m_asset_groups.end(), [&] (const auto& group) {
        return group.m_id == id;
      });
    if(group == resources.m_asset_groups.end()) {
      return nullptr;
    }
    return &*group;
  }
}

AssetMoveDialog::AssetMoveDialog(
    Editor& editor, std::vector<NodeId> assets, QWidget* parent)
    : QFrame(parent, Qt::Popup),
      m_editor(&editor),
      m_assets(std::move(assets)),
      m_is_creating(false) {
  setObjectName("asset_move_dialog");
  setAttribute(Qt::WA_DeleteOnClose);
  setFrameShape(QFrame::StyledPanel);
  setFixedWidth(WIDTH);
  if(!m_assets.empty()) {
    m_section = find_section(
      m_editor->get_document().get_resources(), m_assets.front());
  }
  m_group = m_section;
  auto layout = new QVBoxLayout(this);
  layout->setContentsMargins(12, 10, 12, 10);
  layout->setSpacing(6);
  auto header = new QHBoxLayout();
  m_title = new QLabel();
  m_title->setObjectName("asset_move_title");
  header->addWidget(m_title, 1);
  auto close = new QToolButton();
  close->setObjectName("asset_move_close");
  close->setAutoRaise(true);
  close->setText("X");
  close->setAccessibleName("Close");
  QObject::connect(close, &QToolButton::clicked, this, [this] {
    this->close();
  });
  header->addWidget(close);
  layout->addLayout(header);
  auto location = new QHBoxLayout();
  m_back = new QToolButton();
  m_back->setAutoRaise(true);
  m_back->setIcon(get_icon(Glyph::CHEVRON_LEFT));
  m_back->setIconSize(QSize(ICON_SIZE, ICON_SIZE));
  m_back->setAccessibleName("Back");
  QObject::connect(m_back, &QToolButton::clicked, this, [this] {
    go_back();
  });
  location->addWidget(m_back);
  m_location = new QLabel();
  m_location->setObjectName("asset_move_location");
  location->addWidget(m_location, 1);
  layout->addLayout(location);
  m_rows = new QWidget();
  m_rows_layout = new QVBoxLayout(m_rows);
  m_rows_layout->setContentsMargins(0, 0, 0, 0);
  m_rows_layout->setSpacing(0);
  layout->addWidget(m_rows);
  m_name = new QLineEdit();
  m_name->setPlaceholderText("Group name");
  QObject::connect(m_name, &QLineEdit::textChanged, this, [this] {
    refresh();
  });
  layout->addWidget(m_name);
  m_error = new QLabel();
  m_error->setObjectName("asset_move_error");
  m_error->setWordWrap(true);
  layout->addWidget(m_error);
  auto footer = new QHBoxLayout();
  m_add = new QToolButton();
  m_add->setAutoRaise(true);
  m_add->setIcon(get_icon(Glyph::PLUS));
  m_add->setIconSize(QSize(ICON_SIZE, ICON_SIZE));
  m_add->setToolTip("Add group");
  m_add->setAccessibleName("Add group");
  QObject::connect(m_add, &QToolButton::clicked, this, [this] {
    start_creating();
  });
  footer->addWidget(m_add);
  footer->addStretch(1);
  m_cancel = new QPushButton("Cancel");
  QObject::connect(m_cancel, &QPushButton::clicked, this, [this] {
    cancel_creating();
  });
  footer->addWidget(m_cancel);
  m_submit = new QPushButton();
  m_submit->setDefault(true);
  QObject::connect(m_submit, &QPushButton::clicked, this, [this] {
    submit();
  });
  footer->addWidget(m_submit);
  layout->addLayout(footer);
  refresh();
}

QString AssetMoveDialog::get_title() const {
  if(m_is_creating) {
    return "Create new group";
  }
  return "Move to";
}

const std::string& AssetMoveDialog::get_group() const {
  return m_group;
}

QString AssetMoveDialog::get_location() const {
  if(m_group.empty()) {
    return LIBRARY_NAME;
  }
  if(auto group =
      find_group(m_editor->get_document().get_resources(), m_group)) {
    return QString::fromStdString(group->m_name);
  }
  return get_section_name(m_group);
}

std::vector<AssetMoveDialog::Entry> AssetMoveDialog::get_entries() const {
  auto entries = std::vector<Entry>();
  if(m_group.empty()) {
    for(auto& section : {COLOR_SECTION, CHARACTER_STYLE_SECTION}) {
      entries.push_back(
        Entry(section, get_section_name(section), section == m_section));
    }
    entries.push_back(Entry(std::string(), get_section_name(""), false));
    return entries;
  }
  auto& resources = m_editor->get_document().get_resources();
  for(auto& entry : list_assets(resources, m_group)) {
    if(entry.m_group.empty()) {
      continue;
    }
    if(auto group = find_group(resources, entry.m_group)) {
      entries.push_back(
        Entry(entry.m_group, QString::fromStdString(group->m_name)));
    }
  }
  return entries;
}

bool AssetMoveDialog::can_go_back() const {
  return !m_is_creating && !m_group.empty();
}

bool AssetMoveDialog::is_creating() const {
  return m_is_creating;
}

bool AssetMoveDialog::can_submit() const {
  if(m_is_creating) {
    return !m_name->text().trimmed().isEmpty() && get_name_error().isEmpty();
  }
  return !m_group.empty() && !m_assets.empty();
}

bool AssetMoveDialog::open(const std::string& group) {
  if(m_is_creating) {
    return false;
  }
  for(auto& entry : get_entries()) {
    if(entry.m_group == group && entry.m_is_enabled && !group.empty()) {
      m_group = group;
      refresh();
      return true;
    }
  }
  return false;
}

void AssetMoveDialog::go_back() {
  if(!can_go_back()) {
    return;
  }
  m_group = get_parent(m_group);
  refresh();
}

void AssetMoveDialog::start_creating() {
  if(m_is_creating || m_group.empty()) {
    return;
  }
  auto names = std::vector<QString>();
  for(auto& entry : get_entries()) {
    names.push_back(entry.m_name);
  }
  std::sort(names.begin(), names.end());
  auto make_name = [] (int number) {
    if(number == 0) {
      return NEW_GROUP_NAME;
    }
    return NEW_GROUP_NAME + NUMBER_SEPARATOR + QString::number(number);
  };
  auto number = 0;
  for(auto& name : names) {
    if(name == make_name(number)) {
      ++number;
    }
  }
  m_is_creating = true;
  m_name->setText(make_name(number));
  refresh();
  m_name->selectAll();
  m_name->setFocus();
}

void AssetMoveDialog::set_name(const QString& name) {
  m_name->setText(name);
}

bool AssetMoveDialog::submit() {
  if(!can_submit()) {
    return false;
  }
  if(m_is_creating) {
    auto command = std::make_unique<AddAssetGroupCommand>(
      m_group, m_name->text().trimmed().toStdString());
    auto group = command->get_group();
    if(!m_editor->execute(std::move(command))) {
      return false;
    }
    m_is_creating = false;
    m_group = group;
    refresh();
    return true;
  }
  auto is_moved = static_cast<bool>(m_editor->execute(
    std::make_unique<MoveAssetsCommand>(m_assets, m_group)));
  close();
  return is_moved;
}

void AssetMoveDialog::cancel_creating() {
  if(!m_is_creating) {
    return;
  }
  m_is_creating = false;
  refresh();
}

void AssetMoveDialog::popup(QPoint position) {
  adjustSize();
  auto area = QRect();
  if(auto screen = QGuiApplication::screenAt(position)) {
    area = screen->availableGeometry();
  }
  auto point = position;
  if(!area.isEmpty()) {
    point.setX(std::clamp(point.x(), area.left(),
      std::max(area.left(), area.right() - width())));
    point.setY(std::clamp(point.y(), area.top(),
      std::max(area.top(), area.bottom() - height())));
  }
  move(point);
  show();
}

void AssetMoveDialog::keyPressEvent(QKeyEvent* event) {
  if(event->key() == Qt::Key_Escape) {
    if(m_is_creating) {
      cancel_creating();
    } else {
      close();
    }
    event->accept();
    return;
  } else if(event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    submit();
    event->accept();
    return;
  }
  QFrame::keyPressEvent(event);
}

std::string AssetMoveDialog::get_parent(const std::string& group) const {
  if(auto entry =
      find_group(m_editor->get_document().get_resources(), group)) {
    return entry->m_parent;
  }
  return std::string();
}

QString AssetMoveDialog::get_name_error() const {
  auto name = m_name->text().trimmed();
  if(name.contains('/') || name.contains('\\')) {
    return SLASH_ERROR;
  }
  for(auto& entry : get_entries()) {
    if(entry.m_name.compare(name, Qt::CaseInsensitive) == 0) {
      return DUPLICATE_ERROR;
    }
  }
  return QString();
}

void AssetMoveDialog::refresh() {
  m_title->setText(get_title());
  m_back->setVisible(can_go_back());
  m_location->setText(get_location());
  m_location->setVisible(!m_is_creating);
  while(auto item = m_rows_layout->takeAt(0)) {
    if(auto widget = item->widget()) {
      widget->deleteLater();
    }
    delete item;
  }
  if(!m_is_creating) {
    auto entries = get_entries();
    if(entries.empty()) {
      auto message = new QLabel(EMPTY_MESSAGE);
      message->setObjectName("asset_move_empty");
      message->setWordWrap(true);
      m_rows_layout->addWidget(message);
    }
    for(auto& entry : entries) {
      auto row = new QPushButton(entry.m_name);
      row->setObjectName("asset_move_row");
      row->setFlat(true);
      row->setFixedHeight(ROW_HEIGHT);
      row->setEnabled(entry.m_is_enabled);
      row->setLayoutDirection(Qt::RightToLeft);
      row->setIcon(get_icon(Glyph::CHEVRON_RIGHT));
      row->setIconSize(QSize(ICON_SIZE, ICON_SIZE));
      row->setStyleSheet("text-align: left;");
      QObject::connect(row, &QPushButton::clicked, this,
        [this, group = entry.m_group] {
          open(group);
        });
      m_rows_layout->addWidget(row);
    }
  }
  m_rows->setVisible(!m_is_creating);
  m_name->setVisible(m_is_creating);
  auto error = m_is_creating ? get_name_error() : QString();
  m_error->setText(error);
  m_error->setVisible(!error.isEmpty());
  m_add->setVisible(!m_is_creating);
  m_add->setEnabled(!m_group.empty());
  m_cancel->setVisible(m_is_creating);
  m_submit->setText(m_is_creating ? "Create" : "Move");
  m_submit->setEnabled(can_submit());
}
