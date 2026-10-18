file(GLOB source_files ${RELLU_SOURCE_PATH}/RenderingTests/*.cpp)
qt_add_executable(RenderingTests ${source_files})
target_include_directories(RenderingTests SYSTEM PRIVATE
  ${DOCTEST_INCLUDE_PATH})
target_link_libraries(RenderingTests PRIVATE RelluRendering Qt6::Gui)
qt_import_plugins(RenderingTests INCLUDE Qt6::QOffscreenIntegrationPlugin)
add_custom_command(TARGET RenderingTests POST_BUILD COMMAND RenderingTests)
rellu_install_target(RenderingTests "${TEST_INSTALL_DIRECTORY}/$<CONFIG>")
