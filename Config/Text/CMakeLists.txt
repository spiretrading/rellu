file(GLOB_RECURSE header_files ${RELLU_INCLUDE_PATH}/Rellu/Text/*.hpp)
file(GLOB_RECURSE source_files ${RELLU_SOURCE_PATH}/Text/*.cpp
  ${RELLU_SOURCE_PATH}/Text/*.hpp)
add_library(RelluText STATIC ${header_files} ${source_files})
source_group("Header Files" FILES ${header_files})
target_link_libraries(RelluText PUBLIC RelluModel RelluGeometry)
rellu_link_dependencies(RelluText PUBLIC QT)
rellu_link_dependencies(RelluText PRIVATE FREETYPE HARFBUZZ)
rellu_track_target(RelluText)
