set(RELLU_INCLUDE_PATH "${CMAKE_CURRENT_LIST_DIR}/../Include")
set(RELLU_SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}/../Source")
set(DOCTEST_INCLUDE_PATH "${PROJECT_BINARY_DIR}/Dependencies/doctest-2.4.12")
set(CLIPPER2_ROOT "${PROJECT_BINARY_DIR}/Dependencies/Clipper2-2.0.1/install")
set(CLIPPER2_INCLUDE_PATH "${CLIPPER2_ROOT}/include")
set(FONTS_ROOT "${PROJECT_BINARY_DIR}/Dependencies")
file(GLOB FONT_FILES
  "${FONTS_ROOT}/roboto-2.136/*.ttf"
  "${FONTS_ROOT}/Recursive-1.085/Recursive_Desktop/Recursive_VF_1.085.ttf"
  "${FONTS_ROOT}/source-sans-pro-3.006R/TTF/*.ttf"
  "${FONTS_ROOT}/RobotoMono-895ec69/*.ttf"
  "${FONTS_ROOT}/Work-Sans-2.010/*.ttf"
  "${FONTS_ROOT}/Raleway-6c67ab1/*.ttf"
  "${FONTS_ROOT}/opensans-bd7e376/*.ttf"
  "${FONTS_ROOT}/ptserif-47d54ca/*.ttf"
  "${FONTS_ROOT}/ptserifcaption-47d54ca/*.ttf")
if(WIN32)
  include("${CMAKE_CURRENT_LIST_DIR}/dependencies.windows.cmake")
else()
  include("${CMAKE_CURRENT_LIST_DIR}/dependencies.posix.cmake")
endif()
