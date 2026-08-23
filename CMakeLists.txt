cmake_minimum_required(VERSION 3.20)

set(CMLOC "CMakeLists: ")
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)

# OpenCPN/Frontend2 package identity.
set(VERBOSE_NAME "OfflineTides")
set(COMMON_NAME "OfflineTides")
set(PACKAGE "offlinetides")
set(PACKAGE_CONTACT "Paul (pob220)")
set(SHORT_DESCRIPTION
    "Offline tidal-height forecasts from authenticated .xtdt packages.")
set(LONG_DESCRIPTION
    "OfflineTides predicts offline water-level height at the chart cursor, an OpenCPN waypoint or mark, or manual WGS84 coordinates. It displays datum-labelled rolling or selected-date curves, interpolated high and low water events, local data quality and daylight-saving-aware display times while retaining UTC internally.")

set(VERSION_MAJOR "1")
set(VERSION_MINOR "0")
set(VERSION_PATCH "2")
set(VERSION_TWEAK "0")
set(VERSION_DATE "22/08/2026")
set(OCPN_MIN_VERSION "ov514")

set(OCPN_API_VERSION_MAJOR "1")
set(OCPN_API_VERSION_MINOR "21")
set(TP_COMMENT "  * OfflineTides Alpha developer candidate")
set(PARENT "opencpn")

set(PROD "prod")
set(BETA "beta")
set(ALPHA "alpha")
set(CLOUDSMITH_USER "pob220")
set(CLOUDSMITH_BASE_REPOSITORY "offlinetides")
set(PLUGIN_GIT_REPOSITORY "pob220/offlinetides_pi")
set(XML_INFO_URL "https://github.com/pob220/offlinetides_pi")
set(XML_SUMMARY "${SHORT_DESCRIPTION}")
set(XML_DESCRIPTION "${LONG_DESCRIPTION}")

option(PLUGIN_USE_SVG "Use SVG resources" OFF)
option(USE_GL "Enable OpenGL support" OFF)
option(BUILD_GTK3 "Build against GTK3" ON)
option(OCPN_BUILD_TEST "Build OfflineTides tests and authoring tools" OFF)
option(XTIDAL_STANDALONE_API
       "Build against the pinned OpenCPN API submodule" OFF)
option(XTIDAL_BUILD_AUTHORING_TOOLS
       "Build offline harmonic authoring tools" "${OCPN_BUILD_TEST}")
option(OFFLINETIDES_STATIC_RUNTIME_DEPS
       "Statically link the small OfflineTides runtime dependencies" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.>)
if(MSVC)
  add_compile_options(/Zc:__cplusplus)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "")
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

project(${PACKAGE} LANGUAGES CXX)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(PluginSetup)
include(PluginConfigure)

set(XTIDAL_ENGINE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/environmental-grib-generator"
    CACHE PATH "Path to the environmental GRIB/XTD engine repository")
if(NOT EXISTS "${XTIDAL_ENGINE_DIR}/include/environmental_grib/xtd_package.h")
  message(FATAL_ERROR
          "Missing environmental-grib-generator submodule. Run: git submodule update --init --recursive")
endif()

if(WIN32)
  find_package(jsoncpp CONFIG REQUIRED)
  find_package(unofficial-sodium CONFIG REQUIRED)
  find_package(zstd CONFIG REQUIRED)
  set(XTIDAL_JSONCPP_TARGET JsonCpp::JsonCpp)
  set(XTIDAL_SODIUM_TARGET unofficial-sodium::sodium)
  set(XTIDAL_ZSTD_TARGET zstd::libzstd)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(JSONCPP REQUIRED IMPORTED_TARGET jsoncpp)
  pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
  pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)
  set(XTIDAL_JSONCPP_TARGET PkgConfig::JSONCPP)
  set(XTIDAL_SODIUM_TARGET PkgConfig::SODIUM)
  set(XTIDAL_ZSTD_TARGET PkgConfig::ZSTD)
endif()
if(XTIDAL_BUILD_AUTHORING_TOOLS)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(NETCDF REQUIRED IMPORTED_TARGET netcdf)
endif()

if(OFFLINETIDES_STATIC_RUNTIME_DEPS AND NOT WIN32)
  function(xtidal_import_static target prefix)
    set(names ${ARGN})
    set(saved_suffixes ${CMAKE_FIND_LIBRARY_SUFFIXES})
    if(WIN32)
      set(CMAKE_FIND_LIBRARY_SUFFIXES .lib)
    else()
      set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
    endif()
    find_library(static_library NAMES ${names}
      HINTS ${${prefix}_LIBRARY_DIRS} REQUIRED)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${saved_suffixes})
    add_library(${target} UNKNOWN IMPORTED)
    set_target_properties(${target} PROPERTIES
      IMPORTED_LOCATION "${static_library}"
      INTERFACE_INCLUDE_DIRECTORIES "${${prefix}_INCLUDE_DIRS}"
      INTERFACE_LINK_OPTIONS "${${prefix}_STATIC_LDFLAGS_OTHER}")
    unset(static_library CACHE)
  endfunction()

  xtidal_import_static(OfflineTidesJsonCpp JSONCPP jsoncpp)
  xtidal_import_static(OfflineTidesSodium SODIUM sodium libsodium)
  xtidal_import_static(OfflineTidesZstd ZSTD zstd zstd_static libzstd)
  if(NOT WIN32)
    find_package(Threads REQUIRED)
    set_property(TARGET OfflineTidesSodium APPEND PROPERTY
      INTERFACE_LINK_LIBRARIES Threads::Threads)
  endif()
  set(XTIDAL_JSONCPP_TARGET OfflineTidesJsonCpp)
  set(XTIDAL_SODIUM_TARGET OfflineTidesSodium)
  set(XTIDAL_ZSTD_TARGET OfflineTidesZstd)
endif()

# Runtime-only XTD reader. Raw model import, network providers and GRIB
# generation are deliberately excluded from the distributed plugin binary.
add_library(xtidal_xtd_runtime STATIC
  ${XTIDAL_ENGINE_DIR}/src/geo.cpp
  ${XTIDAL_ENGINE_DIR}/src/model.cpp
  ${XTIDAL_ENGINE_DIR}/src/random_access.cpp
  ${XTIDAL_ENGINE_DIR}/src/tpxo.cpp
  ${XTIDAL_ENGINE_DIR}/src/xtd.cpp
  ${XTIDAL_ENGINE_DIR}/src/xtd_package.cpp)
set_target_properties(xtidal_xtd_runtime PROPERTIES
  POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(xtidal_xtd_runtime PRIVATE
  ENVIRONMENTAL_XTD_RUNTIME_ONLY=1)
target_include_directories(xtidal_xtd_runtime PUBLIC
  ${XTIDAL_ENGINE_DIR}/include)
target_link_libraries(xtidal_xtd_runtime PUBLIC
  ${XTIDAL_JSONCPP_TARGET} ${XTIDAL_SODIUM_TARGET} ${XTIDAL_ZSTD_TARGET})
target_compile_options(xtidal_xtd_runtime PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wno-missing-field-initializers>)

add_library(xtidal_prediction STATIC
  src/forecast_export.cpp
  src/native_tide_comparison.cpp
  src/time_zone_support.cpp
  src/xtidal_prediction.cpp)
set_target_properties(xtidal_prediction PROPERTIES
  POSITION_INDEPENDENT_CODE ON)
target_include_directories(xtidal_prediction PUBLIC src)
target_link_libraries(xtidal_prediction PUBLIC
  xtidal_xtd_runtime ${XTIDAL_JSONCPP_TARGET})

if(XTIDAL_BUILD_AUTHORING_TOOLS)
  add_library(xtidal_height_authoring STATIC
    src/eot20_model.cpp
    src/global_height_mosaic.cpp
    src/height_assimilation.cpp
    src/height_correction.cpp
    src/station_harmonics.cpp
    src/ticon_stations.cpp
    src/vertical_datum.cpp)
  set_target_properties(xtidal_height_authoring PROPERTIES
    POSITION_INDEPENDENT_CODE ON)
  target_include_directories(xtidal_height_authoring PUBLIC src)
  target_link_libraries(xtidal_height_authoring PUBLIC
    xtidal_xtd_runtime PkgConfig::NETCDF)
  target_compile_options(xtidal_height_authoring PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>)
endif()

target_sources(${PACKAGE_NAME} PRIVATE
  src/native_tide_host.cpp
  src/native_tide_host.h
  src/xtidal_dialog.cpp
  src/xtidal_dialog.h
  src/xtidal_pi.cpp
  src/xtidal_pi.h)
target_include_directories(${PACKAGE_NAME} PRIVATE src)
target_link_libraries(${PACKAGE_NAME} xtidal_prediction ${CMAKE_DL_LIBS})
target_compile_options(${PACKAGE_NAME} PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>)

if(TARGET ocpn::opencpn)
  target_link_libraries(${PACKAGE_NAME} ocpn::opencpn)
elseif(TARGET ocpn::api)
  target_link_libraries(${PACKAGE_NAME} ocpn::api)
elseif(XTIDAL_STANDALONE_API AND
       EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencpn-libs/api-${OCPN_API_VERSION_MINOR}/CMakeLists.txt")
  add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencpn-libs/api-${OCPN_API_VERSION_MINOR}"
    "${CMAKE_CURRENT_BINARY_DIR}/opencpn-api")
  target_link_libraries(${PACKAGE_NAME} ocpn::api)
elseif(DEFINED OCPN_ROOT AND EXISTS "${OCPN_ROOT}/include/ocpn_plugin.h")
  target_include_directories(${PACKAGE_NAME} PRIVATE "${OCPN_ROOT}/include")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/include/ocpn_plugin.h")
  target_include_directories(${PACKAGE_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/include")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencpn-libs/api-${OCPN_API_VERSION_MINOR}/CMakeLists.txt")
  add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencpn-libs/api-${OCPN_API_VERSION_MINOR}"
    "${CMAKE_CURRENT_BINARY_DIR}/opencpn-api")
  target_link_libraries(${PACKAGE_NAME} ocpn::api)
else()
  message(FATAL_ERROR
          "Missing OpenCPN API headers. Initialise submodules or set OCPN_ROOT.")
endif()

if(OCPN_BUILD_TEST)
  enable_testing()
  if(NOT TARGET xtidal_height_authoring)
    message(FATAL_ERROR "OCPN_BUILD_TEST requires XTIDAL_BUILD_AUTHORING_TOOLS")
  endif()
  find_package(Python3 COMPONENTS Interpreter REQUIRED)

  add_executable(xtidal_tests
    tests/xtidal_tests.cpp
    ${XTIDAL_ENGINE_DIR}/tests/xtd_test_support.cpp)
  target_include_directories(xtidal_tests PRIVATE
    src ${XTIDAL_ENGINE_DIR}/tests)
  target_compile_definitions(xtidal_tests PRIVATE
    XTIDAL_VALIDATION_LOCATIONS="${CMAKE_CURRENT_SOURCE_DIR}/tests/validation_locations.json"
    XTIDAL_LIVERPOOL_REFERENCE="${CMAKE_CURRENT_SOURCE_DIR}/tests/reference/liverpool-2026-01-01-published.json"
    XTIDAL_CORRECTION_CATALOGUE="${CMAKE_CURRENT_SOURCE_DIR}/tests/corrections/uk-chart-datum-v1.json"
    XTIDAL_CORRECTION_CATALOGUE_V2="${CMAKE_CURRENT_SOURCE_DIR}/tests/corrections/uk-chart-datum-v2.json"
    XTIDAL_DOVER_REFERENCE="${CMAKE_CURRENT_SOURCE_DIR}/tests/reference/dover-2026-08-21-ntslf.json"
    XTIDAL_LOCAL_HOLYHEAD_REFERENCE="${CMAKE_CURRENT_SOURCE_DIR}/tests/reference/holyhead-2026-08-21-ntslf.json"
    XTIDAL_MULTIDAY_DOVER_REFERENCE="${CMAKE_CURRENT_SOURCE_DIR}/tests/reference/dover-2026-08-21-ntslf-28d.json"
    XTIDAL_TICON3_NORMALIZED_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ticon3-normalized-sample.json")
  target_compile_definitions(xtidal_tests PRIVATE
    XTIDAL_CHART_DATUM_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/chart-datum-stations-sample.json")
  target_link_libraries(xtidal_tests PRIVATE
    xtidal_prediction xtidal_height_authoring)
  add_test(NAME xtidal_tests COMMAND xtidal_tests)

  add_test(NAME xtidal_global_canary_manifests
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_global_canary_manifests.py
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/global_canaries.json
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/reference)
  add_test(NAME xtidal_ticon3_import
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ticon3_import.py
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/import_ticon3.py
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ticon3-sample.tsv)
  add_test(NAME xtidal_chart_datum_overrides
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_chart_datum_overrides.py
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/apply_chart_datum_overrides.py
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/chart-datum-stations-sample.json
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/corrections/global-chart-datum-v09.json)
  add_test(NAME xtidal_interchange_fixtures
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_interchange_fixtures.py
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/s104-mapping-water-level-v1.json)

  function(xtidal_add_tool target)
    add_executable(${target} ${ARGN})
    target_include_directories(${target} PRIVATE
      src ${XTIDAL_ENGINE_DIR}/tests)
  endfunction()

  xtidal_add_tool(xtidal_make_fixture tests/make_fixture.cpp
    ${XTIDAL_ENGINE_DIR}/tests/xtd_test_support.cpp)
  target_link_libraries(xtidal_make_fixture PRIVATE xtidal_xtd_runtime)
  xtidal_add_tool(xtidal_validate_height tests/validate_height.cpp)
  target_link_libraries(xtidal_validate_height PRIVATE
    xtidal_prediction ${XTIDAL_JSONCPP_TARGET})
  xtidal_add_tool(xtidal_build_station_xtd tests/build_station_xtd.cpp
    ${XTIDAL_ENGINE_DIR}/tests/xtd_test_support.cpp)
  target_link_libraries(xtidal_build_station_xtd PRIVATE
    xtidal_prediction ${XTIDAL_JSONCPP_TARGET})
  xtidal_add_tool(xtidal_build_corrected_xtd tests/build_corrected_xtd.cpp
    ${XTIDAL_ENGINE_DIR}/tests/xtd_test_support.cpp)
  target_link_libraries(xtidal_build_corrected_xtd PRIVATE
    xtidal_height_authoring ${XTIDAL_JSONCPP_TARGET})
  xtidal_add_tool(xtidal_calibrate_gains tests/calibrate_station_gains.cpp)
  target_link_libraries(xtidal_calibrate_gains PRIVATE
    xtidal_prediction xtidal_height_authoring ${XTIDAL_JSONCPP_TARGET})
  xtidal_add_tool(xtidal_build_ticon_xtdt tests/build_ticon_xtdt.cpp
    ${XTIDAL_ENGINE_DIR}/tests/xtd_test_support.cpp)
  target_link_libraries(xtidal_build_ticon_xtdt PRIVATE
    xtidal_height_authoring ${XTIDAL_JSONCPP_TARGET})
  xtidal_add_tool(xtidal_inspect_height_point tests/inspect_height_point.cpp)
  target_link_libraries(xtidal_inspect_height_point PRIVATE
    xtidal_xtd_runtime ${XTIDAL_JSONCPP_TARGET})
  xtidal_add_tool(xtidal_verify_data_release tests/verify_data_release.cpp)
  target_link_libraries(xtidal_verify_data_release PRIVATE
    xtidal_xtd_runtime ${XTIDAL_JSONCPP_TARGET})
  xtidal_add_tool(xtidal_build_ticon_station_xtdt
    tests/build_ticon_station_xtdt.cpp
    ${XTIDAL_ENGINE_DIR}/tests/xtd_test_support.cpp)
  target_link_libraries(xtidal_build_ticon_station_xtdt PRIVATE
    xtidal_height_authoring)
  xtidal_add_tool(xtidal_evaluate_station_harmonics
    tests/evaluate_station_harmonics.cpp)
  target_link_libraries(xtidal_evaluate_station_harmonics PRIVATE
    xtidal_height_authoring ${XTIDAL_JSONCPP_TARGET})
  xtidal_add_tool(xtidal_model_height_author tests/eot20_height_author.cpp
    ${XTIDAL_ENGINE_DIR}/tests/xtd_test_support.cpp)
  target_link_libraries(xtidal_model_height_author PRIVATE
    xtidal_height_authoring ${XTIDAL_JSONCPP_TARGET})
endif()

include(PluginInstall)
include(PluginLocalization)
include(PluginPackage)
