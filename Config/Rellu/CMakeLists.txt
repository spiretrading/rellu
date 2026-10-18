file(GLOB header_files ${RELLU_SOURCE_PATH}/Rellu/*.hpp)
file(GLOB source_files ${RELLU_SOURCE_PATH}/Rellu/*.cpp)
add_executable(rellu WIN32 ${header_files} ${source_files})
target_link_libraries(rellu PRIVATE RelluIo RelluScene)
rellu_link_dependencies(
  rellu PRIVATE QT_ENTRY_POINT QT_MODERN_WINDOWS_STYLE QT_WINDOWS)
rellu_install_target(rellu "${APPLICATION_INSTALL_DIRECTORY}")
