#ifndef RELLU_UI_EXPORT_DIALOG_HPP
#define RELLU_UI_EXPORT_DIALOG_HPP
#include <functional>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFrame>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QString>
#include <QVBoxLayout>
#include "Rellu/Io/ExportFormat.hpp"

namespace Rellu {

  /**
   * Adobe XD's Export Assets dialog, shown modally over a dimmed window:
   * the format, its options, the destination folder, and the buttons that
   * export or cancel, replaced by a progress bar while exporting.
   */
  class ExportDialog : public QDialog {
    public:

      /**
       * Chooses the folder exported to.
       * @param folder The folder chosen before, or empty for none.
       * @return The folder chosen, or empty if the choice was cancelled.
       */
      using FolderChooser = std::function<QString (const QString& folder)>;

      /** Called when the Export button is pressed with a folder chosen. */
      using ExportHandler = std::function<void ()>;

      /**
       * Constructs an ExportDialog.
       * @param options The options shown.
       * @param folder The folder exported to, or empty if none is chosen.
       * @param is_all_artboards Whether All Artboards are exported, labelling
       *        the Export button Export All Artboards.
       * @param asset_count The number of assets exported.
       * @param parent The widget whose window the dialog covers.
       */
      ExportDialog(const ExportOptions& options, const QString& folder,
        bool is_all_artboards, int asset_count, QWidget* parent);

      /**
       * Constructs an ExportDialog whose Export size field holds a size
       * typed in.
       * @param options The options shown.
       * @param scale_text The size typed in, or empty for one of the
       *        field's choices.
       * @param folder The folder exported to, or empty if none is chosen.
       * @param is_all_artboards Whether All Artboards are exported.
       * @param asset_count The number of assets exported.
       * @param parent The widget whose window the dialog covers.
       */
      ExportDialog(const ExportOptions& options, const QString& scale_text,
        const QString& folder, bool is_all_artboards, int asset_count,
        QWidget* parent);

      /** Returns the options shown. */
      const ExportOptions& get_options() const;

      /**
       * Sets the options shown.
       * @param options The options.
       */
      void set_options(const ExportOptions& options);

      /** Returns the folder exported to, empty if none is chosen. */
      const QString& get_folder() const;

      /**
       * Sets the folder exported to.
       * @param folder The folder, or empty for none.
       */
      void set_folder(const QString& folder);

      /**
       * Returns the text of the Export size field when it holds a size
       * typed in, such as 1/3x, or empty when it holds one of its choices.
       */
      const QString& get_scale_text() const;

      /**
       * Types a size into the Export size field and commits it, as Enter
       * does, appending x if it lacks one; a text that is not a positive
       * size is discarded.
       * @param text The text typed.
       * @return <code>true</code> iff the size was accepted.
       */
      bool set_scale_text(const QString& text);

      /** Returns the line of text explaining the sizes exported, if any. */
      QString get_hint() const;

      /** Returns the label of the Export button. */
      QString get_export_label() const;

      /**
       * Sets the function choosing the folder exported to, by default a
       * native folder picker.
       * @param chooser The function.
       */
      void set_folder_chooser(FolderChooser chooser);

      /**
       * Sets the function called when the Export button is pressed.
       * @param handler The function.
       */
      void set_export_handler(ExportHandler handler);

      /**
       * Chooses the folder exported to, as Choose Destination or Change
       * does.
       * @return <code>true</code> iff a folder was chosen.
       */
      bool choose_folder();

      /**
       * Presses the Export button, choosing a folder first if none is.
       */
      void submit();

      /** Returns <code>true</code> iff an export is in progress. */
      bool is_exporting() const;

      /**
       * Shows or hides the progress of an export, disabling the options
       * while it is shown.
       * @param is_exporting Whether an export is in progress.
       */
      void set_exporting(bool is_exporting);

      /**
       * Shows an export's progress.
       * @param done The number of files settled.
       * @param total The number of files.
       */
      void set_progress(int done, int total);

      /**
       * Returns <code>true</code> iff the export in progress was cancelled.
       */
      bool is_cancel_requested() const;

      /** Cancels the export in progress, as its Cancel button does. */
      void request_cancel();

    protected:
      void paintEvent(QPaintEvent* event) override;
      void showEvent(QShowEvent* event) override;
      void keyPressEvent(QKeyEvent* event) override;
      bool eventFilter(QObject* watched, QEvent* event) override;

    private:
      ExportOptions m_options;
      QString m_folder;
      QString m_scale_text;
      bool m_is_all_artboards;
      int m_asset_count;
      bool m_is_exporting;
      bool m_is_cancel_requested;
      bool m_is_updating;
      FolderChooser m_folder_chooser;
      ExportHandler m_export_handler;
      QFrame* m_panel;
      QVBoxLayout* m_body;
      QWidget* m_format_row;
      QComboBox* m_format;
      QWidget* m_quality_row;
      QComboBox* m_quality;
      QWidget* m_size_row;
      QComboBox* m_size;
      QLabel* m_hint;
      QWidget* m_styling_row;
      QComboBox* m_styling;
      QWidget* m_images_row;
      QRadioButton* m_embed;
      QRadioButton* m_link;
      QWidget* m_minify_row;
      QCheckBox* m_minify;
      QWidget* m_outline_row;
      QCheckBox* m_outline;
      QWidget* m_pdf_row;
      QRadioButton* m_single;
      QRadioButton* m_multiple;
      QWidget* m_folder_row;
      QLabel* m_path;
      QPushButton* m_destination;
      QWidget* m_footer;
      QWidget* m_buttons;
      QPushButton* m_export;
      QPushButton* m_cancel;
      QWidget* m_progress_row;
      QProgressBar* m_progress;
      QPushButton* m_progress_cancel;

      ExportDialog(const ExportDialog&) = delete;
      ExportDialog& operator =(const ExportDialog&) = delete;
      void read_controls();
      void update_controls();
      void rebuild();
  };
}

#endif
