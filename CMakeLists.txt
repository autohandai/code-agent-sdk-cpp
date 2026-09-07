cmake_minimum_required(VERSION 3.22)
project(autohand_code_agent_sdk_cpp VERSION 0.1.0 LANGUAGES CXX)

find_package(Threads REQUIRED)

option(AUTOHAND_BUILD_EXAMPLES "Build Autohand C++ SDK examples" ON)
option(AUTOHAND_BUILD_TESTS "Build Autohand C++ SDK tests" ON)

add_library(autohand_sdk
  src/sdk.cpp
)
add_library(autohand::sdk ALIAS autohand_sdk)

target_compile_features(autohand_sdk PUBLIC cxx_std_20)
target_include_directories(autohand_sdk
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(autohand_sdk PRIVATE Threads::Threads)

if(APPLE OR UNIX)
  target_compile_definitions(autohand_sdk PRIVATE AUTOHAND_POSIX=1)
else()
  message(FATAL_ERROR "The initial C++ SDK transport supports POSIX platforms. Windows support needs a CreateProcess transport.")
endif()

if(AUTOHAND_BUILD_EXAMPLES)
  file(GLOB AUTOHAND_EXAMPLES CONFIGURE_DEPENDS examples/*.cpp)
  foreach(example_file IN LISTS AUTOHAND_EXAMPLES)
    get_filename_component(example_stem "${example_file}" NAME_WE)
    string(REPLACE "-" "_" example_target "${example_stem}")
    set(example_target "example_${example_target}")
    add_executable("${example_target}" "${example_file}")
    target_link_libraries("${example_target}" PRIVATE autohand::sdk)
    target_include_directories("${example_target}" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/examples")
  endforeach()
endif()

if(AUTOHAND_BUILD_TESTS)
  enable_testing()
  add_executable(autohand_sdk_tests tests/test_sdk.cpp)
  target_link_libraries(autohand_sdk_tests PRIVATE autohand::sdk)
  if(MSVC)
    target_compile_options(autohand_sdk_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(autohand_sdk_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME autohand_sdk_tests COMMAND autohand_sdk_tests)

  add_executable(autohand_current_cli_tests tests/test_current_cli.cpp)
  target_link_libraries(autohand_current_cli_tests PRIVATE autohand::sdk)
  if(MSVC)
    target_compile_options(autohand_current_cli_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(autohand_current_cli_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME autohand_current_cli_tests COMMAND autohand_current_cli_tests)

  add_executable(autohand_step_control_tests tests/test_step_control.cpp)
  target_link_libraries(autohand_step_control_tests PRIVATE autohand::sdk)
  target_compile_options(autohand_step_control_tests PRIVATE -UNDEBUG)
  add_test(NAME autohand_step_control_tests COMMAND autohand_step_control_tests)

  add_executable(autohand_harness_step_tests tests/test_harness_steps.cpp)
  target_link_libraries(autohand_harness_step_tests PRIVATE autohand::sdk)
  target_compile_options(autohand_harness_step_tests PRIVATE -UNDEBUG)
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME autohand_harness_step_tests
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/harness_steps.py"
              "$<TARGET_FILE:autohand_harness_step_tests>")
  endif()
endif()

install(TARGETS autohand_sdk EXPORT AutohandSdkTargets)
install(DIRECTORY include/ DESTINATION include)
install(EXPORT AutohandSdkTargets NAMESPACE autohand:: DESTINATION lib/cmake/AutohandSdk)
