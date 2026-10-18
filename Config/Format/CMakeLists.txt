file(GLOB_RECURSE header_files ${RELLU_INCLUDE_PATH}/Rellu/Format/*.hpp)
file(GLOB_RECURSE source_files ${RELLU_SOURCE_PATH}/Format/*.cpp
  ${RELLU_SOURCE_PATH}/Format/*.hpp)
add_library(RelluFormat STATIC ${header_files} ${source_files})
source_group("Header Files" FILES ${header_files})
target_link_libraries(RelluFormat PUBLIC RelluModel RelluGeometry)
rellu_link_dependencies(RelluFormat PRIVATE QT)
rellu_track_target(RelluFormat)
