cmake_minimum_required(VERSION 3.1)
project(
  "vdeplug_wirefilter"
  DESCRIPTION "libvdeplug plugin module for wirefilter"
  HOMEPAGE_URL ""
  VERSION 0.0.1
  LANGUAGES C
)

include(GNUInstallDirs)
include(CheckIncludeFile)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_FORTIFY_SOURCE=2 -O2 -pedantic -Wall -Wextra")

set(CMAKE_REQUIRED_QUIET TRUE)
set(LIBS_REQUIRED vdeplug_mod)
set(HEADERS_REQUIRED libvdeplug.h)
set(THREADS_PREFER_PTHREAD_FLAG ON)

foreach(THISLIB IN LISTS LIBS_REQUIRED)
  find_library(LIB${THISLIB}_OK ${THISLIB})
  if(NOT LIB${THISLIB}_OK)
    message(FATAL_ERROR "library lib${THISLIB} not found")
  endif()
endforeach(THISLIB)
find_package(Threads REQUIRED)

foreach(HEADER IN LISTS HEADERS_REQUIRED)
  check_include_file(${HEADER} ${HEADER}_OK)
  if(NOT ${HEADER}_OK)
    message(FATAL_ERROR "header file ${HEADER} not found")
  endif()
endforeach(HEADER)

add_definitions(-D_GNU_SOURCE)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_library(vdeplug_wirefilter SHARED libvdeplug_wirefilter.c)
target_link_libraries(vdeplug_wirefilter vdeplug_mod Threads::Threads)

install(TARGETS vdeplug_wirefilter DESTINATION ${CMAKE_INSTALL_LIBDIR}/vdeplug)

add_custom_target(uninstall "${CMAKE_COMMAND}" -P "${PROJECT_SOURCE_DIR}/Uninstall.cmake")