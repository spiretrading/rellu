#include <atomic>
#include <thread>
#include <vector>
#include <doctest/doctest.h>
#include <QApplication>
#include <QElapsedTimer>
#include <QUuid>
#include "Rellu/Ui/SingleInstance.hpp"

using namespace Rellu;

namespace {
  QString make_test_key() {
    return "RelluTest-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
  }

  void wait_until(const auto& condition) {
    auto timer = QElapsedTimer();
    timer.start();
    while(!condition() && timer.elapsed() < 2 * SingleInstance::TIMEOUT) {
      QApplication::processEvents(QEventLoop::AllEvents, 10);
    }
  }
}

TEST_SUITE("SingleInstance") {
  TEST_CASE("encode_paths") {
    auto paths = QStringList({"C:\\Designs\\A.xd", QString(), "b\xC3\xA9.xd"});
    auto data = encode_paths(paths);
    REQUIRE(decode_paths(data) == paths);
    REQUIRE(decode_paths(encode_paths(QStringList())) == QStringList());
    REQUIRE(!decode_paths(data.left(data.size() - 1)));
    REQUIRE(!decode_paths(data.left(3)));
    REQUIRE(!decode_paths(QByteArray("XXXX") + data.mid(4)));
  }

  TEST_CASE("instance_key") {
    REQUIRE(make_instance_key() == make_instance_key());
    REQUIRE(make_instance_key().startsWith("Rellu-"));
  }

  TEST_CASE("forward_without_instance") {
    auto instance = SingleInstance(make_test_key());
    REQUIRE(!instance.forward(QStringList({"a.xd"})));
  }

  TEST_CASE("forward") {
    auto key = make_test_key();
    auto running = SingleInstance(key);
    auto received = std::vector<QStringList>();
    REQUIRE(running.listen([&] (const auto& paths) {
      received.push_back(paths);
    }));
    for(auto& paths : {QStringList({"C:\\Designs\\A.xd"}), QStringList()}) {
      auto is_done = std::atomic_bool(false);
      auto is_forwarded = std::atomic_bool(false);
      auto launch = std::thread([&] {
        auto instance = SingleInstance(key);
        is_forwarded = instance.forward(paths);
        is_done = true;
      });
      wait_until([&] {
        return is_done.load();
      });
      launch.join();
      REQUIRE(is_forwarded);
      REQUIRE(!received.empty());
      REQUIRE(received.back() == paths);
    }
    REQUIRE(received.size() == 2);
  }
}
