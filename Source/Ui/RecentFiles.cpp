#include <algorithm>
#include <QDir>
#include <QFileInfo>
#include "Rellu/Ui/RecentFiles.hpp"

using namespace Rellu;

namespace {
  QString normalize(const QString& path) {
    return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
  }
}

RecentFiles::RecentFiles()
  : RecentFiles(DEFAULT_CAPACITY) {}

RecentFiles::RecentFiles(int capacity)
  : m_capacity(std::max(capacity, 0)) {}

const QStringList& RecentFiles::get_paths() const {
  return m_paths;
}

void RecentFiles::set_paths(const QStringList& paths) {
  m_paths.clear();
  for(auto i = paths.rbegin(); i != paths.rend(); ++i) {
    if(!i->isEmpty()) {
      add(*i);
    }
  }
}

void RecentFiles::add(const QString& path) {
  auto normalized = normalize(path);
  remove(normalized);
  m_paths.prepend(normalized);
  while(m_paths.size() > m_capacity) {
    m_paths.removeLast();
  }
}

void RecentFiles::remove(const QString& path) {
  m_paths.removeIf([&] (const auto& entry) {
    return is_same_file(entry, path);
  });
}

void RecentFiles::clear() {
  m_paths.clear();
}

bool Rellu::is_same_file(const QString& left, const QString& right) {
  auto left_info = QFileInfo(left);
  auto right_info = QFileInfo(right);
  if(left_info.exists() && right_info.exists()) {
    return left_info == right_info;
  }
  return normalize(left) == normalize(right);
}
