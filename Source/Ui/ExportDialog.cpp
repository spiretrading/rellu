#include <algorithm>
#include <array>
#include <utility>
#include <vector>
#include <QDir>
#include <QFileDialog>
#include <QFontMetrics>
#include <QGridLayout>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QStyledItemDelegate>
#include <QStylePainter>
#include <QTimer>
#include "Rellu/Ui/AssetExport.hpp"
#include "Rellu/Ui/ExportDialog.hpp"
#include "Rellu/Ui/Translation.hpp"
#include "Icons.hpp"

using namespace Rellu;

namespace {
  constexpr auto PANEL_WIDTH = 464;
  constexpr auto MARGIN = 32;
  constexpr auto LABEL_WIDTH = 70;
  constexpr auto LABEL_GAP = 32;
  constexpr auto ROW_HEIGHT = 32;
  constexpr auto ROW_SPACING = 16;
  constexpr auto HINT_LINE_HEIGHT = 13;
  constexpr auto UNDERLINED_WIDTH = 78;
  constexpr auto CHEVRON_GAP = 23;
  constexpr auto CHEVRON_SIZE = 12;
  constexpr auto CHEVRON_WIDTH = 6.0;
  constexpr auto CHEVRON_HEIGHT = 3.3;
  constexpr auto POPUP_PADDING = 46;
  constexpr auto RADIO_SIZE = 16;
  constexpr auto RADIO_GAP = 12;
  constexpr auto DESTINATION_OFFSET = 22;
  constexpr auto DIALOG_RISE = 36;
  constexpr auto ITEM_HEIGHT = 41;
  constexpr auto PATH_WIDTH = 187;
  constexpr auto HINT_WIDTH = 298;
  constexpr auto SCALES = std::to_array({0.5, 1.0, 1.5, 2.0, 3.0, 4.0});
  constexpr auto QUALITIES = std::to_array({20, 40, 60, 80, 100});
  const auto BACKDROP = QColor(0, 0, 0, 140);
  const auto TEXT = QColor(0x3F, 0x3F, 0x3F);
  const auto DISABLED = QColor(0xB8, 0xB8, 0xB8);
  const auto CHEVRON = QColor(0x4B, 0x4B, 0x4B);
  const auto UNDERLINE = QColor(0xE4, 0xE4, 0xE4);
  const auto ACCENT = QColor(0x14, 0x92, 0xE6);
  const auto HOVER = QColor(0xF4, 0xF4, 0xF4);
  const auto RADIO = QColor(0x66, 0x66, 0x66);
  const auto HINT = QColor(0x8E, 0x8E, 0x8E);
  constexpr auto HINT_FONT_SIZE = 11;
  constexpr auto HINT_TOP = 4;
  constexpr auto HINT_BOTTOM = 3;
  const auto STYLE = QString(R"(
QFrame#export_panel {
  background: #F5F5F5;
  border-radius: 4px;
}
QLabel#export_title {
  color: #3F3F3F;
  font-size: 14px;
  font-weight: bold;
}
QFrame#export_rule {
  background: #E4E4E4;
  border: none;
}
QLabel#export_label {
  color: #8E8E8E;
  font-size: 11px;
}
QLabel#export_label:disabled {
  color: #C8C8C8;
}
QLabel#export_path, QLabel#export_footer {
  color: #3F3F3F;
  font-size: 12px;
}
QComboBox#export_choice {
  color: #3F3F3F;
  font-size: 12px;
}
QComboBox#export_choice::drop-down {
  border: none;
  width: 14px;
}
QListView#export_choices {
  background: #FFFFFF;
  border: 1px solid #DADADA;
  color: #3F3F3F;
  font-size: 12px;
  outline: 0;
}
QRadioButton#export_option, QCheckBox#export_option {
  color: #3F3F3F;
  font-size: 12px;
  spacing: 12px;
}
QRadioButton#export_option:disabled, QCheckBox#export_option:disabled {
  color: #B8B8B8;
}
QPushButton#export_destination {
  background: transparent;
  border: none;
  color: #4B4B4B;
  font-size: 12px;
  font-weight: bold;
  padding: 0px;
}
QPushButton#export_destination:disabled {
  color: #B8B8B8;
}
QPushButton#export_primary, QPushButton#export_secondary {
  background: transparent;
  border-radius: 16px;
  font-size: 12px;
  font-weight: bold;
  min-height: 28px;
  max-height: 28px;
  padding: 0px 15px;
}
QPushButton#export_primary {
  border: 2px solid #4B4B4B;
  color: #4B4B4B;
}
QPushButton#export_secondary {
  border: 2px solid #999999;
  color: #707070;
}
QPushButton#export_primary:hover {
  background: #4B4B4B;
  color: #FFFFFF;
}
QPushButton#export_primary:focus {
  background: #1473E6;
  border-color: #1473E6;
  color: #FFFFFF;
}
QPushButton#export_primary:disabled, QPushButton#export_secondary:disabled {
  border-color: #D0D0D0;
  color: #C0C0C0;
}
QLabel#export_progress_label {
  color: #8E8E8E;
  font-size: 11px;
}
QProgressBar#export_progress {
  background: #E4E4E4;
  border: none;
  max-height: 4px;
  min-height: 4px;
}
QProgressBar#export_progress::chunk {
  background: #1492E6;
}
)");

  QString to_scale_text(double scale) {
    return QString::number(scale) + translate("export", "x");
  }

  QString get_preset_text(ExportPreset preset) {
    if(preset == ExportPreset::WEB) {
      return translate("export", "Web");
    } else if(preset == ExportPreset::IOS) {
      return translate("export", "iOS");
    }
    return translate("export", "Android");
  }

  QStringList get_preset_sizes(ExportPreset preset) {
    if(preset == ExportPreset::WEB) {
      return {to_scale_text(1), to_scale_text(2)};
    } else if(preset == ExportPreset::IOS) {
      return {to_scale_text(1), to_scale_text(2), to_scale_text(3)};
    }
    return {"ldpi", "mdpi", "hdpi", "xhdpi", "xxhdpi", "xxxhdpi"};
  }

  QString to_link_html(const QString& text) {
    auto html = QString();
    auto position = qsizetype(0);
    while(position < text.size()) {
      auto start = text.indexOf('[', position);
      auto end = start < 0 ? -1 : text.indexOf(']', start);
      if(end < 0) {
        html += text.mid(position).toHtmlEscaped();
        break;
      }
      html += text.mid(position, start - position).toHtmlEscaped();
      auto label = text.mid(start + 1, end - start - 1).section('|', 0, 0);
      html += "<span style=\"color: #1492E6; text-decoration: underline;\">" +
        label.toHtmlEscaped() + "</span>";
      position = end + 1;
    }
    return html;
  }

  class ChoiceDelegate : public QStyledItemDelegate {
    public:
      explicit ChoiceDelegate(QComboBox* box)
        : QStyledItemDelegate(box),
          m_box(box) {}

      QSize sizeHint(const QStyleOptionViewItem& option,
          const QModelIndex& index) const override {
        auto metrics = QFontMetrics(option.font);
        return QSize(metrics.horizontalAdvance(index.data().toString()) +
          POPUP_PADDING, ITEM_HEIGHT);
      }

      void paint(QPainter* painter, const QStyleOptionViewItem& option,
          const QModelIndex& index) const override {
        painter->save();
        auto is_current = index.row() == m_box->currentIndex();
        if(option.state & QStyle::State_MouseOver ||
            option.state & QStyle::State_Selected) {
          painter->fillRect(option.rect, HOVER);
        }
        if(is_current) {
          painter->fillRect(QRect(option.rect.left(), option.rect.top() + 2,
            2, option.rect.height() - 4), ACCENT);
        }
        painter->setPen(is_current ? ACCENT : TEXT);
        painter->setFont(option.font);
        painter->drawText(option.rect.adjusted(13, 0, -32, 0),
          Qt::AlignVCenter | Qt::AlignLeft, index.data().toString());
        if(is_current) {
          auto check = QPainterPath();
          auto x = option.rect.right() - 22;
          auto y = option.rect.center().y();
          check.moveTo(x, y);
          check.lineTo(x + 3.5, y + 3.5);
          check.lineTo(x + 10, y - 3.5);
          painter->setRenderHint(QPainter::Antialiasing);
          painter->setPen(QPen(ACCENT, 1.6));
          painter->setBrush(Qt::NoBrush);
          painter->drawPath(check);
        }
        painter->restore();
      }

    private:
      QComboBox* m_box;
  };

  class ChoiceBox : public QComboBox {
    public:
      explicit ChoiceBox(bool is_underlined)
          : m_is_underlined(is_underlined) {
        setObjectName("export_choice");
        setCursor(Qt::PointingHandCursor);
        auto list = new QListView();
        list->setObjectName("export_choices");
        setView(list);
        setItemDelegate(new ChoiceDelegate(this));
        setFixedHeight(ROW_HEIGHT);
        connect(this, &QComboBox::currentIndexChanged, this, [this] {
          updateGeometry();
          update();
        });
      }

      QSize sizeHint() const override {
        if(m_is_underlined) {
          return QSize(UNDERLINED_WIDTH, ROW_HEIGHT);
        }
        return QSize(fontMetrics().horizontalAdvance(currentText()) +
          CHEVRON_GAP + CHEVRON_SIZE, ROW_HEIGHT);
      }

      QSize minimumSizeHint() const override {
        return sizeHint();
      }

      void showPopup() override {
        auto metrics = view()->fontMetrics();
        auto width = 0;
        for(auto i = 0; i != count(); ++i) {
          width = std::max(width, metrics.horizontalAdvance(itemText(i)));
        }
        setMaxVisibleItems(count());
        view()->setMinimumWidth(width + POPUP_PADDING);
        QComboBox::showPopup();
        auto popup = view()->window();
        if(!popup || popup == window()) {
          return;
        }
        auto current = std::max(currentIndex(), 0);
        auto position = mapToGlobal(QPoint(-1,
          height() / 2 - ITEM_HEIGHT / 2 - current * ITEM_HEIGHT - 1));
        if(auto screen = this->screen()) {
          auto area = screen->availableGeometry();
          position.setY(std::clamp(position.y(), area.top(),
            std::max(area.top(), area.bottom() - popup->height())));
          position.setX(std::clamp(position.x(), area.left(),
            std::max(area.left(), area.right() - popup->width())));
        }
        popup->move(position);
      }

    protected:
      void paintEvent(QPaintEvent*) override {
        auto painter = QPainter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if(!isEditable()) {
          painter.setPen(isEnabled() ? TEXT : DISABLED);
          painter.setFont(font());
          painter.drawText(rect().adjusted(0, 0, -CHEVRON_SIZE, 0),
            Qt::AlignVCenter | Qt::AlignLeft, currentText());
        }
        auto x = width() - CHEVRON_WIDTH - 1;
        if(!m_is_underlined) {
          x = fontMetrics().horizontalAdvance(currentText()) + CHEVRON_GAP;
        }
        auto y = height() / 2.0;
        auto chevron = QPainterPath();
        chevron.moveTo(x, y - CHEVRON_HEIGHT / 2);
        chevron.lineTo(x + CHEVRON_WIDTH / 2, y + CHEVRON_HEIGHT / 2);
        chevron.lineTo(x + CHEVRON_WIDTH, y - CHEVRON_HEIGHT / 2);
        painter.setPen(QPen(isEnabled() ? CHEVRON : DISABLED, 1.1));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(chevron);
        if(m_is_underlined) {
          painter.fillRect(QRectF(0, height() - 1, width(), 1), UNDERLINE);
        }
      }

    private:
      bool m_is_underlined;
  };

  class OptionRadio : public QRadioButton {
    public:
      explicit OptionRadio(const QString& text)
        : QRadioButton(text) {}

      QSize sizeHint() const override {
        return QSize(RADIO_SIZE + RADIO_GAP +
          fontMetrics().horizontalAdvance(text()) + 2, ROW_HEIGHT);
      }

      QSize minimumSizeHint() const override {
        return sizeHint();
      }

    protected:
      bool hitButton(const QPoint& position) const override {
        return rect().contains(position);
      }

      void paintEvent(QPaintEvent*) override {
        auto painter = QPainter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        auto color = isEnabled() ? RADIO : DISABLED;
        auto circle =
          QRectF(1, (height() - RADIO_SIZE) / 2.0 + 1, RADIO_SIZE - 2,
            RADIO_SIZE - 2);
        if(isChecked()) {
          painter.setPen(Qt::NoPen);
          painter.setBrush(color);
          painter.drawEllipse(circle.adjusted(-1, -1, 1, 1));
          painter.setBrush(QColor(0xFF, 0xFF, 0xFF));
          painter.drawEllipse(circle.center(), 2.4, 2.4);
        } else {
          painter.setPen(QPen(color, 2));
          painter.setBrush(Qt::NoBrush);
          painter.drawEllipse(circle);
        }
        painter.setPen(isEnabled() ? TEXT : DISABLED);
        painter.setFont(font());
        painter.drawText(rect().adjusted(RADIO_SIZE + RADIO_GAP, 0, 0, 0),
          Qt::AlignLeft | Qt::AlignVCenter, text());
      }
  };

  class HintLabel : public QWidget {
    public:
      void set_text(const QString& text) {
        m_lines.clear();
        auto metrics = fontMetrics();
        auto line = QString();
        for(auto& word : text.split(' ', Qt::SkipEmptyParts)) {
          auto candidate = line.isEmpty() ? word : line + ' ' + word;
          if(!line.isEmpty() &&
              metrics.horizontalAdvance(candidate) > HINT_WIDTH) {
            m_lines.push_back(line);
            line = word;
          } else {
            line = candidate;
          }
        }
        if(!line.isEmpty()) {
          m_lines.push_back(line);
        }
        setFixedSize(HINT_WIDTH, m_lines.empty() ? 0 : HINT_TOP +
          HINT_LINE_HEIGHT * static_cast<int>(m_lines.size()) + HINT_BOTTOM);
        update();
      }

      int get_line_count() const {
        return static_cast<int>(m_lines.size());
      }

    protected:
      void paintEvent(QPaintEvent*) override {
        auto painter = QPainter(this);
        painter.setPen(HINT);
        painter.setFont(font());
        for(auto i = 0; i != static_cast<int>(m_lines.size()); ++i) {
          painter.drawText(QRect(0, HINT_TOP + i * HINT_LINE_HEIGHT,
            width(), HINT_LINE_HEIGHT), Qt::AlignLeft | Qt::AlignVCenter,
            m_lines[i]);
        }
      }

    private:
      std::vector<QString> m_lines;
  };

  class FolderIcon : public QWidget {
    public:
      FolderIcon() {
        setFixedSize(16, 14);
      }

    protected:
      void paintEvent(QPaintEvent*) override {
        auto painter = QPainter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        auto folder = QPainterPath();
        folder.moveTo(1, 2.5);
        folder.lineTo(6, 2.5);
        folder.lineTo(7.5, 4);
        folder.lineTo(15, 4);
        folder.lineTo(15, 12.5);
        folder.lineTo(1, 12.5);
        folder.closeSubpath();
        painter.setPen(QPen(isEnabled() ? QColor(0x66, 0x66, 0x66) : DISABLED,
          1));
        painter.setBrush(QColor(0xD8, 0xD8, 0xD8));
        painter.drawPath(folder);
      }
  };

  QLabel* make_label(const QString& text) {
    auto label = new QLabel(text);
    label->setObjectName("export_label");
    label->setFixedWidth(LABEL_WIDTH + LABEL_GAP);
    label->setFixedHeight(ROW_HEIGHT);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    return label;
  }

  QWidget* make_row(const QString& label, QWidget* control,
      QWidget* second = nullptr) {
    auto row = new QWidget();
    auto layout = new QGridLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(0);
    layout->setVerticalSpacing(0);
    layout->addWidget(make_label(label), 0, 0, Qt::AlignTop);
    layout->addWidget(control, 0, 1, Qt::AlignLeft | Qt::AlignVCenter);
    if(second) {
      layout->addWidget(second, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);
    }
    layout->setColumnStretch(1, 1);
    return row;
  }

  QWidget* wrap_option(QWidget* option) {
    option->setObjectName("export_option");
    option->setFixedHeight(ROW_HEIGHT);
    option->setCursor(Qt::PointingHandCursor);
    return option;
  }

  QString elide_start(const QString& text, const QFontMetrics& metrics,
      int width) {
    if(metrics.horizontalAdvance(text) <= width) {
      return text;
    }
    auto tail = text;
    while(!tail.isEmpty() &&
        metrics.horizontalAdvance("..." + tail) > width) {
      tail.remove(0, 1);
    }
    return "..." + tail;
  }
}

ExportDialog::ExportDialog(const ExportOptions& options,
  const QString& folder, bool is_all_artboards, int asset_count,
  QWidget* parent)
  : ExportDialog(
      options, QString(), folder, is_all_artboards, asset_count, parent) {}

ExportDialog::ExportDialog(const ExportOptions& options,
    const QString& scale_text, const QString& folder, bool is_all_artboards,
    int asset_count, QWidget* parent)
    : QDialog(parent, Qt::FramelessWindowHint | Qt::Dialog),
      m_options(options),
      m_folder(folder),
      m_scale_text(scale_text),
      m_is_all_artboards(is_all_artboards),
      m_asset_count(asset_count),
      m_is_exporting(false),
      m_is_cancel_requested(false),
      m_is_updating(false),
      m_is_scale_edited(false) {
  setObjectName("export_dialog");
  setAttribute(Qt::WA_TranslucentBackground);
  setModal(true);
  setStyleSheet(STYLE);
  m_folder_chooser = [this] (const QString& current) {
    return QFileDialog::getExistingDirectory(this, QString(), current);
  };
  auto outer = new QVBoxLayout(this);
  outer->setAlignment(Qt::AlignCenter);
  outer->setContentsMargins(0, 0, 0, DIALOG_RISE);
  m_panel = new QFrame();
  m_panel->setObjectName("export_panel");
  m_panel->setFixedWidth(PANEL_WIDTH);
  m_panel->setFocusPolicy(Qt::StrongFocus);
  auto shadow = new QGraphicsDropShadowEffect(m_panel);
  shadow->setBlurRadius(20);
  shadow->setOffset(0, 2);
  shadow->setColor(QColor(0, 0, 0, 60));
  m_panel->setGraphicsEffect(shadow);
  outer->addWidget(m_panel);
  auto panel = new QVBoxLayout(m_panel);
  panel->setContentsMargins(MARGIN, 20, MARGIN, MARGIN);
  panel->setSpacing(0);
  auto title = new QLabel(translate("export", "Export Assets"));
  title->setObjectName("export_title");
  title->setFixedHeight(24);
  panel->addWidget(title);
  panel->addSpacing(2);
  auto rule = new QFrame();
  rule->setObjectName("export_rule");
  rule->setFixedHeight(1);
  panel->addWidget(rule);
  panel->addSpacing(33);
  m_body = new QVBoxLayout();
  m_body->setContentsMargins(0, 0, 0, 0);
  m_body->setSpacing(0);
  panel->addLayout(m_body);
  m_format = new ChoiceBox(false);
  m_format->addItems({translate("export", "PNG"), translate("export", "SVG"),
    translate("export", "PDF"), translate("export", "JPG")});
  m_format_row = make_row(translate("export", "Format:"), m_format);
  m_quality = new ChoiceBox(true);
  for(auto quality : QUALITIES) {
    m_quality->addItem(QString::number(quality) + '%');
  }
  m_quality_row = make_row(translate("export", "Quality:"), m_quality);
  m_size = new ChoiceBox(true);
  m_size->setEditable(true);
  m_size->setInsertPolicy(QComboBox::NoInsert);
  m_size->setCompleter(nullptr);
  m_size->lineEdit()->setObjectName("export_size_field");
  m_size->lineEdit()->installEventFilter(this);
  m_size->lineEdit()->setAutoFillBackground(false);
  auto field_palette = m_size->lineEdit()->palette();
  field_palette.setColor(QPalette::Base, Qt::transparent);
  field_palette.setColor(QPalette::Window, Qt::transparent);
  m_size->lineEdit()->setPalette(field_palette);
  m_size->lineEdit()->setFrame(false);
  m_size->lineEdit()->setStyleSheet(
    "QLineEdit { background: transparent; border: none; color: #3F3F3F; "
    "font-size: 12px; padding: 0px; selection-background-color: #2680EB; }"
    "QLineEdit:disabled { color: #B8B8B8; }");
  connect(m_size->lineEdit(), &QLineEdit::textEdited, this, [this] {
    m_is_scale_edited = true;
  });
  for(auto scale : SCALES) {
    m_size->addItem(to_scale_text(scale));
  }
  for(auto preset : {ExportPreset::WEB, ExportPreset::IOS,
      ExportPreset::ANDROID}) {
    m_size->addItem(get_preset_text(preset));
  }
  auto hint = new HintLabel();
  auto hint_font = hint->font();
  hint_font.setPixelSize(HINT_FONT_SIZE);
  hint->setFont(hint_font);
  m_hint = hint;
  m_size_row = make_row(translate("export", "Export size:"), m_size, m_hint);
  m_styling = new ChoiceBox(false);
  m_styling->addItems({translate("export", "Presentation Attributes"),
    translate("export", "Internal CSS")});
  m_styling_row = make_row(translate("export", "Styling:"), m_styling);
  m_embed = new OptionRadio(translate("export", "Embed"));
  m_link = new OptionRadio(translate("export", "Link"));
  wrap_option(m_embed);
  wrap_option(m_link);
  m_images_row =
    make_row(translate("export", "Save images:"), m_embed, m_link);
  m_minify = new QCheckBox(translate("export", "Optimized (Minified)"));
  wrap_option(m_minify);
  m_minify_row = make_row(translate("export", "File size:"), m_minify);
  m_outline = new QCheckBox(translate("export", "Outline Stroke"));
  wrap_option(m_outline);
  m_outline_row = make_row(translate("export", "Path Options:"), m_outline);
  m_single = new OptionRadio(translate("export", "Single PDF file"));
  m_multiple = new OptionRadio(translate("export", "Multiple PDF files"));
  wrap_option(m_single);
  wrap_option(m_multiple);
  m_pdf_row =
    make_row(translate("export", "Save assets as:"), m_single, m_multiple);
  auto destination = new QWidget();
  auto destination_layout = new QHBoxLayout(destination);
  destination_layout->setContentsMargins(0, 0, 0, 0);
  destination_layout->setSpacing(0);
  destination_layout->addWidget(new FolderIcon());
  destination_layout->addSpacing(10);
  m_path = new QLabel();
  m_path->setObjectName("export_path");
  destination_layout->addWidget(m_path);
  m_destination = new QPushButton();
  m_destination->setObjectName("export_destination");
  m_destination->setCursor(Qt::PointingHandCursor);
  m_destination->setAutoDefault(false);
  destination_layout->addWidget(m_destination);
  destination_layout->addStretch(1);
  destination->setFixedHeight(ROW_HEIGHT);
  m_folder_row = make_row(translate("export", "Export to:"), destination);
  m_footer = new QWidget();
  auto footer = new QVBoxLayout(m_footer);
  footer->setContentsMargins(0, 0, 0, 0);
  footer->setSpacing(0);
  auto footer_rule = new QFrame();
  footer_rule->setObjectName("export_rule");
  footer_rule->setFixedHeight(1);
  footer->addWidget(footer_rule);
  footer->addSpacing(16);
  auto notice_label = new QLabel(to_link_html(translate("export", "Colors of "
    "the exported objects may look slightly different when viewed on "
    "multiple surfaces. [Learn more about Color Management]")));
  notice_label->setObjectName("export_footer");
  notice_label->setTextFormat(Qt::RichText);
  notice_label->setWordWrap(true);
  footer->addWidget(notice_label);
  m_buttons = new QWidget();
  auto buttons = new QHBoxLayout(m_buttons);
  buttons->setContentsMargins(0, 0, 0, 0);
  buttons->setSpacing(17);
  buttons->addStretch(1);
  m_export = new QPushButton(get_export_label());
  m_export->setObjectName("export_primary");
  m_export->setCursor(Qt::PointingHandCursor);
  m_export->setAutoDefault(false);
  buttons->addWidget(m_export);
  m_cancel = new QPushButton(translate("export", "Cancel"));
  m_cancel->setObjectName("export_secondary");
  m_cancel->setCursor(Qt::PointingHandCursor);
  m_cancel->setAutoDefault(false);
  buttons->addWidget(m_cancel);
  m_progress_row = new QWidget();
  auto progress = new QHBoxLayout(m_progress_row);
  progress->setContentsMargins(0, 0, 0, 0);
  progress->setSpacing(17);
  auto bar = new QVBoxLayout();
  bar->setSpacing(6);
  auto progress_label = new QLabel(translate("export", "Exporting assets"));
  progress_label->setObjectName("export_progress_label");
  bar->addWidget(progress_label);
  m_progress = new QProgressBar();
  m_progress->setObjectName("export_progress");
  m_progress->setTextVisible(false);
  bar->addWidget(m_progress);
  progress->addLayout(bar, 1);
  m_progress_cancel = new QPushButton(translate("export", "Cancel"));
  m_progress_cancel->setObjectName("export_secondary");
  m_progress_cancel->setCursor(Qt::PointingHandCursor);
  m_progress_cancel->setAutoDefault(false);
  progress->addWidget(m_progress_cancel);
  panel->addWidget(m_footer);
  panel->addWidget(m_buttons);
  panel->addWidget(m_progress_row);
  m_progress_row->hide();
  for(auto box : {m_format, m_quality, m_size, m_styling}) {
    connect(box, &QComboBox::currentIndexChanged, this, [this] {
      read_controls();
    });
  }
  for(auto button : std::to_array<QAbstractButton*>({m_embed, m_link,
      m_minify, m_outline, m_single, m_multiple})) {
    connect(button, &QAbstractButton::toggled, this, [this] {
      read_controls();
    });
  }
  connect(m_destination, &QPushButton::clicked, this, [this] {
    choose_folder();
  });
  connect(m_export, &QPushButton::clicked, this, [this] {
    submit();
  });
  connect(m_cancel, &QPushButton::clicked, this, [this] {
    reject();
  });
  connect(m_progress_cancel, &QPushButton::clicked, this, [this] {
    request_cancel();
  });
  update_controls();
}

const ExportOptions& ExportDialog::get_options() const {
  return m_options;
}

void ExportDialog::set_options(const ExportOptions& options) {
  m_options = options;
  update_controls();
}

const QString& ExportDialog::get_scale_text() const {
  return m_scale_text;
}

bool ExportDialog::set_scale_text(const QString& text) {
  m_is_scale_edited = false;
  auto scale = parse_export_scale(text);
  if(!scale) {
    update_controls();
    return false;
  }
  auto normalized = text.trimmed();
  if(!normalized.endsWith('x', Qt::CaseInsensitive)) {
    normalized += translate("export", "x");
  }
  m_options.m_scale = *scale;
  m_options.m_preset = ExportPreset::NONE;
  m_scale_text = normalized;
  for(auto i = 0; i != static_cast<int>(SCALES.size()); ++i) {
    if(m_size->itemText(i) == normalized) {
      m_scale_text.clear();
    }
  }
  update_controls();
  return true;
}

const QString& ExportDialog::get_folder() const {
  return m_folder;
}

void ExportDialog::set_folder(const QString& folder) {
  m_folder = folder;
  update_controls();
}

QString ExportDialog::get_hint() const {
  if(is_vector(m_options.m_format) ||
      m_options.m_preset == ExportPreset::NONE) {
    return QString();
  }
  auto sizes = get_preset_sizes(m_options.m_preset);
  auto text = translate("export", "Selected assets will export at ");
  for(auto i = 0; i != sizes.size(); ++i) {
    if(i == sizes.size() - 1 && i != 0) {
      text += translate("export", " and ");
    } else if(i != 0) {
      text += translate("export", ", ");
    }
    text += sizes[i];
  }
  return text;
}

QString ExportDialog::get_export_label() const {
  if(m_is_all_artboards) {
    return translate("export", "Export All Artboards");
  }
  return translate("export", "Export");
}

void ExportDialog::set_folder_chooser(FolderChooser chooser) {
  m_folder_chooser = std::move(chooser);
}

void ExportDialog::set_export_handler(ExportHandler handler) {
  m_export_handler = std::move(handler);
}

bool ExportDialog::choose_folder() {
  if(!m_folder_chooser) {
    return false;
  }
  auto folder = m_folder_chooser(m_folder);
  if(folder.isEmpty()) {
    return false;
  }
  set_folder(folder);
  return true;
}

void ExportDialog::submit() {
  if(m_is_exporting) {
    return;
  }
  if(m_folder.isEmpty() && !choose_folder()) {
    return;
  }
  if(m_export_handler) {
    m_export_handler();
  } else {
    accept();
  }
}

bool ExportDialog::is_exporting() const {
  return m_is_exporting;
}

void ExportDialog::set_exporting(bool is_exporting) {
  m_is_exporting = is_exporting;
  m_is_cancel_requested = false;
  m_progress->setRange(0, 1);
  m_progress->setValue(0);
  update_controls();
}

void ExportDialog::set_progress(int done, int total) {
  m_progress->setRange(0, std::max(total, 1));
  m_progress->setValue(done);
}

bool ExportDialog::is_cancel_requested() const {
  return m_is_cancel_requested;
}

void ExportDialog::request_cancel() {
  if(m_is_exporting) {
    m_is_cancel_requested = true;
  }
}

void ExportDialog::paintEvent(QPaintEvent* event) {
  auto painter = QPainter(this);
  painter.fillRect(rect(), BACKDROP);
}

void ExportDialog::showEvent(QShowEvent* event) {
  if(auto parent = parentWidget()) {
    auto window = parent->window();
    setGeometry(QRect(window->mapToGlobal(QPoint(0, 0)), window->size()));
  }
  QDialog::showEvent(event);
  m_panel->setFocus();
}

void ExportDialog::keyPressEvent(QKeyEvent* event) {
  if(event->key() == Qt::Key_Escape) {
    if(m_is_exporting) {
      request_cancel();
    } else {
      reject();
    }
    return;
  }
  if(event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    if(auto button = qobject_cast<QPushButton*>(focusWidget())) {
      button->click();
    }
    return;
  }
  QDialog::keyPressEvent(event);
}

bool ExportDialog::eventFilter(QObject* watched, QEvent* event) {
  if(watched == m_size->lineEdit()) {
    if(event->type() == QEvent::FocusIn) {
      QTimer::singleShot(0, m_size->lineEdit(), &QLineEdit::selectAll);
    } else if(event->type() == QEvent::KeyPress) {
      auto key = static_cast<QKeyEvent*>(event)->key();
      if(key == Qt::Key_Return || key == Qt::Key_Enter) {
        set_scale_text(m_size->lineEdit()->text());
        m_export->setFocus(Qt::TabFocusReason);
        return true;
      }
    } else if(event->type() == QEvent::FocusOut && m_is_scale_edited) {
      set_scale_text(m_size->lineEdit()->text());
    }
  }
  return QDialog::eventFilter(watched, event);
}

void ExportDialog::read_controls() {
  if(m_is_updating) {
    return;
  }
  m_options.m_format = static_cast<ExportFormat>(m_format->currentIndex());
  m_options.m_jpg_quality = QUALITIES[std::max(0, m_quality->currentIndex())];
  auto size = m_size->currentIndex();
  if(size >= 0) {
    m_scale_text.clear();
  }
  if(size >= 0 && size < static_cast<int>(SCALES.size())) {
    m_options.m_scale = SCALES[size];
    m_options.m_preset = ExportPreset::NONE;
  } else if(size >= 0) {
    m_options.m_preset =
      static_cast<ExportPreset>(size - static_cast<int>(SCALES.size()) + 1);
  }
  m_options.m_svg_styling = m_styling->currentIndex() == 1 ?
    SvgStyling::INTERNAL_CSS : SvgStyling::PRESENTATION_ATTRIBUTES;
  m_options.m_svg_images =
    m_link->isChecked() ? SvgImages::LINK : SvgImages::EMBED;
  m_options.m_is_svg_minified = m_minify->isChecked();
  m_options.m_is_svg_outlined = m_outline->isChecked();
  if(m_asset_count > 1) {
    m_options.m_is_pdf_single = !m_multiple->isChecked();
  }
  update_controls();
}

void ExportDialog::update_controls() {
  m_is_updating = true;
  m_format->setCurrentIndex(static_cast<int>(m_options.m_format));
  auto quality = 0;
  for(auto i = std::size_t(0); i != QUALITIES.size(); ++i) {
    if(QUALITIES[i] <= m_options.m_jpg_quality) {
      quality = static_cast<int>(i);
    }
  }
  m_quality->setCurrentIndex(quality);
  if(m_options.m_preset == ExportPreset::NONE && !m_scale_text.isEmpty()) {
    m_size->setCurrentIndex(-1);
    m_size->setEditText(m_scale_text);
  } else if(m_options.m_preset == ExportPreset::NONE) {
    auto size = 1;
    for(auto i = std::size_t(0); i != SCALES.size(); ++i) {
      if(SCALES[i] == m_options.m_scale) {
        size = static_cast<int>(i);
      }
    }
    m_size->setCurrentIndex(size);
  } else {
    m_size->setCurrentIndex(static_cast<int>(SCALES.size()) +
      static_cast<int>(m_options.m_preset) - 1);
  }
  m_styling->setCurrentIndex(
    m_options.m_svg_styling == SvgStyling::INTERNAL_CSS ? 1 : 0);
  m_embed->setChecked(m_options.m_svg_images == SvgImages::EMBED);
  m_link->setChecked(m_options.m_svg_images == SvgImages::LINK);
  m_minify->setChecked(m_options.m_is_svg_minified);
  m_outline->setChecked(m_options.m_is_svg_outlined);
  auto is_single = m_options.m_is_pdf_single || m_asset_count <= 1;
  m_single->setChecked(is_single);
  m_multiple->setChecked(!is_single);
  auto hint = get_hint();
  static_cast<HintLabel*>(m_hint)->set_text(hint);
  m_hint->setVisible(!hint.isEmpty());
  if(m_folder.isEmpty()) {
    m_path->clear();
    m_destination->setText(translate("export", "Choose Destination"));
  } else {
    m_path->setText(elide_start(QDir::toNativeSeparators(m_folder),
      m_path->fontMetrics(), PATH_WIDTH));

    m_destination->setText(translate("export", "Change"));
  }
  m_path->setFixedWidth(
    m_folder.isEmpty() ? DESTINATION_OFFSET : PATH_WIDTH + 24);
  for(auto widget : std::to_array<QWidget*>({m_format_row, m_quality_row,
      m_size_row, m_styling_row, m_images_row, m_minify_row, m_outline_row,
      m_folder_row})) {
    widget->setEnabled(!m_is_exporting);
  }
  m_pdf_row->setEnabled(!m_is_exporting && m_asset_count > 1);
  m_buttons->setVisible(!m_is_exporting);
  m_progress_row->setVisible(m_is_exporting);
  m_is_updating = false;
  rebuild();
}

void ExportDialog::rebuild() {
  while(auto item = m_body->takeAt(0)) {
    if(auto widget = item->widget()) {
      widget->hide();
    }
    delete item;
  }
  auto rows = std::vector<QWidget*>{m_format_row};
  if(m_options.m_format == ExportFormat::JPG) {
    rows.push_back(m_quality_row);
  }
  if(!is_vector(m_options.m_format)) {
    rows.push_back(m_size_row);
  } else if(m_options.m_format == ExportFormat::SVG) {
    rows.insert(rows.end(),
      {m_styling_row, m_images_row, m_minify_row, m_outline_row});
  } else {
    rows.push_back(m_pdf_row);
  }
  rows.push_back(m_folder_row);
  for(auto i = std::size_t(0); i != rows.size(); ++i) {
    if(i != 0 && rows[i - 1] == m_size_row) {
      m_body->addSpacing(get_hint().isEmpty() ? ROW_SPACING + 3 :
        ROW_SPACING - 4);
    } else if(i != 0 && (rows[i - 1] == m_pdf_row ||
        rows[i - 1] == m_outline_row) && rows[i] == m_folder_row) {
      m_body->addSpacing(ROW_SPACING - 2);
    } else if(i != 0) {
      m_body->addSpacing(ROW_SPACING);
    }
    m_body->addWidget(rows[i]);
    rows[i]->show();
  }
  auto has_footer = m_options.m_format != ExportFormat::SVG;
  m_footer->setVisible(has_footer);
  auto panel = static_cast<QVBoxLayout*>(m_panel->layout());
  auto footer_index = panel->indexOf(m_footer);
  if(auto spacer = panel->itemAt(footer_index - 1)->spacerItem()) {
    spacer->changeSize(0, has_footer ? 48 : 0);
  } else {
    panel->insertSpacing(footer_index, has_footer ? 48 : 0);
  }
  auto buttons_index = panel->indexOf(m_buttons);
  if(auto spacer = panel->itemAt(buttons_index - 1)->spacerItem()) {
    spacer->changeSize(0, has_footer ? 47 : 66);
  } else {
    panel->insertSpacing(buttons_index, has_footer ? 47 : 66);
  }
  panel->invalidate();
  m_panel->adjustSize();
}
