#ifndef RELLU_UI_HOME_SCREEN_HPP
#define RELLU_UI_HOME_SCREEN_HPP
#include <functional>
#include <vector>
#include <QImage>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QWidget>

class QScrollArea;

namespace Rellu {

  /** Lists the tiles of Adobe XD's Home screen offering presets. */
  enum class HomePresetType {

    /** Phones and tablets. */
    MOBILE,

    /** Web pages and desktops. */
    DESKTOP,

    /** Social media posts. */
    SOCIAL,

    /** A size typed in the Custom Size fields. */
    CUSTOM
  };

  /** Stores a size the Home screen offers for a new document. */
  struct HomePreset {

    /** The tile offering the preset. */
    HomePresetType m_type = HomePresetType::CUSTOM;

    /** The English name of the preset. */
    QString m_name;

    /** The width in pixels, 0 for a custom size left empty. */
    double m_width = 0;

    /** The height in pixels, 0 for a custom size left empty. */
    double m_height = 0;

    bool operator ==(const HomePreset&) const = default;
  };

  /**
   * Returns the presets of Adobe XD's Home screen in the order its tiles
   * list them, the Custom Size preset last with no size.
   */
  const std::vector<HomePreset>& get_home_presets();

  /**
   * Returns the presets a tile of the Home screen offers.
   * @param type The tile.
   */
  std::vector<HomePreset> get_home_presets(HomePresetType type);

  /**
   * Returns the label of a preset in its tile's list, its translated name
   * followed by its size.
   * @param preset The preset.
   */
  QString get_home_preset_label(const HomePreset& preset);

  /**
   * Returns the size of a preset as its tile describes it.
   * @param preset The preset.
   */
  QString get_home_preset_description(const HomePreset& preset);

  /**
   * Tests whether text typed in a Custom Size field is accepted, as a
   * JavaScript number is.
   * @param text The text.
   */
  bool is_home_size_text(const QString& text);

  /**
   * Returns the value of text accepted in a Custom Size field, 0 for empty
   * text.
   * @param text The text, which must be accepted.
   */
  double to_home_size(const QString& text);

  /**
   * Returns the title of a recent file on the Home screen, its file name
   * without the extension.
   * @param path The path of the file.
   */
  QString get_recent_title(const QString& path);

  /**
   * Returns the folder of a recent file on the Home screen.
   * @param path The path of the file.
   */
  QString get_recent_folder(const QString& path);

  /**
   * Reads the thumbnail an Adobe XD file keeps of its content.
   * @param path The path of the .xd file.
   * @return The thumbnail, or a null image if the file has none or cannot
   *         be read.
   */
  QImage load_xd_thumbnail(const QString& path);

  /** Stores the choices the Home screen remembers between sessions. */
  struct HomeSelections {

    /** The English name of the preset the Mobile tile shows. */
    QString m_mobile;

    /** The English name of the preset the Desktop tile shows. */
    QString m_desktop;

    /** The English name of the preset the Social tile shows. */
    QString m_social;

    /** The width last used to create a document of a custom size. */
    QString m_width;

    /** The height last used to create a document of a custom size. */
    QString m_height;

    bool operator ==(const HomeSelections&) const = default;
  };

  /**
   * Reads the Home screen's choices.
   * @param settings The settings holding them.
   */
  HomeSelections load_home_selections(QSettings& settings);

  /**
   * Writes the Home screen's choices.
   * @param settings The settings to hold them.
   * @param selections The choices.
   */
  void save_home_selections(
    QSettings& settings, const HomeSelections& selections);

  /**
   * Adobe XD's Home screen, shown when no document is open: buttons
   * creating and opening documents, a tile per kind of preset creating a
   * document of its size, and the recent files with their thumbnails.
   */
  class HomeScreen : public QWidget {
    public:

      /** The height of the white header at the top, in logical pixels. */
      static constexpr auto HEADER_HEIGHT = 88;

      /**
       * The type of function creating a document of a preset.
       * @param preset The preset.
       */
      using CreateHandler = std::function<void (const HomePreset& preset)>;

      /**
       * The type of function opening a file.
       * @param path The path of the file.
       */
      using OpenHandler = std::function<void (const QString& path)>;

      /** The type of function performing an action. */
      using ActionHandler = std::function<void ()>;

      /**
       * The type of function notified of changed choices.
       * @param selections The choices.
       */
      using SelectionsHandler =
        std::function<void (const HomeSelections& selections)>;

      /**
       * Constructs a HomeScreen listing no recent files.
       * @param parent The parent widget.
       */
      explicit HomeScreen(QWidget* parent = nullptr);

      /**
       * Sets the function creating a document of a preset.
       * @param handler The function.
       */
      void set_create_handler(CreateHandler handler);

      /**
       * Sets the function creating a document as the New file button does.
       * @param handler The function.
       */
      void set_new_handler(ActionHandler handler);

      /**
       * Sets the function opening a recent file.
       * @param handler The function.
       */
      void set_open_handler(OpenHandler handler);

      /**
       * Sets the function asking for a file to open, as the Open button
       * and Ctrl+O do.
       * @param handler The function.
       */
      void set_open_dialog_handler(ActionHandler handler);

      /**
       * Sets the function returning to the open documents.
       * @param handler The function.
       */
      void set_back_handler(ActionHandler handler);

      /**
       * Sets the function notified when the choices change.
       * @param handler The function.
       */
      void set_selections_handler(SelectionsHandler handler);

      /** Returns <code>true</code> iff returning to a document is offered. */
      bool is_back_enabled() const;

      /**
       * Sets whether returning to a document is offered, as it is while a
       * document is open.
       * @param is_enabled Whether it is offered.
       */
      void set_back_enabled(bool is_enabled);

      /** Returns the paths of the recent files listed, newest first. */
      const QStringList& get_recent_paths() const;

      /**
       * Sets the recent files listed.
       * @param paths The paths, newest first.
       */
      void set_recent_paths(const QStringList& paths);

      /** Returns the choices. */
      const HomeSelections& get_selections() const;

      /**
       * Sets the choices.
       * @param selections The choices.
       */
      void set_selections(const HomeSelections& selections);

      /**
       * Returns the preset a tile shows.
       * @param type The tile.
       */
      HomePreset get_shown_preset(HomePresetType type) const;

      /**
       * Creates a document of a tile's preset, as clicking the tile does.
       * @param type The tile.
       */
      void activate(HomePresetType type);

      /**
       * Makes a preset the one its tile shows and creates a document of it,
       * as choosing it from the tile's list does.
       * @param preset The preset.
       */
      void choose(const HomePreset& preset);

    protected:
      void paintEvent(QPaintEvent* event) override;
      void resizeEvent(QResizeEvent* event) override;
      void keyPressEvent(QKeyEvent* event) override;
      void changeEvent(QEvent* event) override;
      bool eventFilter(QObject* watched, QEvent* event) override;

    private:
      class Content;
      class Tile;
      class Card;
      class Button;
      class Back;
      CreateHandler m_create_handler;
      ActionHandler m_new_handler;
      OpenHandler m_open_handler;
      ActionHandler m_open_dialog_handler;
      ActionHandler m_back_handler;
      SelectionsHandler m_selections_handler;
      QStringList m_paths;
      HomeSelections m_selections;
      QString m_width;
      QString m_height;
      Back* m_back;
      Button* m_new_file;
      Button* m_open;
      QScrollArea* m_scroll;
      Content* m_content;

      HomeScreen(const HomeScreen&) = delete;
      HomeScreen& operator =(const HomeScreen&) = delete;
      void create(const HomePreset& preset);
      void open(const QString& path);
      void update_selections(const HomeSelections& selections);
      void set_custom_text(const QString& width, const QString& height);
      void retranslate();
  };
}

#endif
