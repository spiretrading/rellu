file(GLOB_RECURSE header_files ${RELLU_INCLUDE_PATH}/Rellu/Io/*.hpp)
file(GLOB_RECURSE source_files ${RELLU_SOURCE_PATH}/Io/*.cpp)
add_library(RelluIo STATIC ${header_files} ${source_files})
source_group("Header Files" FILES ${header_files})
target_link_libraries(RelluIo PUBLIC RelluCommands RelluGeometry
  RelluLocalization RelluModel RelluScene)
rellu_link_dependencies(RelluIo PUBLIC QT QT_PRIVATE)
rellu_link_dependencies(RelluIo PRIVATE QT_IMAGE_FORMATS)
rellu_track_target(RelluIo)
