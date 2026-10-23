file(GLOB_RECURSE header_files ${RELLU_INCLUDE_PATH}/Rellu/Ui/*.hpp)
file(GLOB_RECURSE source_files ${RELLU_SOURCE_PATH}/Ui/*.cpp
  ${RELLU_SOURCE_PATH}/Ui/*.hpp)
add_library(RelluUi STATIC ${header_files} ${source_files})
source_group("Header Files" FILES ${header_files})
target_link_libraries(
  RelluUi PUBLIC RelluEditor RelluIo RelluPrototype RelluText)
rellu_link_dependencies(RelluUi PUBLIC QT QT_NETWORK QT_PRIVATE)
rellu_track_target(RelluUi)
