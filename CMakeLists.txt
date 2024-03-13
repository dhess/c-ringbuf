cmake_minimum_required(VERSION 3.5)
project(
  ringbuf
  VERSION 0.0.1
  LANGUAGES C)

set(CMAKE_C_STANDARD 99)
option(BUILD_SHARED_LIBS "Build using shared libraries" ON)

include(GNUInstallDirs)

add_library(${PROJECT_NAME} ringbuf.c ringbuf.h)
set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER ringbuf.h)
set_target_properties(${PROJECT_NAME} PROPERTIES VERSION "0.0.0")

include(CTest)
if(BUILD_TESTING)
  add_executable(${PROJECT_NAME}-test ringbuf-test.c)
  target_link_libraries(${PROJECT_NAME}-test ${PROJECT_NAME})

  add_test(${PROJECT_NAME}_test ${PROJECT_NAME}-test)
  set_tests_properties(
    ${PROJECT_NAME}_test PROPERTIES PASS_REGULAR_EXPRESSION
                                    "Test [0-9]+\\.\\.\\.pass\\.")
endif()

block()
set(prefix ${CMAKE_INSTALL_PREFIX})
set(exec_prefix ${CMAKE_INSTALL_PREFIX})
set(includedir ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR})
set(libdir ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR})
set(PACKAGE_NAME ${PROJECT_NAME})
set(LIBRINGBUF_PKG_CONFIG_VERSION
    ${${PROJECT_NAME}_VERSION_MAJOR}.${${PROJECT_NAME}_VERSION_MINOR}.${${PROJECT_NAME}_VERSION_PATCH}
)
configure_file("libringbuf.pc.in" "libringbuf.pc" @ONLY)
install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/libringbuf.pc"
  COMPONENT Devel
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
endblock()

install(
  TARGETS ${PROJECT_NAME}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
