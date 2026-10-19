file(GLOB_RECURSE header_files ${RELLU_INCLUDE_PATH}/Rellu/Localization/*.hpp)
file(GLOB_RECURSE source_files ${RELLU_SOURCE_PATH}/Localization/*.cpp
  ${RELLU_SOURCE_PATH}/Localization/*.hpp)
add_library(RelluLocalization STATIC ${header_files} ${source_files})
source_group("Header Files" FILES ${header_files})
rellu_track_target(RelluLocalization)
