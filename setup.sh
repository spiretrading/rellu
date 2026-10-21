#!/bin/bash
set -o errexit
set -o pipefail
DIRECTORY=""
ROOT=""
CACHE_DIRECTORY=""
DEPENDENCIES=()
FILES=()

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
  local roboto_url="https://github.com/googlefonts/roboto-2/releases"
  add_dependency "roboto-2.136" \
    "$roboto_url/download/v2.136/roboto-hinted.zip" \
    "c4528791de55ade3d6c698738a70b457311e4dd296c5a3318aa729874067fa6a" 1
  add_files "opensans-bd7e376" "googlefonts/opensans" \
    "bd7e37632246368c60fdcbd374dbf9bad11969b6" "fonts/noto-set/ttf" 1
  add_file "OpenSans-Bold.ttf" \
    "b9652382e4d6f16b5cc0b9e44809e520c8965f97aad2a61cce103ebe1be61ad5"
  add_file "OpenSans-BoldItalic.ttf" \
    "6be38771f2ba835b1515bb6a3cce56900275760a1b4768ed075d049721d964f0"
  add_file "OpenSans-CondensedBold.ttf" \
    "28ea9deff91ec78fe7d3c66a4ee702af156b9ad327b0827e6043f94772aab21c"
  add_file "OpenSans-CondensedBoldItalic.ttf" \
    "9b28853d915567ce5d8cf13f1433c617c135bc4ab388f401123c25b9e7d12702"
  add_file "OpenSans-CondensedExtraBold.ttf" \
    "eb07aed2b8c816e1bf765b526e3b6b4c3c1e8e8dcc2735cf6bb209bf2e935333"
  add_file "OpenSans-CondensedExtraBoldItalic.ttf" \
    "72d66673d80a5b7ddf346b463c4e8f74541d305fb1b64607f0a21381ace8cd34"
  add_file "OpenSans-CondensedItalic.ttf" \
    "0cbe24043f25200ef026c8636a7a9f3f6f53c4b0e29ddc536f3845c9c97d89f5"
  add_file "OpenSans-CondensedLight.ttf" \
    "1bac1fb62720b1d388d676369bc3389b60cbcfce677396981a7c98a4cd4428fc"
  add_file "OpenSans-CondensedLightItalic.ttf" \
    "4d3ba6580e6727b4d41aaef8c91bc020c0a41f4113bbd32869a5dbfcf24b9b40"
  add_file "OpenSans-CondensedRegular.ttf" \
    "7fd8ec690d06b7504e1803163f4ba1e4eea08182af50deb8872b26597075ffee"
  add_file "OpenSans-CondensedSemiBold.ttf" \
    "7013d0d09a3bcffbc96883d7ec1170577308eaf9fc1c6c44d23801d56e34e93e"
  add_file "OpenSans-CondensedSemiBoldItalic.ttf" \
    "701d5076f8c949849a671adc536491287df62ee97bae1e091e547122ceeaab61"
  add_file "OpenSans-ExtraBold.ttf" \
    "58131e8ded443333fdeda104384b1c637863636c0517f4af5891965f2f39e05c"
  add_file "OpenSans-ExtraBoldItalic.ttf" \
    "c265dd757e3e40c40f55a5d377fe5a74ac4b15eda66db56bbbfd1be7df34eac1"
  add_file "OpenSans-Italic.ttf" \
    "b9980136238347f81f9865bcee65d52ca432b7e05fd479af1484983988432def"
  add_file "OpenSans-Light.ttf" \
    "493c5e9f31c425745da143dcc1b3513e9d36ea3c444d45dda6ca6f5eab86fff4"
  add_file "OpenSans-LightItalic.ttf" \
    "f441a8b24bcab020f040ed741371205246a6375fa5531ad67debab3f9daaa33f"
  add_file "OpenSans-Regular.ttf" \
    "ce57ff2cbd11a2ce997256c3ec5d7c9e8a8199141bd7aaceda77f37fbecd71b3"
  add_file "OpenSans-SemiBold.ttf" \
    "ce59e775d558eefea4c8b01365acc14806cf7d83e18aff65ca1f9d3de4655d30"
  add_file "OpenSans-SemiBoldItalic.ttf" \
    "b5c47cd18612a43a2059a5d854fd4c04e08b68bc95fa9a6d27287c7857a389a4"
  install_dependencies || return 1
  install_harfbuzz_headers "qtbase-6.12.0" || return 1
}

install_harfbuzz_headers() {
  local folder="$1"
  local directory="$ROOT/$folder/install/include/harfbuzz"
  local marker="$CACHE_DIRECTORY/harfbuzz-headers.build_complete"
  local build_hash
  build_hash="$(< "$CACHE_DIRECTORY/$folder.build_complete")" || return 1
  if [[ -d "$directory" && -f "$marker" ]] &&
      [[ "$(< "$marker")" == "$build_hash" ]]; then
    return 0
  fi
  rm -f "$marker" || return 1
  rm -rf "$directory" || return 1
  mkdir -p "$directory" || return 1
  cp "$ROOT/$folder/src/3rdparty/harfbuzz-ng/src/"*.h "$directory" ||
    return 1
  echo "$build_hash" > "$marker" || return 1
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

add_files() {
  local name="$1"
  local url="https://raw.githubusercontent.com/$2/$3/$4"
  local revision="$5"
  DEPENDENCIES+=("$name|$url||$revision|")
}

add_file() {
  FILES+=("$((${#DEPENDENCIES[@]} - 1))|$1|$2")
}

install_dependencies() {
  local index=0
  for dep in "${DEPENDENCIES[@]}"; do
    IFS='|' read -r name url hash revision build <<< "$dep"
    if [[ -z "$hash" ]]; then
      download_files "$index" "$name" "$url" "$revision" || return 1
    else
      download_and_extract "$name" "$url" "$hash" "$revision" "$build" ||
        return 1
    fi
    index=$((index + 1))
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
      cmake -DARCHIVE:FILEPATH="$archive" -DDESTINATION:PATH="$folder" \
        -DSTRIP_COMPONENTS=1 -P "$DIRECTORY/Config/extract.cmake" ||
        return 1
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

download_files() {
  local index="$1"
  local folder="$2"
  local url="$3"
  local build_marker="$CACHE_DIRECTORY/$folder.build_complete"
  local manifest="$CACHE_DIRECTORY/$folder.manifest"
  local files=()
  local entry file_index name hash
  for entry in "${FILES[@]}"; do
    IFS='|' read -r file_index name hash <<< "$entry"
    if [[ "$file_index" == "$index" ]]; then
      files+=("$name|$hash")
    fi
  done
  {
    echo "$url"
    for entry in "${files[@]}"; do
      echo "${entry/|/ }"
    done
  } > "$manifest" || return 1
  local build_hash
  build_hash="$(sha256 "$manifest") posix-$4"
  rm -f "$manifest"
  if [[ -d "$folder" && -f "$build_marker" ]] &&
      [[ "$(< "$build_marker")" == "$build_hash" ]]; then
    return 0
  fi
  rm -f "$build_marker" || return 1
  mkdir -p "$folder" || return 1
  for entry in "${files[@]}"; do
    IFS='|' read -r name hash <<< "$entry"
    download_file "$url/$name" "$folder/$name" "$hash" || return 1
  done
  echo "$build_hash" > "$folder/.rellu_download_complete" || return 1
  echo "$build_hash" > "$build_marker" || return 1
}

download_file() {
  local url="${1// /%20}"
  local path="$2"
  local expected_hash="$3"
  if [[ -f "$path" ]]; then
    if [[ "$(sha256 "$path")" == "$expected_hash" ]]; then
      return 0
    fi
    rm -f "$path" || return 1
  fi
  curl -fsSL -o "$path.download" "$url" || {
    rm -f "$path.download"
    return 1
  }
  local actual_hash
  actual_hash=$(sha256 "$path.download")
  if [[ "$actual_hash" != "$expected_hash" ]]; then
    echo "Error: SHA256 mismatch for $url."
    echo "  Expected: $expected_hash"
    echo "  Actual:   $actual_hash"
    rm -f "$path.download"
    return 1
  fi
  mv -f "$path.download" "$path" || return 1
}

main "$@"
