file(GLOB source_files ${RELLU_SOURCE_PATH}/Rellu/*.cpp)
qt_add_executable(rellu WIN32 ${source_files})
target_link_libraries(rellu PRIVATE RelluRendering Qt6::Widgets)
rellu_install_target(rellu "${APPLICATION_INSTALL_DIRECTORY}")
