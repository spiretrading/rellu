#ifndef RELLU_IO_XD_PACKAGE_HPP
#define RELLU_IO_XD_PACKAGE_HPP
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <QByteArray>
#include <QJsonObject>

namespace Rellu {

  /**
   * Reads the entries of an Adobe XD package, a ZIP archive holding a
   * manifest, JSON graphic content, prototype interactions and image
   * resources. Its methods may be called from several threads at once, and
   * every failure throws XdError.
   */
  class XdPackage {
    public:

      /** The name of the manifest entry. */
      static constexpr auto MANIFEST = std::string_view("manifest");

      /** The name of the entry holding the shared resources. */
      static constexpr auto RESOURCES =
        std::string_view("resources/graphics/graphicContent.agc");

      /** The name of the entry holding the top-level pasteboard nodes. */
      static constexpr auto PASTEBOARD =
        std::string_view("artwork/pasteboard/graphics/graphicContent.agc");

      /** The name of the entry holding the prototype interactions. */
      static constexpr auto INTERACTIONS =
        std::string_view("interactions/interactions.json");

      /**
       * Opens a package from a file.
       * @param path The path of the .xd file.
       */
      explicit XdPackage(const std::filesystem::path& path);

      /**
       * Opens a package from its bytes.
       * @param data The contents of an .xd file.
       */
      explicit XdPackage(QByteArray data);

      XdPackage(XdPackage&& package) noexcept;
      ~XdPackage();

      /** Returns the names of the entries in archive order. */
      const std::vector<std::string>& get_entries() const;

      /**
       * Tests whether the package has an entry.
       * @param name The entry's name.
       * @return <code>true</code> iff the entry is present.
       */
      bool contains(std::string_view name) const;

      /**
       * Reads an entry.
       * @param name The entry's name.
       * @return The entry's uncompressed contents.
       */
      QByteArray read(std::string_view name) const;

      /**
       * Reads an entry holding a JSON object.
       * @param name The entry's name.
       * @return The parsed object.
       */
      QJsonObject read_json(std::string_view name) const;

      /** Returns the parsed manifest. */
      const QJsonObject& get_manifest() const;

      /** Reads the graphic content holding the shared resources. */
      QJsonObject read_resources() const;

      /** Reads the graphic content holding the top-level pasteboard nodes. */
      QJsonObject read_pasteboard() const;

      /**
       * Returns the entries holding the graphic content of each artboard, in
       * the manifest's order followed by any entry the manifest omits.
       */
      const std::vector<std::string>& get_artboard_entries() const;

      /**
       * Reads the prototype interactions.
       * @return The interactions, or an empty object if the package has
       *         none.
       */
      QJsonObject read_interactions() const;

      /** Returns the entries holding image resources, in archive order. */
      const std::vector<std::string>& get_image_entries() const;

      XdPackage& operator =(XdPackage&& package) noexcept;

    private:
      struct Data;
      std::unique_ptr<Data> m_data;

      XdPackage(const XdPackage&) = delete;
      XdPackage& operator =(const XdPackage&) = delete;
      void initialize();
  };
}

#endif
