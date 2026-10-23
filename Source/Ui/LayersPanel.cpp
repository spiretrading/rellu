#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <functional>
#include <memory>
#include <utility>
#include <vector>
#include <QAbstractProxyModel>
#include <QActionGroup>
#include <QApplication>
#include <QColor>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHelpEvent>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/HeaderCommands.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/PropertyCommands.hpp"
#include "Rellu/Commands/StructureCommands.hpp"
#include "Rellu/Ui/ContextMenus.hpp"
#include "Rellu/Ui/LayersPanel.hpp"
#include "Rellu/Ui/NodeDescription.hpp"
#include "Rellu/Ui/ToolTips.hpp"
#include "Rellu/Ui/Translation.hpp"
#include "Colors.hpp"
#include "Icons.hpp"

using namespace Rellu;

namespace {
  constexpr auto INDICATOR_WIDTH = 24;
  constexpr auto EXPORT_WIDTH = 33;
  constexpr auto LOCK_WIDTH = 27;
  constexpr auto VISIBILITY_WIDTH = 35;
  constexpr auto VISIBILITY_ICON_CENTER = 13;
  const auto HOVER_GLYPH = QColor(0xB8, 0xBC, 0xC0);
  constexpr auto ICON_MARGIN = 6;
  constexpr auto NAME_PADDING = 6;
  constexpr auto SEARCH_FIELD_HEIGHT = 30;
  constexpr auto HEADING_HEIGHT = 14;
  constexpr auto HEADING_INDENT = 7;
  constexpr auto GROUP_HEADING_INDENT = 26;
  const auto BACK_RECT = QRect(12, 20, 20, 20);
  const auto BACK_COLOR = QColor(0x66, 0x66, 0x66);
  const auto CONTEXT_BACKGROUND = QColor(0xFF, 0xFF, 0xFF);
  constexpr auto INDENTATION = 20;
  constexpr auto BACK_WIDTH = 4.0 / 3;
  constexpr auto SEARCH_GLYPH_SIZE = 28.0 / 3;
  constexpr auto SEARCH_GLYPH_LEFT = 2.0 / 3;
  constexpr auto SEARCH_GLYPH_TOP = 28.0 / 3;
  constexpr auto SEARCH_TEXT_MARGIN = 17;
  constexpr auto CHEVRON_WIDTH = 28.0 / 3;
  constexpr auto CHEVRON_HEIGHT = 6.0;
  constexpr auto CHEVRON_RIGHT = 14.0 / 3;
  constexpr auto CHEVRON_TOP = 34.0 / 3;
  constexpr auto FILTER_INSET = 4;
  constexpr auto FILTER_REGION_WIDTH = 62.0 / 3;
  constexpr auto SEARCH_TEXT_GAP = 2;
  const auto UNDERLINE_COLOR = QColor(0xCA, 0xCA, 0xCA);
  const auto FILTERS = std::to_array<std::pair<LayerFilter, const char*>>({
    std::pair(LayerFilter::ALL, "All Items"),
    std::pair(LayerFilter::IMAGES, "Images"),
    std::pair(LayerFilter::SHAPES, "Shapes"),
    std::pair(LayerFilter::TEXT, "Text"),
    std::pair(LayerFilter::VIDEO, "Video")});

  QRectF get_underline_rect(const QWidget& field) {
    auto ratio = field.devicePixelRatioF();
    auto origin = QPointF(field.mapTo(field.window(), QPoint(0, 0))) * ratio;
    auto left = std::lround(origin.x());
    auto right = std::lround(origin.x() + field.width() * ratio);
    auto bottom = std::lround(origin.y() + field.height() * ratio);
    auto top = bottom - std::max(std::lround(ratio), 1L);
    return QRectF((left - origin.x()) / ratio, (top - origin.y()) / ratio,
      (right - left) / ratio, (bottom - top) / ratio);
  }

  QRectF snap_glyph(
      const QWidget& field, double left, double top, double width,
      double height) {
    auto ratio = field.devicePixelRatioF();
    auto origin = QPointF(field.mapTo(field.window(), QPoint(0, 0))) * ratio;
    auto x = std::lround(origin.x() + left * ratio);
    auto y = std::lround(origin.y() + top * ratio);
    return QRectF((x - origin.x()) / ratio, (y - origin.y()) / ratio,
      std::lround(width * ratio) / ratio, std::lround(height * ratio) / ratio);
  }

  QRectF get_search_glyph_rect(const QWidget& field) {
    return snap_glyph(field, SEARCH_GLYPH_LEFT, SEARCH_GLYPH_TOP,
      SEARCH_GLYPH_SIZE, SEARCH_GLYPH_SIZE);
  }

  QRectF get_chevron_rect(const QWidget& field) {
    auto ratio = field.devicePixelRatioF();
    auto width = std::lround(CHEVRON_WIDTH * ratio) / ratio;
    auto right = snap_glyph(field, field.width() - CHEVRON_RIGHT, CHEVRON_TOP,
      CHEVRON_WIDTH, CHEVRON_HEIGHT);
    return right.translated(-width, 0);
  }

  class BackButton : public QToolButton {
    protected:
      void paintEvent(QPaintEvent*) override {
        auto painter = QPainter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        auto pen = QPen(BACK_COLOR, BACK_WIDTH);
        pen.setCapStyle(Qt::FlatCap);
        pen.setJoinStyle(Qt::MiterJoin);
        painter.setPen(pen);
        auto origin = QPointF(BACK_RECT.topLeft());
        auto points = std::array{QPointF(20.75, 26.25) - origin,
          QPointF(17, 30) - origin, QPointF(20.75, 33.75) - origin};
        painter.drawPolyline(points.data(), static_cast<int>(points.size()));
      }
  };

  class SearchGlyphs : public QWidget {
    public:
      explicit SearchGlyphs(QWidget* field)
        : QWidget(field),
          m_ratio(0) {
        setObjectName("search_glyphs");
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
      }

    protected:
      void paintEvent(QPaintEvent*) override {
        auto ratio = devicePixelRatioF();
        if(ratio != m_ratio) {
          m_ratio = ratio;
          auto size = [&] (double width, double height) {
            return QSize(static_cast<int>(std::lround(width * ratio)),
              static_cast<int>(std::lround(height * ratio)));
          };
          m_icon = make_fitted_glyph(Glyph::SEARCH,
            size(SEARCH_GLYPH_SIZE, SEARCH_GLYPH_SIZE), ratio);
          m_chevron = make_fitted_glyph(Glyph::CHEVRON_DOWN,
            size(CHEVRON_WIDTH, CHEVRON_HEIGHT), ratio);
        }
        auto painter = QPainter(this);
        auto& field = *parentWidget();
        painter.fillRect(get_underline_rect(field), UNDERLINE_COLOR);
        painter.drawPixmap(get_search_glyph_rect(field).topLeft(), m_icon);
        painter.drawPixmap(get_chevron_rect(field).topLeft(), m_chevron);
      }

    private:
      double m_ratio;
      QPixmap m_icon;
      QPixmap m_chevron;
  };

  bool is_image(const Node& node) {
    auto shape = node.get_shape();
    return shape && std::holds_alternative<ImageFill>(shape->m_style.m_fill);
  }

  bool is_plain_shape(const Node& node) {
    auto shape = node.get_shape();
    if(!shape || std::holds_alternative<CompoundShape>(shape->m_geometry)) {
      return false;
    }
    return !std::holds_alternative<RectangleShape>(shape->m_geometry) ||
      !node.m_header.m_aspect_lock || !is_image(node);
  }

  class Guard {
    public:
      explicit Guard(bool& flag)
          : m_flag(&flag),
            m_previous(flag) {
        flag = true;
      }

      ~Guard() {
        *m_flag = m_previous;
      }

    private:
      bool* m_flag;
      bool m_previous;

      Guard(const Guard&) = delete;
      Guard& operator =(const Guard&) = delete;
  };

  class StateDelegate : public QStyledItemDelegate {
    public:
      explicit StateDelegate(QTreeView* view)
        : QStyledItemDelegate(view),
          m_view(view) {}

      bool is_elided(QStyleOptionViewItem option,
          const QModelIndex& index) const {
        initStyleOption(&option, index);
        auto style = option.widget ? option.widget->style() :
          QApplication::style();
        auto text = style->subElementRect(
          QStyle::SE_ItemViewItemText, &option, option.widget);
        auto margin = style->pixelMetric(
          QStyle::PM_FocusFrameHMargin, nullptr, option.widget) + 1;
        return QFontMetricsF(option.font).horizontalAdvance(option.text) >
          text.width() - 2 * margin;
      }

    protected:
      void initStyleOption(QStyleOptionViewItem* option,
          const QModelIndex& index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        auto is_hovered =
          static_cast<bool>(option->state & QStyle::State_MouseOver);
        if(index.column() == LayersModel::NAME && m_view->isExpanded(index)) {
          if(auto icon = index.data(LayersModel::EXPANDED_ICON_ROLE);
              icon.isValid()) {
            option->icon = icon.value<QIcon>();
          }
        } else if(index.column() == LayersModel::VISIBILITY &&
            index.data(LayersModel::VISIBLE_ROLE).toBool() && !is_hovered) {
          option->icon = QIcon();
          option->features &= ~QStyleOptionViewItem::HasDecoration;
        } else if(index.column() == LayersModel::LOCK &&
            !index.data(LayersModel::LOCKED_ROLE).toBool() && is_hovered) {
          static auto unlocked = make_icon(Glyph::LOCK, HOVER_GLYPH);
          option->icon = unlocked;
          option->features |= QStyleOptionViewItem::HasDecoration;
        } else if(index.column() == LayersModel::EXPORT &&
            !index.data(LayersModel::MARKED_ROLE).toBool() && is_hovered &&
            index.data(LayersModel::ID_ROLE).isValid()) {
          option->icon = get_export_mark_icon(false);
          option->features |= QStyleOptionViewItem::HasDecoration;
        }
      }

      void paint(QPainter* painter, const QStyleOptionViewItem& option,
          const QModelIndex& index) const override {
        if(index.column() != LayersModel::VISIBILITY) {
          QStyledItemDelegate::paint(painter, option, index);
          return;
        }
        auto cell = option;
        initStyleOption(&cell, index);
        auto icon = cell.icon;
        auto has_icon = static_cast<bool>(
          cell.features & QStyleOptionViewItem::HasDecoration);
        cell.icon = QIcon();
        cell.features &= ~QStyleOptionViewItem::HasDecoration;
        auto style =
          cell.widget ? cell.widget->style() : QApplication::style();
        style->drawControl(
          QStyle::CE_ItemViewItem, &cell, painter, cell.widget);
        if(has_icon) {
          auto size = m_view->iconSize();
          icon.paint(painter, QRect(QPoint(
            option.rect.left() + VISIBILITY_ICON_CENTER - size.width() / 2,
            option.rect.top() + (option.rect.height() - size.height()) / 2),
            size));
        }
      }

    private:
      QTreeView* m_view;
  };

  class ContextTreeView : public QTreeView {
    public:
      std::function<bool (const QModelIndex&)> m_is_in_context;

    protected:
      void drawRow(QPainter* painter, const QStyleOptionViewItem& option,
          const QModelIndex& index) const override {
        if(m_is_in_context && m_is_in_context(index)) {
          painter->fillRect(option.rect, CONTEXT_BACKGROUND);
        }
        QTreeView::drawRow(painter, option, index);
      }

      bool viewportEvent(QEvent* event) override {
        if(event->type() != QEvent::ToolTip) {
          return QTreeView::viewportEvent(event);
        }
        auto& help = static_cast<QHelpEvent&>(*event);
        auto index = indexAt(help.pos());
        auto delegate =
          dynamic_cast<StateDelegate*>(itemDelegateForIndex(index));
        if(!index.isValid() || index.column() != LayersModel::NAME ||
            !delegate) {
          return QTreeView::viewportEvent(event);
        }
        auto option = QStyleOptionViewItem();
        initViewItemOption(&option);
        option.rect = visualRect(index);
        if(delegate->is_elided(option, index)) {
          show_tool_tip(*viewport(), index.data(Qt::DisplayRole).toString(),
            help.globalPos(), option.rect);
        }
        return true;
      }
  };

  class ResultsModel : public QAbstractProxyModel {
    public:
      struct Row {
        QPersistentModelIndex m_source;
        QString m_label;
      };

      using QAbstractProxyModel::QAbstractProxyModel;

      void set_rows(std::vector<Row> rows) {
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
      }

      QModelIndex index(int row, int column,
          const QModelIndex& parent = QModelIndex()) const override {
        if(parent.isValid() || row < 0 || row >= get_size() || column < 0 ||
            column >= LayersModel::COLUMN_COUNT) {
          return QModelIndex();
        }
        return createIndex(row, column);
      }

      QModelIndex parent(const QModelIndex& index) const override {
        return QModelIndex();
      }

      int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        if(parent.isValid()) {
          return 0;
        }
        return get_size();
      }

      int columnCount(
          const QModelIndex& parent = QModelIndex()) const override {
        return LayersModel::COLUMN_COUNT;
      }

      bool hasChildren(
          const QModelIndex& parent = QModelIndex()) const override {
        return !parent.isValid() && !m_rows.empty();
      }

      QModelIndex mapToSource(const QModelIndex& index) const override {
        if(!index.isValid() || index.row() >= get_size()) {
          return QModelIndex();
        }
        auto& source = m_rows[index.row()].m_source;
        if(!source.isValid()) {
          return QModelIndex();
        }
        return source.sibling(source.row(), index.column());
      }

      QModelIndex mapFromSource(const QModelIndex& source) const override {
        if(!source.isValid()) {
          return QModelIndex();
        }
        auto name = source.sibling(source.row(), LayersModel::NAME);
        for(auto i = 0; i != get_size(); ++i) {
          if(m_rows[i].m_source == name) {
            return index(i, source.column());
          }
        }
        return QModelIndex();
      }

      QVariant data(const QModelIndex& index, int role) const override {
        if(index.isValid() && index.row() < get_size() &&
            !m_rows[index.row()].m_source.isValid()) {
          if(index.column() != LayersModel::NAME) {
            return QVariant();
          } else if(role == Qt::DisplayRole) {
            return m_rows[index.row()].m_label;
          } else if(role == Qt::ForegroundRole) {
            return Colors::SECONDARY_TEXT;
          }
          return QVariant();
        }
        return QAbstractProxyModel::data(index, role);
      }

      Qt::ItemFlags flags(const QModelIndex& index) const override {
        if(!index.isValid() || index.row() >= get_size() ||
            !m_rows[index.row()].m_source.isValid()) {
          return Qt::NoItemFlags;
        }
        return QAbstractProxyModel::flags(index) &
          ~(Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
      }

    private:
      std::vector<Row> m_rows;

      int get_size() const {
        return static_cast<int>(m_rows.size());
      }
  };

  ResultsModel& get_results_model(QAbstractProxyModel& model) {
    return static_cast<ResultsModel&>(model);
  }
}

LayersPanel::LayersPanel(QWidget* parent)
    : QWidget(parent),
      m_model(new LayersModel(this)),
      m_view(new ContextTreeView()),
      m_results_model(nullptr),
      m_results(new QTreeView()),
      m_search(new QLineEdit()),
      m_filter_button(new QToolButton()),
      m_search_glyphs(nullptr),
      m_back(new BackButton()),
      m_heading(new QLabel()),
      m_editor(nullptr),
      m_actions(nullptr),
      m_filter(LayerFilter::ALL),
      m_is_synchronizing(false),
      m_is_filtered(false),
      m_is_pasteboard_open(false),
      m_is_results_stale(true) {
  setObjectName("layers_panel");
  auto search = new QWidget();
  auto search_layout = new QHBoxLayout(search);
  search_layout->setContentsMargins(12, 12, 12, 0);
  search_layout->setSpacing(4);
  m_search->setObjectName("search_field");
  m_search->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  m_search->setFixedHeight(SEARCH_FIELD_HEIGHT);
  m_search->setPlaceholderText(translate("layers", "All Items"));
  m_search->setAccessibleName(translate("layers", "Search Layers"));
  m_search->setClearButtonEnabled(true);
  m_search->installEventFilter(this);
  search_layout->addWidget(m_search, 1);
  connect(m_search, &QLineEdit::textChanged, this, [this] (const auto& text) {
    set_search(text);
  });
  m_filter_button->setObjectName("layers_filter");
  m_filter_button->setCursor(Qt::ArrowCursor);
  m_filter_button->setAutoRaise(true);
  m_filter_button->setFocusPolicy(Qt::NoFocus);
  m_filter_button->setAccessibleName(translate("layers", "Filter"));
  m_filter_button->setPopupMode(QToolButton::InstantPopup);
  auto filters = new QMenu(m_filter_button);
  auto group = new QActionGroup(filters);
  for(auto& [filter, label] : FILTERS) {
    auto action = filters->addAction(translate("layers", label));
    action->setCheckable(true);
    action->setChecked(filter == LayerFilter::ALL);
    group->addAction(action);
    connect(action, &QAction::triggered, this, [this, filter] {
      set_filter(filter);
    });
  }
  m_filter_button->setMenu(filters);
  m_filter_button->setParent(m_search);
  m_search_glyphs = new SearchGlyphs(m_search);
  m_search->setTextMargins(SEARCH_TEXT_MARGIN, 0,
    static_cast<int>(std::ceil(CHEVRON_WIDTH)) + FILTER_INSET +
      SEARCH_TEXT_GAP, 0);
  auto heading = new QWidget();
  auto heading_layout = new QHBoxLayout(heading);
  heading_layout->setContentsMargins(12, 23, 16, 1);
  heading_layout->setSpacing(0);
  m_back->setObjectName("panel_button");
  m_back->setAutoRaise(true);
  m_back->setFocusPolicy(Qt::NoFocus);
  m_back->setToolTip(translate("layers", "Artboards"));
  m_back->setAccessibleName(translate("layers", "Layers back"));
  m_back->setParent(heading);
  m_back->setGeometry(BACK_RECT);
  m_heading->setObjectName("layers_heading");
  m_heading->setFixedHeight(HEADING_HEIGHT);
  m_heading->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  m_heading->setContentsMargins(HEADING_INDENT, 0, 0, 0);
  heading_layout->addWidget(m_heading, 1);
  m_back->raise();
  m_results_model = new ResultsModel(this);
  m_results_model->setSourceModel(m_model);
  m_view->setObjectName("layers");
  m_view->setModel(m_model);
  m_results->setObjectName("layers");
  m_results->setModel(m_results_model);
  for(auto view : {m_view, m_results}) {
    view->setItemDelegate(new StateDelegate(view));
    view->setUniformRowHeights(true);
    view->setRootIsDecorated(false);
    view->setHeaderHidden(true);
    view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setAllColumnsShowFocus(true);
    view->setIndentation(INDENTATION);
    view->setIconSize(QSize(16, 16));
    view->setFrameShape(QFrame::NoFrame);
    view->setMouseTracking(true);
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    view->viewport()->installEventFilter(this);
    auto header = view->header();
    header->setStretchLastSection(false);
    header->setMinimumSectionSize(INDICATOR_WIDTH);
    header->setSectionResizeMode(LayersModel::NAME, QHeaderView::Stretch);
    for(auto [column, width] : std::to_array<std::pair<int, int>>({
        {LayersModel::EXPORT, EXPORT_WIDTH}, {LayersModel::LOCK, LOCK_WIDTH},
        {LayersModel::VISIBILITY, VISIBILITY_WIDTH}})) {
      header->setSectionResizeMode(column, QHeaderView::Fixed);
      header->resizeSection(column, width);
    }
    header->moveSection(header->visualIndex(LayersModel::EXPORT), 1);
    header->moveSection(header->visualIndex(LayersModel::LOCK), 2);
    connect(view, &QTreeView::customContextMenuRequested, this,
      [this, view] (const QPoint& position) {
        show_context_menu(*view, position);
      });
  }
  static_cast<ContextTreeView*>(m_view)->m_is_in_context =
    [this] (const QModelIndex& index) {
      if(!m_editor) {
        return false;
      }
      auto& container = m_editor->get_context().get_container();
      if(container.is_nil()) {
        return false;
      }
      auto id = m_model->get_id(index);
      return id == container ||
        is_ancestor(m_editor->get_document(), container, id);
    };
  m_view->setDragDropMode(QAbstractItemView::InternalMove);
  m_view->setDefaultDropAction(Qt::MoveAction);
  m_view->setDropIndicatorShown(true);
  m_results->setItemsExpandable(false);
  m_results->setVisible(false);
  auto layout = new QVBoxLayout(this);
  layout->setSizeConstraint(QLayout::SetNoConstraint);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(search);
  layout->addWidget(heading);
  layout->addWidget(m_view, 1);
  layout->addWidget(m_results, 1);
  connect(m_back, &QToolButton::clicked, this, [this] {
    show_artboards();
  });
  connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
    this, [this] {
      if(!m_is_synchronizing) {
        on_selection();
      }
    });
  connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged,
    this, [this] (const QModelIndex& current) {
      if(m_is_synchronizing) {
        return;
      }
      on_selection();
      if(m_activation_handler && current.isValid() &&
          m_model->get_id(current) != LayersModel::PASTEBOARD &&
          m_view->selectionModel()->isSelected(current)) {
        m_activation_handler(m_model->get_id(current));
      }
    });
  connect(m_results->selectionModel(),
    &QItemSelectionModel::selectionChanged, this, [this] {
      if(!m_is_synchronizing) {
        on_result_selection();
      }
    });
  connect(m_results->selectionModel(), &QItemSelectionModel::currentChanged,
    this, [this] (const QModelIndex& current) {
      if(m_is_synchronizing) {
        return;
      }
      on_result_selection();
      auto source = m_results_model->mapToSource(current);
      if(m_activation_handler && source.isValid() &&
          m_results->selectionModel()->isSelected(current)) {
        m_activation_handler(m_model->get_id(source));
      }
    });
  update_scope();
}

void LayersPanel::set_actions(ActionRegistry* actions) {
  m_actions = actions;
}

QString LayersPanel::get_search() const {
  return m_search->text();
}

void LayersPanel::set_search(const QString& text) {
  if(m_search->text() != text) {
    m_search->setText(text);
    return;
  }
  m_is_results_stale = true;
  update_scope();
}

LayerFilter LayersPanel::get_filter() const {
  return m_filter;
}

void LayersPanel::set_filter(LayerFilter filter) {
  m_filter = filter;
  for(auto& entry : FILTERS) {
    if(entry.first == filter) {
      m_search->setPlaceholderText(translate("layers", entry.second));
    }
  }
  if(auto menu = m_filter_button->menu()) {
    auto actions = menu->actions();
    for(auto i = 0; i != actions.size(); ++i) {
      actions[i]->setChecked(FILTERS[i].first == filter);
    }
  }
  m_is_results_stale = true;
  update_scope();
}

bool LayersPanel::is_filtered_out(const NodeId& id) {
  if(is_searching()) {
    return !m_matches.contains(id);
  }
  return !m_model->find(id).isValid();
}

bool LayersPanel::toggle_visibility(const NodeId& id) {
  auto document = m_model->get_document();
  if(!m_editor || !document || !document->find(id) ||
      is_inside_compound(*document, id)) {
    return false;
  }
  auto header = find_rendered_header(*document, id);
  if(!header) {
    return false;
  }
  auto is_visible = header->m_is_visible;
  try {
    return static_cast<bool>(m_editor->execute(
      std::make_unique<SetVisibilityCommand>(std::vector{id}, !is_visible)));
  } catch(const std::exception&) {
    return false;
  }
}

bool LayersPanel::toggle_lock(const NodeId& id) {
  auto document = m_model->get_document();
  if(!m_editor || !document || !document->find(id)) {
    return false;
  }
  auto header = find_rendered_header(*document, id);
  if(!header) {
    return false;
  }
  auto is_locked = header->m_is_locked;
  try {
    return static_cast<bool>(m_editor->execute(
      std::make_unique<SetLockCommand>(std::vector{id}, !is_locked)));
  } catch(const std::exception&) {
    return false;
  }
}

bool LayersPanel::toggle_export_mark(const NodeId& id) {
  auto document = m_model->get_document();
  if(!m_editor || !document || !document->find(id)) {
    return false;
  }
  auto header = find_rendered_header(*document, id);
  if(!header) {
    return false;
  }
  try {
    auto command = std::make_unique<SetExportMarkCommand>(
      std::vector{id}, !header->m_is_marked_for_export);
    return static_cast<bool>(m_editor->execute(std::move(command)));
  } catch(const std::exception&) {
    return false;
  }
}

bool LayersPanel::move(const std::vector<NodeId>& nodes, const NodeId& parent,
    std::size_t index) {
  if(!m_editor || !m_model->is_movable(nodes, parent)) {
    return false;
  }
  auto ordered = nodes;
  sort_by_z_order(m_editor->get_document(), ordered);
  try {
    return static_cast<bool>(m_editor->execute(
      std::make_unique<ReparentCommand>(std::move(ordered), parent, index)));
  } catch(const std::exception&) {
    return false;
  }
}

QMenu* LayersPanel::make_context_menu(QWidget* parent) {
  if(!m_actions) {
    return new QMenu(parent);
  }
  return Rellu::make_context_menu(*m_actions, ContextMenuKind::LAYERS, parent);
}

void LayersPanel::show_context_menu(QTreeView& view, const QPoint& position) {
  auto index = view.indexAt(position);
  if(&view == m_results) {
    index = m_results_model->mapToSource(index);
  }
  auto id = m_model->get_id(index);
  if(index.isValid() && m_editor && id != LayersModel::PASTEBOARD &&
      !m_editor->get_selection().contains(id)) {
    m_editor->get_selection().select(id);
  }
  auto menu = make_context_menu(&view);
  menu->setAttribute(Qt::WA_DeleteOnClose);
  if(menu->isEmpty()) {
    menu->deleteLater();
    return;
  }
  menu->popup(view.viewport()->mapToGlobal(position));
}

bool LayersPanel::eventFilter(QObject* watched, QEvent* event) {
  if(watched == m_search && event->type() == QEvent::Resize) {
    auto left = static_cast<int>(
      std::floor(m_search->width() - FILTER_REGION_WIDTH));
    m_filter_button->setGeometry(
      left, 0, m_search->width() - left, m_search->height());
    m_search_glyphs->setGeometry(m_search->rect());
    m_search_glyphs->raise();
  }
  if(watched == m_search) {
    if(event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
      m_search->clearFocus();
      return true;
    } else if(event->type() == QEvent::MouseButtonPress &&
        !m_search->hasFocus()) {
      m_search->setFocus(Qt::MouseFocusReason);
      m_search->selectAll();
      return true;
    }
    return QWidget::eventFilter(watched, event);
  }
  auto is_results = watched == m_results->viewport();
  if((watched != m_view->viewport() && !is_results) ||
      (event->type() != QEvent::MouseButtonPress &&
        event->type() != QEvent::MouseButtonDblClick)) {
    return QWidget::eventFilter(watched, event);
  }
  auto mouse = static_cast<QMouseEvent*>(event);
  if(mouse->button() != Qt::LeftButton) {
    return QWidget::eventFilter(watched, event);
  }
  auto position = mouse->position().toPoint();
  auto index = is_results ?
    m_results_model->mapToSource(m_results->indexAt(position)) :
    m_view->indexAt(position);
  if(!is_results && index.isValid() && index.column() == LayersModel::NAME) {
    auto rect = m_view->visualRect(index);
    if(m_view->itemsExpandable() && m_model->hasChildren(index) &&
        position.x() < rect.left() + NAME_PADDING +
          m_view->iconSize().width() + ICON_MARGIN) {
      if(event->type() == QEvent::MouseButtonPress) {
        auto id = m_model->get_id(index);
        set_current(id);
        auto group = m_model->find(id);
        m_view->setExpanded(group, !m_view->isExpanded(group));
      }
      return true;
    }
    if(event->type() == QEvent::MouseButtonDblClick &&
        m_model->get_id(index) == LayersModel::PASTEBOARD) {
      m_is_pasteboard_open = true;
      update_scope();
      return true;
    }
  }
  if(!index.isValid() || (index.column() != LayersModel::VISIBILITY &&
      index.column() != LayersModel::LOCK &&
      index.column() != LayersModel::EXPORT)) {
    return QWidget::eventFilter(watched, event);
  }
  if(!m_editor) {
    return QWidget::eventFilter(watched, event);
  } else if(event->type() == QEvent::MouseButtonPress) {
    auto id = m_model->get_id(index);
    if(index.column() == LayersModel::VISIBILITY) {
      toggle_visibility(id);
    } else if(index.column() == LayersModel::LOCK) {
      toggle_lock(id);
    } else {
      toggle_export_mark(id);
    }
  }
  return true;
}

bool LayersPanel::is_searching() const {
  return !m_search->text().trimmed().isEmpty() ||
    m_filter != LayerFilter::ALL;
}

bool LayersPanel::is_match(const Node& node) const {
  auto& document = *m_model->get_document();
  auto text = m_search->text().trimmed();
  if(!text.isEmpty() &&
      !get_display_name(document, node).contains(text, Qt::CaseInsensitive)) {
    return false;
  }
  auto displayed = get_displayed_node(document, node);
  if(!displayed) {
    return m_filter == LayerFilter::ALL;
  }
  switch(m_filter) {
    case LayerFilter::IMAGES:
      return is_image(*displayed);
    case LayerFilter::SHAPES:
      return is_plain_shape(*displayed);
    case LayerFilter::TEXT:
      return static_cast<bool>(displayed->get_text());
    case LayerFilter::VIDEO:
      return false;
    default:
      return true;
  }
}

void LayersPanel::collect_matches(
    const NodeId& id, std::vector<NodeId>& matches) const {
  auto& node = m_model->get_document()->get(id);
  if(is_match(node)) {
    matches.push_back(id);
  }
  auto group = node.get_group();
  if(group && group->m_repeat_grid) {
    if(!node.m_children.empty()) {
      collect_matches(node.m_children.front(), matches);
    }
    return;
  }
  for(auto i = node.m_children.rbegin(); i != node.m_children.rend(); ++i) {
    collect_matches(*i, matches);
  }
}

void LayersPanel::update_results() {
  m_is_results_stale = false;
  m_matches.clear();
  auto rows = std::vector<ResultsModel::Row>();
  auto document = m_model->get_document();
  if(!document) {
    get_results_model(*m_results_model).set_rows(std::move(rows));
    return;
  }
  auto add_group = [&] (const QString& label,
      const std::vector<NodeId>& roots) {
    auto matches = std::vector<NodeId>();
    for(auto& root : roots) {
      collect_matches(root, matches);
    }
    if(matches.empty()) {
      return;
    }
    rows.push_back(ResultsModel::Row(QPersistentModelIndex(), label));
    for(auto& id : matches) {
      m_matches.insert(id);
      rows.push_back(ResultsModel::Row(
        QPersistentModelIndex(m_model->find(id)), QString()));
    }
  };
  auto artboards = std::vector<NodeId>();
  auto others = std::vector<NodeId>();
  auto& canvas = document->get_canvas();
  for(auto i = canvas.rbegin(); i != canvas.rend(); ++i) {
    if(document->get(*i).get_artboard()) {
      artboards.push_back(*i);
    } else {
      others.push_back(*i);
    }
  }
  if(artboards.empty()) {
    add_group(translate("layers", "CANVAS"), others);
  } else {
    for(auto& artboard : artboards) {
      add_group(get_display_name(*document, document->get(artboard)).
        toUpper(), {artboard});
    }
    add_group(translate("layers", "PASTEBOARD"), others);
  }
  get_results_model(*m_results_model).set_rows(std::move(rows));
}

void LayersPanel::select_results() {
  auto guard = Guard(m_is_synchronizing);
  auto ids = std::vector<NodeId>();
  auto primary = m_current;
  if(m_editor) {
    ids = m_editor->get_selection().get_nodes();
    primary = m_editor->get_selection().get_primary();
  } else if(!m_current.is_nil()) {
    ids.push_back(m_current);
  }
  auto items = QItemSelection();
  for(auto& id : ids) {
    if(m_matches.contains(id)) {
      auto index = m_results_model->mapFromSource(m_model->find(id));
      items.select(index, index);
    }
  }
  auto model = m_results->selectionModel();
  model->select(items,
    QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  if(m_matches.contains(primary)) {
    auto index = m_results_model->mapFromSource(m_model->find(primary));
    model->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
    m_results->scrollTo(index);
  }
}

void LayersPanel::on_result_selection() {
  auto model = m_results->selectionModel();
  auto current = model->currentIndex();
  if(!model->isSelected(current)) {
    current = QModelIndex();
  }
  auto nodes = std::vector<NodeId>();
  for(auto& row : model->selectedRows(LayersModel::NAME)) {
    if(auto id = m_model->get_id(m_results_model->mapToSource(row));
        !id.is_nil()) {
      nodes.push_back(id);
    }
  }
  auto source = m_results_model->mapToSource(current);
  if(m_editor) {
    auto guard = Guard(m_is_synchronizing);
    m_editor->get_selection().select(nodes, m_model->get_id(source));
  }
  if(!source.isValid() && !nodes.empty()) {
    source = m_model->find(nodes.front());
  }
  on_current(source);
}

void LayersPanel::exit_search() {
  m_search->clear();
  set_filter(LayerFilter::ALL);
}

QTreeView& LayersPanel::get_results() {
  return *m_results;
}

LayersModel& LayersPanel::get_model() {
  return *m_model;
}

QTreeView& LayersPanel::get_view() {
  return *m_view;
}

void LayersPanel::set_document(Document* document) {
  m_subscription.release();
  m_document_subscription.release();
  m_context_subscription.release();
  m_editor = nullptr;
  m_model->set_rename_handler(nullptr);
  m_model->set_move_handler(nullptr);
  m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_results->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_model->set_document(document);
  m_is_results_stale = true;
  if(document) {
    m_document_subscription = document->subscribe([this] (const auto&) {
      m_is_results_stale = true;
      update_scope();
    });
  }
  on_current(QModelIndex());
}

Editor* LayersPanel::get_editor() const {
  return m_editor;
}

void LayersPanel::set_editor(Editor* editor) {
  m_subscription.release();
  m_document_subscription.release();
  m_context_subscription.release();
  m_editor = nullptr;
  if(!editor) {
    m_model->set_rename_handler(nullptr);
    m_model->set_move_handler(nullptr);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_results->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_model->set_document(nullptr);
    m_is_results_stale = true;
    on_current(QModelIndex());
    return;
  }
  m_model->set_document(&editor->get_document());
  m_is_results_stale = true;
  m_editor = editor;
  m_model->set_rename_handler([this] (const auto& id, const auto& name) {
    if(!m_editor) {
      return;
    }
    try {
      m_editor->execute(
        std::make_unique<RenameCommand>(id, name.toStdString()));
    } catch(const std::exception&) {}
  });
  m_model->set_move_handler([this] (const auto& nodes, const auto& parent,
      auto index) {
    return move(nodes, parent, index);
  });
  for(auto view : {m_view, m_results}) {
    view->setEditTriggers(
      QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
  }
  m_document_subscription =
    editor->get_document().subscribe([this] (const auto&) {
      m_is_results_stale = true;
      update_scope();
    });
  m_subscription = m_editor->get_selection().subscribe([this] {
    synchronize();
  });
  m_context_subscription = m_editor->get_context().subscribe([this] {
    m_view->viewport()->update();
  });
  synchronize();
}

const NodeId& LayersPanel::get_current() const {
  return m_current;
}

void LayersPanel::set_current(const NodeId& id) {
  if(m_editor) {
    m_editor->get_selection().select(id);
    return;
  }
  auto index = m_model->find(id);
  if(!index.isValid()) {
    m_view->selectionModel()->clear();
    on_current(QModelIndex());
    return;
  }
  for(auto parent = index.parent(); parent.isValid();
      parent = parent.parent()) {
    m_view->expand(parent);
  }
  m_view->selectionModel()->setCurrentIndex(index,
    QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  m_view->scrollTo(index);
}

const NodeId& LayersPanel::get_scope() const {
  return m_scope;
}

QString LayersPanel::get_heading() const {
  return m_heading->text();
}

bool LayersPanel::rename(const NodeId& id) {
  if(!m_editor) {
    return false;
  }
  auto index = m_model->find(id);
  if(!index.isValid()) {
    return false;
  }
  auto view = m_view;
  if(is_searching()) {
    index = m_results_model->mapFromSource(index);
    if(!index.isValid()) {
      return false;
    }
    view = m_results;
  }
  view->scrollTo(index);
  view->setCurrentIndex(index);
  view->edit(index);
  return static_cast<bool>(view->model()->flags(index) & Qt::ItemIsEditable);
}

void LayersPanel::set_current_handler(CurrentHandler handler) {
  m_current_handler = std::move(handler);
}

void LayersPanel::set_activation_handler(CurrentHandler handler) {
  m_activation_handler = std::move(handler);
}

void LayersPanel::synchronize() {
  if(!m_is_synchronizing) {
    m_is_pasteboard_open = false;
    if(is_searching()) {
      auto& nodes = m_editor->get_selection().get_nodes();
      if(std::ranges::any_of(nodes, [&] (const auto& id) {
          return !m_matches.contains(id);
        })) {
        exit_search();
      }
    }
  }
  auto guard = Guard(m_is_synchronizing);
  auto& selection = m_editor->get_selection();
  auto items = QItemSelection();
  for(auto& id : selection.get_nodes()) {
    auto index = m_model->find(id);
    if(!index.isValid()) {
      continue;
    }
    for(auto parent = index.parent(); parent.isValid();
        parent = parent.parent()) {
      m_view->expand(parent);
    }
    items.select(index, index);
  }
  auto model = m_view->selectionModel();
  model->select(items,
    QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  auto primary = m_model->find(selection.get_primary());
  if(primary.isValid()) {
    model->setCurrentIndex(primary, QItemSelectionModel::NoUpdate);
  }
  on_current(primary);
  if(primary.isValid()) {
    m_view->scrollTo(primary);
  }
  if(is_searching()) {
    select_results();
  }
}

void LayersPanel::on_selection() {
  auto model = m_view->selectionModel();
  auto current = model->currentIndex();
  if(!model->isSelected(current)) {
    current = QModelIndex();
  }
  if(m_editor) {
    auto nodes = std::vector<NodeId>();
    for(auto& row : model->selectedRows(LayersModel::NAME)) {
      if(auto id = m_model->get_id(row); id != LayersModel::PASTEBOARD) {
        nodes.push_back(id);
      }
    }
    auto primary = m_model->get_id(current);
    if(primary == LayersModel::PASTEBOARD) {
      primary = NodeId();
    }
    auto guard = Guard(m_is_synchronizing);
    auto pasteboard =
      QPersistentModelIndex(m_model->find(LayersModel::PASTEBOARD));
    auto is_pasteboard_selected = nodes.empty() && pasteboard.isValid() &&
      model->isSelected(pasteboard);
    m_editor->get_selection().select(nodes, primary);
    if(is_pasteboard_selected) {
      model->setCurrentIndex(pasteboard,
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }
  }
  if(current.isValid()) {
    on_current(current);
  } else {
    auto rows = model->selectedRows(LayersModel::NAME);
    on_current(rows.isEmpty() ? QModelIndex() : rows.front());
  }
}

void LayersPanel::on_current(const QModelIndex& index) {
  auto current = m_model->get_id(index);
  if(current == LayersModel::PASTEBOARD) {
    current = NodeId();
  }
  if(current == m_current) {
    update_scope();
    return;
  }
  m_current = current;
  update_scope();
  if(m_current_handler) {
    m_current_handler(m_current);
  }
}

void LayersPanel::update_scope() {
  auto document = m_model->get_document();
  auto scope = NodeId();
  auto is_searching = this->is_searching();
  if(is_searching) {
    m_scope = NodeId();
    m_view->setVisible(false);
    m_results->setVisible(true);
    m_heading->setVisible(false);
    m_back->setVisible(false);
    m_is_filtered = true;
    if(m_is_results_stale) {
      update_results();
      select_results();
    }
    return;
  } else if(m_is_filtered) {
    m_is_filtered = false;
    m_results->setVisible(false);
    m_view->setVisible(true);
    m_heading->setVisible(true);
    get_results_model(*m_results_model).set_rows(
      std::vector<ResultsModel::Row>());
    m_matches.clear();
    m_is_results_stale = true;
    m_view->collapseAll();
    m_scope = NodeId();
    m_view->setRootIndex(QModelIndex());
  }
  if(document && !m_current.is_nil() && document->find(m_current)) {
    auto artboard = find_artboard(*document, m_current);
    if(!artboard.is_nil() && artboard != m_current &&
        document->is_on_canvas(artboard)) {
      scope = artboard;
    } else if(artboard.is_nil()) {
      auto top = m_model->find(m_current);
      while(top.parent().isValid()) {
        top = top.parent();
      }
      if(m_model->get_id(top) == LayersModel::PASTEBOARD) {
        scope = LayersModel::PASTEBOARD;
      }
    }
  } else if(m_is_pasteboard_open) {
    scope = LayersModel::PASTEBOARD;
  }
  auto root = scope.is_nil() ? QModelIndex() : m_model->find(scope);
  if(!root.isValid()) {
    scope = NodeId();
  }
  if(scope != m_scope || m_view->rootIndex() != root) {
    m_scope = scope;
    if(m_scope.is_nil()) {
      m_view->collapseAll();
    }
    m_view->setRootIndex(root);
  }
  auto is_canvas = m_scope.is_nil() && document &&
    std::none_of(document->get_canvas().begin(),
      document->get_canvas().end(), [&] (const auto& id) {
        return static_cast<bool>(document->get(id).get_artboard());
      });
  m_view->setItemsExpandable(!m_scope.is_nil() || is_canvas);
  if(is_canvas) {
    m_heading->setText(translate("layers", "CANVAS"));
    m_back->setVisible(false);
    m_heading->setContentsMargins(HEADING_INDENT, 0, 0, 0);
  } else if(m_scope.is_nil()) {
    m_heading->setText(translate("layers", "ARTBOARDS"));
    m_back->setVisible(false);
    m_heading->setContentsMargins(HEADING_INDENT, 0, 0, 0);
  } else if(m_scope == LayersModel::PASTEBOARD) {
    m_heading->setText(translate("layers", "PASTEBOARD"));
    m_back->setVisible(true);
    m_heading->setContentsMargins(GROUP_HEADING_INDENT, 0, 0, 0);
  } else {
    m_heading->setText(
      get_display_name(*document, document->get(m_scope)).toUpper());
    m_back->setVisible(true);
    m_heading->setContentsMargins(GROUP_HEADING_INDENT, 0, 0, 0);
  }
}

void LayersPanel::show_artboards() {
  if(m_scope == LayersModel::PASTEBOARD) {
    m_is_pasteboard_open = false;
    if(m_editor) {
      m_editor->get_selection().clear();
    } else {
      set_current(NodeId());
    }
    update_scope();
    return;
  }
  if(m_editor) {
    if(!m_scope.is_nil()) {
      m_editor->get_selection().select(m_scope);
    }
    return;
  }
  set_current(m_scope);
}
