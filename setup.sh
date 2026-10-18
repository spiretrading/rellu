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
  local recursive_url="https://github.com/arrowtype/recursive/releases"
  add_dependency "Recursive-1.085" \
    "$recursive_url/download/v1.085/ArrowType-Recursive-1.085.zip" \
    "cbcbdf7a0e181d284a9235e09ed5f3873e527bc5dd1d977df71cdc1ff937da02" 1
  local source_sans_url="https://github.com/adobe-fonts/source-sans/releases"
  add_dependency "source-sans-pro-3.006R" \
    "$source_sans_url/download/3.006R/source-sans-pro-3.006R.zip" \
    "b22c7a4c45050ed8ae1b5293a9fd3b886d6f5150eb1e2ded429b9a11ac1602c5" 1
  add_files "RobotoMono-895ec69" "googlefonts/RobotoMono" \
    "895ec691990d041dd727c7b5afa3ce56525d98e6" "fonts/ttf" 1
  add_file "RobotoMono-Bold.ttf" \
    "3ecf35e5e87accc7578b605d1f5f0bc30d88b195d6807bec8a0c57f6aa95c4db"
  add_file "RobotoMono-BoldItalic.ttf" \
    "a0f16567447311eaf42a35f6c50eb64b911694b42f1b01038e3b7e92c20f131d"
  add_file "RobotoMono-Italic.ttf" \
    "4549325cd2d10938d37d63eba2aaca7c2e16e48322dc767576eab45e512b6ad2"
  add_file "RobotoMono-Light.ttf" \
    "8045de2596189296c6ac8aaa76316a7b8289ee8b20a66d3d58122a0b8fc4391d"
  add_file "RobotoMono-LightItalic.ttf" \
    "2f8dc95270e28464f26dde41aa515a25a2b6e81f0ba9a4168f3f73f51dbeefe3"
  add_file "RobotoMono-Medium.ttf" \
    "4acbb2725a260eafff1bcaade3501204ca774e56346e259ee1b61d56996d2611"
  add_file "RobotoMono-MediumItalic.ttf" \
    "e01593c1cf8034729deb4ebe412d19884907ca1156962169bca624ef139d827a"
  add_file "RobotoMono-Regular.ttf" \
    "af0bff7599c3df3831755c16e39b3c496df74b8c8d8a1161b14dc8461be17cb4"
  add_file "RobotoMono-Thin.ttf" \
    "77c06a7cca40839e7e98f08837bb2a9510690472ad156b32f663c05ceff13186"
  add_file "RobotoMono-ThinItalic.ttf" \
    "78b05d06047962c6ba4cb6a8301acca3986ca7d51e4d34a67e4fd82e6e515617"
  add_files "Work-Sans-2.010" "weiweihuanghuang/Work-Sans" \
    "6634e7b2395e9dba4ac8611c4529968a9719d606" "fonts/static/TTF" 1
  add_file "WorkSans-Black.ttf" \
    "5cb2a4c90dd9c4504ed002e608863346368c0ac7e3f6a71d6c03643c30a91ba4"
  add_file "WorkSans-BlackItalic.ttf" \
    "1500e83d92f3117ed62a1720c111e1d10508251c5ea6cb14f1907364d337ff90"
  add_file "WorkSans-Bold.ttf" \
    "e5c36b1191be27046270da352f231fc342ad4333cfb2967f393e00d18c274cdc"
  add_file "WorkSans-BoldItalic.ttf" \
    "8f7b9e47221fab81026d48d1d746f3e8cbad4b045524c09645d052efd181ca19"
  add_file "WorkSans-ExtraBold.ttf" \
    "d47d48e8290a4d1a90faed7812edad2a1418295a431859557d618ed9247f2aed"
  add_file "WorkSans-ExtraBoldItalic.ttf" \
    "ec8d47247d300bf96282c3092be43086e5e0162482600e057199474fa0dde163"
  add_file "WorkSans-ExtraLight.ttf" \
    "7939231e7d9abfa68030383bdb8ac9fc05b0cc7f50da45b76018778dd9a41be8"
  add_file "WorkSans-ExtraLightItalic.ttf" \
    "c62370f8294f7f97c956aae8bb0c862c05d6dd88a780305978144697d152b83f"
  add_file "WorkSans-Italic.ttf" \
    "8fcbc2499374a09594ff28ed488852191f3451ec2ef430d421ec327b3d6c15e8"
  add_file "WorkSans-Light.ttf" \
    "16d4241f82605d7a104e49ace6b11c07c1ed4e5b70bbdc6cd88311a3152665eb"
  add_file "WorkSans-LightItalic.ttf" \
    "212687f73b6f752d51a8ebc73b73dcfeb0c4a2b8af95b8bd0d408aa2ac25cd52"
  add_file "WorkSans-Medium.ttf" \
    "3c5ae1ef9260a0c1cdf1f59841f28620c40826f6ca3bd4c71516997c29fec7dc"
  add_file "WorkSans-MediumItalic.ttf" \
    "81877c8081f4d68a4932b5128438cd892df72501206c17a5f7437a4ad86b6dab"
  add_file "WorkSans-Regular.ttf" \
    "22e7f1607ebc29d03be61d893ec47dde307847eaf60fbec260e286695001982a"
  add_file "WorkSans-SemiBold.ttf" \
    "399b8be04df8e5c531cc1cf490d9d4d781299440e086d61097db5175e2c71321"
  add_file "WorkSans-SemiBoldItalic.ttf" \
    "c5e1b356c227239cd3003e606af03ca7199194e67f83639e32406ac269d0ca48"
  add_file "WorkSans-Thin.ttf" \
    "91d398dfc6367fe18e80e296d219e99d73507d6a0ac927d27188104a68678953"
  add_file "WorkSans-ThinItalic.ttf" \
    "08ae234d96652a4befd4dea18538ea71a53211823932a4f554dc338174e9e285"
  add_files "Raleway-6c67ab1" "impallari/Raleway" \
    "6c67ab1f7aa65c442bd2745bb9d4ef1cd7bc01fa" "fonts/v3.000 Fontlab/TTF" 1
  add_file "Raleway-Black-Italic.ttf" \
    "332be78c28e10f13eb395f50dd55fcc508c303d54948ce22f35d07706c072981"
  add_file "Raleway-Black.ttf" \
    "f27f4ec663f6d59f4c14a324d45c520b27b034c94381977f61873008a094c874"
  add_file "Raleway-Bold-Italic.ttf" \
    "88f61a661d8ccdc2189b9baf5a46f26a3889c3902e27dd4b07b1f5845172cdfe"
  add_file "Raleway-Bold.ttf" \
    "4bbee5ac4b10b4fbe5fdb0bff6a75898d8800c62ad16311484e8680ef46b5701"
  add_file "Raleway-ExtraBold-Italic.ttf" \
    "617165bc6f2d085b0dd94ee0ba82034cf17d2fa621dcf4fff70064e91f995566"
  add_file "Raleway-ExtraBold.ttf" \
    "d8a96c8de333a8854254bbd9a6022df2bb434205e82c0987d944e1f74e2e6cf5"
  add_file "Raleway-ExtraLight-Italic.ttf" \
    "58d3dc30e5b4b5c903fe0cad25f9245ac9026176e7455a356b3b7b8ba8fee534"
  add_file "Raleway-ExtraLight.ttf" \
    "dbe8a009a21ed2e6ad0f7b7eaccb630c3e33708a4301ec0e36f2f562b9d1c11d"
  add_file "Raleway-Light-Italic.ttf" \
    "a25be620f79edf1bdd37377cf94017b3d3d85223d8bd4deec9b33d385ba43535"
  add_file "Raleway-Light.ttf" \
    "d11b6f8bbb1025a56e94141b4fe29ff6c8cd67c60a0adeb7165679e5fba70988"
  add_file "Raleway-Medium-Italic.ttf" \
    "5cfee7494e1af0f1c4e97ee000c703836971ff78b95a325fe330f84e492ec336"
  add_file "Raleway-Medium.ttf" \
    "84e15d32decbb3e051c169b4023d7debafa044587b8f12eeb3324f4ed2683f48"
  add_file "Raleway-Regular-Italic.ttf" \
    "104679ffca1064f25c27f71c6e672b5d2d21ec1cbaf0da93b84fdd4b7c79926f"
  add_file "Raleway-Regular.ttf" \
    "1ac5d5e41cd972d0700f41a03dd25a1315ae872b774a17150dbd835ba7cbb7d9"
  add_file "Raleway-SemiBold-Italic.ttf" \
    "7d86dc1c7c5d0f52d8ce87648edefaa7c156ddae01cbfc6f53868db08f26baab"
  add_file "Raleway-SemiBold.ttf" \
    "ce7712f51d2775f751087b9a13ccaee41ef6d7a822c69677e738f679f6f8eb2a"
  add_file "Raleway-Thin-Italic.ttf" \
    "49b03e5c0baaefcb7ba3b10221d6e1096454c92072042872ddfe9adfb7550af4"
  add_file "Raleway-Thin.ttf" \
    "62c89b72e490e3c7832f9c5ab4e8f554e57356fefd2da31f3e9d0a15b564823e"
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
  add_files "ptserif-47d54ca" "google/fonts" \
    "47d54ca1ebb14fd864695f1d39dc734f39017c33" "ofl/ptserif" 1
  add_file "PT_Serif-Web-Bold.ttf" \
    "038ba7336bd7ea14f12ad155bed51a4345cac5153275d521dec3ba04021c526e"
  add_file "PT_Serif-Web-BoldItalic.ttf" \
    "f003788ba08981eb0988b3557a6f224a53dab49c20e283e8b74d5af3c466f8be"
  add_file "PT_Serif-Web-Italic.ttf" \
    "f57e95ff9dc85691a3b2e193f2028db36f6663939a46c0fc4f286d618b80b7ce"
  add_file "PT_Serif-Web-Regular.ttf" \
    "a4951fade06ff8f09b7673aa81ffb65a8cd409e24d3289a6dc670bc4dda2557a"
  add_files "ptserifcaption-47d54ca" "google/fonts" \
    "47d54ca1ebb14fd864695f1d39dc734f39017c33" "ofl/ptserifcaption" 1
  add_file "PT_Serif-Caption-Web-Italic.ttf" \
    "afe69233498676c418383b9defda8aace407eca0e86f4499d9ba4e9f494f9da5"
  add_file "PT_Serif-Caption-Web-Regular.ttf" \
    "d17d47f44e840eee017f839b27dff3c94c7afe7743eff308d4bd11f7018b69af"
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
