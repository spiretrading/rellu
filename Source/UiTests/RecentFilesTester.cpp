#include <doctest/doctest.h>
#include <QDir>
#include "Rellu/Ui/RecentFiles.hpp"

using namespace Rellu;

namespace {
  QString make_path(const QString& name) {
    return QDir::toNativeSeparators(QDir::current().absoluteFilePath(name));
  }
}

TEST_SUITE("RecentFiles") {
  TEST_CASE("add") {
    auto files = RecentFiles(3);
    files.add("a.xd");
    files.add("b.xd");
    REQUIRE(files.get_paths() ==
      QStringList({make_path("b.xd"), make_path("a.xd")}));
    files.add("a.xd");
    REQUIRE(files.get_paths() ==
      QStringList({make_path("a.xd"), make_path("b.xd")}));
    files.add("c.xd");
    files.add("d.xd");
    REQUIRE(files.get_paths() ==
      QStringList({make_path("d.xd"), make_path("c.xd"), make_path("a.xd")}));
  }

  TEST_CASE("remove") {
    auto files = RecentFiles();
    files.add("a.xd");
    files.add("b.xd");
    files.remove("a.xd");
    REQUIRE(files.get_paths() == QStringList({make_path("b.xd")}));
    files.clear();
    REQUIRE(files.get_paths().isEmpty());
  }

  TEST_CASE("set_paths") {
    auto files = RecentFiles(2);
    files.set_paths(QStringList({"a.xd", "", "b.xd", "a.xd", "c.xd"}));
    REQUIRE(files.get_paths() ==
      QStringList({make_path("a.xd"), make_path("b.xd")}));
  }
}
