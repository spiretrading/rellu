set(QT_ROOT "${PROJECT_BINARY_DIR}/Dependencies/qtbase-6.12.0/install")
set(QT_INCLUDE_PATH
  "${QT_ROOT}/include"
  "${QT_ROOT}/include/QtCore"
  "${QT_ROOT}/include/QtGui"
  "${QT_ROOT}/include/QtWidgets"
  "${QT_ROOT}/mkspecs/win32-msvc")
set(QT_COMPILE_DEFINITIONS _ENABLE_EXTENDED_ALIGNED_STORAGE QT_CORE_LIB
  QT_GUI_LIB QT_NO_KEYWORDS QT_WIDGETS_LIB UNICODE WIN64 _UNICODE _WIN64)
set(QT_COMPILE_OPTIONS /permissive- /utf-8)
set(QT_LIBRARY_PATH "${QT_ROOT}/lib")
set(QT_PLUGIN_PATH "${QT_ROOT}/plugins")
set(QT_DEBUG_OBJECTS_PATH "${QT_LIBRARY_PATH}/objects-Debug")
set(QT_OPTIMIZED_OBJECTS_PATH "${QT_LIBRARY_PATH}/objects-Release")
set(QT_LIBRARY_DEBUG_PATH)
set(QT_LIBRARY_OPTIMIZED_PATH)
foreach(library Widgets OpenGL Gui Core BundledHarfbuzz BundledFreetype
    BundledLibpng BundledPcre2 BundledZLIB)
  list(APPEND QT_LIBRARY_DEBUG_PATH "${QT_LIBRARY_PATH}/Qt6${library}d.lib")
  list(APPEND QT_LIBRARY_OPTIMIZED_PATH "${QT_LIBRARY_PATH}/Qt6${library}.lib")
endforeach()
foreach(resource Gui_resources_1/qrc_qpdf_init.obj
    Gui_resources_2/qrc_gui_shaders_init.obj
    Widgets_resources_1/qrc_qstyle_init.obj
    Widgets_resources_2/qrc_qstyle1_init.obj
    Widgets_resources_3/qrc_qstyle_fusion_init.obj
    Widgets_resources_4/qrc_qmessagebox_init.obj)
  list(APPEND QT_LIBRARY_DEBUG_PATH "${QT_DEBUG_OBJECTS_PATH}/${resource}")
  list(APPEND QT_LIBRARY_OPTIMIZED_PATH
    "${QT_OPTIMIZED_OBJECTS_PATH}/${resource}")
endforeach()
set(QT_LIBRARIES advapi32 authz comdlg32 d2d1 d3d11 d3d12 d3d9 dwmapi dwrite
  dxgi dxguid gdi32 icuin icuuc imm32 kernel32 mpr mscms netapi32 ntdll ole32
  oleaut32 runtimeobject setupapi shcore shell32 shlwapi synchronization
  uiautomationcore user32 userenv uuid uxtheme version winmm winspool ws2_32
  wtsapi32)
set(QT_PRIVATE_INCLUDE_PATH
  "${QT_ROOT}/include/QtCore/6.12.0"
  "${QT_ROOT}/include/QtCore/6.12.0/QtCore"
  "${QT_ROOT}/include/QtGui/6.12.0"
  "${QT_ROOT}/include/QtGui/6.12.0/QtGui")
set(QT_ENTRY_POINT_LIBRARY_DEBUG_PATH "${QT_LIBRARY_PATH}/Qt6EntryPointd.lib")
set(QT_ENTRY_POINT_LIBRARY_OPTIMIZED_PATH
  "${QT_LIBRARY_PATH}/Qt6EntryPoint.lib")
set(QT_WINDOWS_LIBRARY_DEBUG_PATH "${QT_PLUGIN_PATH}/platforms/qwindowsd.lib")
set(QT_WINDOWS_LIBRARY_OPTIMIZED_PATH
  "${QT_PLUGIN_PATH}/platforms/qwindows.lib")
foreach(resource
    QWindowsIntegrationPlugin_resources_1/qrc_openglblacklists_init.obj
    QWindowsIntegrationPlugin_resources_2/qrc_cursors_init.obj)
  list(APPEND QT_WINDOWS_LIBRARY_DEBUG_PATH
    "${QT_DEBUG_OBJECTS_PATH}/${resource}")
  list(APPEND QT_WINDOWS_LIBRARY_OPTIMIZED_PATH
    "${QT_OPTIMIZED_OBJECTS_PATH}/${resource}")
endforeach()
set(QT_OFFSCREEN_LIBRARY_DEBUG_PATH
  "${QT_PLUGIN_PATH}/platforms/qoffscreend.lib")
set(QT_OFFSCREEN_LIBRARY_OPTIMIZED_PATH
  "${QT_PLUGIN_PATH}/platforms/qoffscreen.lib")
set(QT_MODERN_WINDOWS_STYLE_LIBRARY_DEBUG_PATH
  "${QT_PLUGIN_PATH}/styles/qmodernwindowsstyled.lib")
set(QT_MODERN_WINDOWS_STYLE_LIBRARY_OPTIMIZED_PATH
  "${QT_PLUGIN_PATH}/styles/qmodernwindowsstyle.lib")
set(QT_IMAGE_FORMATS_LIBRARY_DEBUG_PATH
  "${QT_PLUGIN_PATH}/imageformats/qjpegd.lib"
  "${QT_PLUGIN_PATH}/imageformats/qgifd.lib"
  "${QT_LIBRARY_PATH}/Qt6BundledLibjpegd.lib")
set(QT_IMAGE_FORMATS_LIBRARY_OPTIMIZED_PATH
  "${QT_PLUGIN_PATH}/imageformats/qjpeg.lib"
  "${QT_PLUGIN_PATH}/imageformats/qgif.lib"
  "${QT_LIBRARY_PATH}/Qt6BundledLibjpeg.lib")
set(QT_SHADER_COMPILER_PATH "${QT_ROOT}/bin/qsb.exe")
set(HARFBUZZ_INCLUDE_PATH "${QT_ROOT}/../src/3rdparty/harfbuzz-ng/src")
set(FREETYPE_INCLUDE_PATH "${QT_ROOT}/include/QtFreetype")
set(CLIPPER2_LIBRARY_DEBUG_PATH "${CLIPPER2_ROOT}/lib/Clipper2d.lib")
set(CLIPPER2_LIBRARY_OPTIMIZED_PATH "${CLIPPER2_ROOT}/lib/Clipper2.lib")
