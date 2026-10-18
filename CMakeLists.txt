cmake_minimum_required(VERSION 3.28)
project(rellu LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(D "${CMAKE_BINARY_DIR}/Dependencies" CACHE STRING
  "Path to dependencies folder.")
set(DEFAULT_BUILD_TYPE "Release")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "${DEFAULT_BUILD_TYPE}" CACHE
    STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()
if(CMAKE_CONFIGURATION_TYPES AND
    NOT "MinSizeRel" IN_LIST CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_CONFIGURATION_TYPES "${CMAKE_CONFIGURATION_TYPES};MinSizeRel"
    CACHE STRING "Available build configurations." FORCE)
endif()
if(WIN32)
  set(configure_script
    cmd /c CALL "${CMAKE_SOURCE_DIR}/configure.bat" -DD "${D}"
    "${CMAKE_BUILD_TYPE}")
elseif(UNIX)
  set(configure_script "${CMAKE_SOURCE_DIR}/configure.sh" "-DD=${D}"
    "${CMAKE_BUILD_TYPE}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "RELLU_SKIP_CMAKE=1" ${configure_script}
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" RESULT_VARIABLE configure_result
  OUTPUT_VARIABLE configure_output ERROR_VARIABLE configure_error
  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "Configuration script failed with error:\n"
    "${configure_error}\nOutput:\n${configure_output}")
endif()
include(Config/dependencies.cmake)
list(APPEND CMAKE_PREFIX_PATH "${QT_PATH}")
find_package(Qt6 6.12 REQUIRED COMPONENTS GuiPrivate Widgets)
set(TEST_INSTALL_DIRECTORY "${PROJECT_BINARY_DIR}/Tests")
set(APPLICATION_INSTALL_DIRECTORY "${PROJECT_BINARY_DIR}/Application")
set(RELLU_SANITIZER "none" CACHE STRING "The sanitizer to build with.")
set_property(CACHE RELLU_SANITIZER PROPERTY STRINGS "none" "address" "thread")
enable_testing()
if(MSVC)
  list(APPEND CMAKE_VS_GLOBALS
    UseMultiToolTask=true EnforceProcessCountAcrossBuilds=true
    "CL_MPCount=$([System.Environment]::ProcessorCount)")
  add_compile_options(/bigobj /external:anglebrackets /external:W0
    $<$<CONFIG:Release>:/GL> /MP /WX /Zc:__cplusplus /Zc:preprocessor)
  add_compile_definitions(_CRT_SECURE_NO_DEPRECATE NOMINMAX
    _SCL_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
  add_link_options($<$<CONFIG:Release>:/LTCG>)
  if(RELLU_SANITIZER STREQUAL "thread")
    message(FATAL_ERROR "ThreadSanitizer is not available on Windows.")
  elseif(NOT RELLU_SANITIZER STREQUAL "none")
    add_compile_options(/fsanitize=${RELLU_SANITIZER})
  endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Werror -g $<$<CONFIG:Release>:-DNDEBUG>)
  if(${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
    add_compile_options(-fsized-deallocation)
  endif()
  if(NOT RELLU_SANITIZER STREQUAL "none")
    add_compile_options(-fsanitize=${RELLU_SANITIZER} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${RELLU_SANITIZER})
  endif()
endif()
if(CYGWIN)
  add_compile_definitions(__USE_W32_SOCKETS)
endif()
include_directories(${RELLU_INCLUDE_PATH})
include(Config/targets.cmake)
rellu_add_modules("${CMAKE_CURRENT_SOURCE_DIR}/Config")
rellu_configure_clean()
