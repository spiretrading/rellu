#ifndef RELLU_UI_SINGLE_INSTANCE_HPP
#define RELLU_UI_SINGLE_INSTANCE_HPP
#include <functional>
#include <memory>
#include <optional>
#include <QByteArray>
#include <QLocalServer>
#include <QString>
#include <QStringList>

namespace Rellu {

  /**
   * Keeps one running instance of the application per user, a later launch
   * forwarding the files it was given to the running instance.
   */
  class SingleInstance {
    public:

      /** The milliseconds a launch waits for the running instance. */
      static constexpr auto TIMEOUT = 3000;

      /**
       * Constructs a SingleInstance neither forwarding nor listening.
       * @param key The name the running instance listens under.
       */
      explicit SingleInstance(QString key);

      ~SingleInstance();

      /**
       * Forwards files to the instance listening under the key.
       * @param paths The paths of the files.
       * @return <code>true</code> iff an instance received them.
       */
      bool forward(const QStringList& paths) const;

      /**
       * Becomes the running instance, receiving the files later launches
       * forward.
       * @param handler The function called with each launch's files.
       * @return <code>true</code> iff the instance is listening.
       */
      bool listen(std::function<void (const QStringList& paths)> handler);

    private:
      QString m_key;
      std::unique_ptr<QLocalServer> m_server;
      std::function<void (const QStringList& paths)> m_handler;

      SingleInstance(const SingleInstance&) = delete;
      SingleInstance& operator =(const SingleInstance&) = delete;
  };

  /** Returns the name the current user's running instance listens under. */
  QString make_instance_key();

  /**
   * Encodes the files a launch forwards.
   * @param paths The paths of the files.
   */
  QByteArray encode_paths(const QStringList& paths);

  /**
   * Decodes the files a launch forwarded.
   * @param data The data received so far.
   * @return The paths, or <code>std::nullopt</code> if the data is
   *         incomplete or invalid.
   */
  std::optional<QStringList> decode_paths(const QByteArray& data);
}

#endif
