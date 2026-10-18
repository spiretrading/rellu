#!/bin/bash
set -o errexit
set -o pipefail
DIRECTORY=""
ROOT=""
CACHE_DIRECTORY=""
DEPENDENCIES=()

sha256() {
  if command -v sha256sum >/dev/null; then
    sha256sum "$1" | cut -d" " -f1
  else
    shasum -a 256 "$1" | cut -d" " -f1
  fi
}

main() {
  resolve_paths
  CACHE_DIRECTORY="$ROOT/cache_files/rellu"
  mkdir -p "$CACHE_DIRECTORY" || return 1
  add_dependency "doctest-2.4.12" \
    "https://github.com/doctest/doctest/archive/refs/tags/v2.4.12.zip" \
    "7a7afb5f70d0b749d49ddfcb8a454299a8fcd53e9db9c131abe99b456e88a1fe" 1
  local qt_url="https://download.qt.io/official_releases/qt/6.12/6.12.0"
  add_dependency "qtbase-6.12.0" \
    "$qt_url/submodules/qtbase-everywhere-src-6.12.0.tar.xz" \
    "a951bd163c7b80fc6b8c88d7668fb56abf91c152373e13c10666763238131307" 1 \
    "build_qt"
  add_dependency "qtshadertools-6.12.0" \
    "$qt_url/submodules/qtshadertools-everywhere-src-6.12.0.tar.xz" \
    "c7d84f436e1aaef39fdcadebf2bd71bdf24dc69497e13f2e0198873cbbfea2ab" 1 \
    "build_qt_shader_tools"
  local clipper_url="https://github.com/AngusJohnson/Clipper2/archive/refs/tags"
  add_dependency "Clipper2-2.0.1" \
    "$clipper_url/Clipper2_2.0.1.tar.gz" \
    "2a3693aceab4aed3e39b743e038d87701acc53cf05ed7b2013aab3e0aec5287e" 1 \
    "build_clipper2"
  install_dependencies || return 1
}

get_core_count() {
  nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4
}

build_clipper2() {
  local cores
  cores=$(get_core_count)
  rm -rf build install || return 1
  cmake -S CPP -B build -DCMAKE_BUILD_TYPE=Release -DCLIPPER2_UTILS=OFF \
    -DCLIPPER2_EXAMPLES=OFF -DCLIPPER2_TESTS=OFF -DCLIPPER2_USINGZ=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_INSTALL_PREFIX="$ROOT/Clipper2-2.0.1/install" || return 1
  cmake --build build --parallel "$cores" || return 1
  cmake --install build || return 1
  rm -rf build
}

build_qt_shader_tools() {
  local cores
  cores=$(get_core_count)
  rm -rf build || return 1
  mkdir build || return 1
  pushd build > /dev/null || return 1
  "$ROOT/qtbase-6.12.0/install/bin/qt-configure-module" .. \
    -cmake-generator "Unix Makefiles" || { popd > /dev/null; return 1; }
  cmake --build . --parallel "$cores" || { popd > /dev/null; return 1; }
  cmake --install . || { popd > /dev/null; return 1; }
  popd > /dev/null
  rm -rf build
}

build_qt() {
  local cores
  cores=$(get_core_count)
  rm -f "$CACHE_DIRECTORY/qtshadertools-6.12.0.build_complete" || return 1
  rm -rf build || return 1
  mkdir build || return 1
  pushd build > /dev/null || return 1
  ../configure -prefix "$ROOT/qtbase-6.12.0/install" -static -release \
    -cmake-generator "Unix Makefiles" -opensource -confirm-license \
    -nomake examples -nomake tests -qt-freetype -qt-harfbuzz -qt-libjpeg \
    -qt-libpng -qt-pcre -qt-zlib ||
    { popd > /dev/null; return 1; }
  cmake --build . --parallel "$cores" || { popd > /dev/null; return 1; }
  cmake --install . || { popd > /dev/null; return 1; }
  popd > /dev/null
  rm -rf build
}

resolve_paths() {
  local source="${BASH_SOURCE[0]}"
  while [[ -h "$source" ]]; do
    local dir="$(cd -P "$(dirname "$source")" >/dev/null && pwd -P)"
    source="$(readlink "$source")"
    [[ $source != /* ]] && source="$dir/$source"
  done
  DIRECTORY="$(cd -P "$(dirname "$source")" >/dev/null && pwd -P)"
  ROOT="$(pwd -P)"
}

add_dependency() {
  local name="$1"
  local url="$2"
  local hash="$3"
  local revision="$4"
  local build="${5:-}"
  DEPENDENCIES+=("$name|$url|$hash|$revision|$build")
}

install_dependencies() {
  for dep in "${DEPENDENCIES[@]}"; do
    IFS='|' read -r name url hash revision build <<< "$dep"
    download_and_extract "$name" "$url" "$hash" "$revision" "$build" ||
      return 1
  done
}

download_and_extract() {
  local folder="$1"
  local build_marker="$CACHE_DIRECTORY/$folder.build_complete"
  local url="$2"
  local expected_hash="$3"
  local build_hash="$expected_hash posix-$4"
  local build_func="$5"
  local archive="${url##*/}"
  if [[ -d "$folder" && -f "$build_marker" ]] &&
      [[ "$(< "$build_marker")" == "$build_hash" ]]; then
    return 0
  fi
  rm -f "$build_marker" || return 1
  if [[ ! -f "$folder/.rellu_extract_complete" ]] ||
      [[ "$(< "$folder/.rellu_extract_complete")" != "$expected_hash" ]]; then
    rm -f "$folder/.rellu_extract_complete" || return 1
    if [[ ! -f "$archive" ]]; then
      curl -fsSL -o "$archive" "$url" || {
        rm -f "$archive"
        return 1
      }
    fi
    local actual_hash
    actual_hash=$(sha256 "$archive")
    if [[ "$actual_hash" != "$expected_hash" ]]; then
      echo "Error: SHA256 mismatch for $archive."
      echo "  Expected: $expected_hash"
      echo "  Actual:   $actual_hash"
      rm -f "$archive"
      return 1
    fi
    mkdir -p "$folder" || return 1
    if [[ "$archive" == *.zip ]]; then
      unzip -qo "$archive" || return 1
    else
      tar -xf "$archive" --strip-components=1 -C "$folder" || return 1
    fi
    echo "$expected_hash" > "$folder/.rellu_extract_complete" || return 1
  fi
  if [[ -n "$build_func" ]]; then
    pushd "$folder" > /dev/null || return 1
    $build_func || { popd > /dev/null; return 1; }
    popd > /dev/null
  fi
  echo "$build_hash" > "$build_marker" || return 1
  rm -f "$archive"
}

main "$@"
