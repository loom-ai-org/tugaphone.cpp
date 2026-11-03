cmake_minimum_required(VERSION 3.16)

project(tugaphone VERSION 1.4.16 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(_tugaphone_top ON)
else()
    set(_tugaphone_top OFF)
endif()
option(TUGAPHONE_BUILD_TESTS "Build the parity and unit tests" ${_tugaphone_top})
option(TUGAPHONE_USE_SYSTEM_DEPS
       "Find silabificador, tugalex, bifonia and orthography2ipa with find_package instead of building third_party/"
       OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# ── Dependencies ─────────────────────────────────────────────────────────────
# The four C++ ports tugaphone uses. By default each one builds from its pinned
# submodule; with TUGAPHONE_USE_SYSTEM_DEPS they come from installed packages.
if(TUGAPHONE_USE_SYSTEM_DEPS)
    find_package(silabificador 2.1.1 CONFIG REQUIRED)
    find_package(tugalex 2.2.2 CONFIG REQUIRED)
    find_package(bifonia 0.2.0 CONFIG REQUIRED)
    find_package(orthography2ipa CONFIG REQUIRED)
else()
    foreach(_dep silabificador tugalex bifonia orthography2ipa)
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/${_dep}.cpp/CMakeLists.txt")
            message(FATAL_ERROR "third_party/${_dep}.cpp is empty: run `git submodule update --init`, "
                                "or configure with -DTUGAPHONE_USE_SYSTEM_DEPS=ON")
        endif()
    endforeach()
    add_subdirectory(third_party/silabificador.cpp)
    add_subdirectory(third_party/tugalex.cpp)
    add_subdirectory(third_party/bifonia.cpp)

    # orthography2ipa looks for silabificador with find_package. A config file
    # that names the target built above lets it find this build tree's copy.
    get_directory_property(_silabificador_version DIRECTORY third_party/silabificador.cpp
                           DEFINITION silabificador_VERSION)
    set(_deps_dir "${CMAKE_CURRENT_BINARY_DIR}/third_party-config")
    file(WRITE "${_deps_dir}/silabificadorConfig.cmake"
         "# The silabificador::silabificador target comes from third_party/silabificador.cpp.\n")
    write_basic_package_version_file("${_deps_dir}/silabificadorConfigVersion.cmake"
                                     VERSION ${_silabificador_version} COMPATIBILITY SameMinorVersion)
    set(silabificador_DIR "${_deps_dir}" CACHE PATH "silabificador package configuration" FORCE)

    # orthography2ipa registers its own tests through include(CTest); they need
    # the Python reference beside it, so they stay out of this build.
    set(_tugaphone_build_testing "${BUILD_TESTING}")
    set(BUILD_TESTING OFF)
    add_subdirectory(third_party/orthography2ipa.cpp)
    set(BUILD_TESTING "${_tugaphone_build_testing}")
    if(NOT TARGET orthography2ipa::orthography2ipa)
        add_library(orthography2ipa::orthography2ipa ALIAS orthography2ipa)
    endif()
endif()

# The langdetect models are gzip files.
find_package(ZLIB REQUIRED)

# ── Library ──────────────────────────────────────────────────────────────────
# The data files are installed here, and the library looks here unless
# set_data_dir() or TUGAPHONE_DATA_DIR names another directory.
set(TUGAPHONE_DATA_INSTALL_DIR "${CMAKE_INSTALL_DATADIR}/tugaphone")
if(IS_ABSOLUTE "${TUGAPHONE_DATA_INSTALL_DIR}")
    set(_tugaphone_default_data "${TUGAPHONE_DATA_INSTALL_DIR}")
else()
    set(_tugaphone_default_data "${CMAKE_INSTALL_PREFIX}/${TUGAPHONE_DATA_INSTALL_DIR}")
endif()

add_library(tugaphone
    src/unicode.cpp
    src/pynum.cpp
    src/json.cpp
    src/data.cpp
    src/pronounce_pt.cpp
    src/number_utils.cpp
    src/text_normalization.cpp
    src/registry.cpp
    src/dialects.cpp
    src/unicode_norm.cpp
    src/o2i_registry.cpp
    src/pyre.cpp
    src/markov.cpp
    src/langdetect.cpp
    src/codeswitch.cpp
    src/accent.cpp
    src/lattice_core.cpp
)
add_library(tugaphone::tugaphone ALIAS tugaphone)
target_include_directories(tugaphone
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(tugaphone
    PUBLIC tugalex::tugalex
    PRIVATE orthography2ipa::orthography2ipa bifonia::bifonia silabificador::silabificador
    PRIVATE ZLIB::ZLIB)
target_compile_definitions(tugaphone PRIVATE TUGAPHONE_DEFAULT_DATA_DIR="${_tugaphone_default_data}")
set_target_properties(tugaphone PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tugaphone PRIVATE -Wall -Wextra -Wpedantic)
    # Python rounds each product and each sum. A fused multiply-add rounds once,
    # so a score differs in the last bit on CPUs with FMA (aarch64, x86-64-v3).
    # The flag is on the target, so a parent project that builds tugaphone from
    # source gets it too.
    target_compile_options(tugaphone PRIVATE -ffp-contract=off)
endif()

install(TARGETS tugaphone EXPORT tugaphoneTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY data/ DESTINATION ${TUGAPHONE_DATA_INSTALL_DIR})
install(EXPORT tugaphoneTargets
    NAMESPACE tugaphone::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tugaphone)
configure_package_config_file(cmake/tugaphoneConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/tugaphoneConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tugaphone)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/tugaphoneConfigVersion.cmake
    COMPATIBILITY SameMinorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/tugaphoneConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/tugaphoneConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tugaphone)

# ── Tests ────────────────────────────────────────────────────────────────────
if(TUGAPHONE_BUILD_TESTS)
    enable_testing()
    find_package(ZLIB REQUIRED)

    # The library, the tugalex lexicon and the bifonia homographs read their
    # data from the source trees.
    if(TUGAPHONE_USE_SYSTEM_DEPS)
        set(_tugaphone_test_env "TUGAPHONE_DATA_DIR=${CMAKE_CURRENT_SOURCE_DIR}/data")
    else()
        set(_tugaphone_test_env
            "TUGAPHONE_DATA_DIR=${CMAKE_CURRENT_SOURCE_DIR}/data"
            "TUGALEX_DATA_DIR=${CMAKE_CURRENT_SOURCE_DIR}/third_party/tugalex.cpp/data"
            "BIFONIA_DATA_DIR=${CMAKE_CURRENT_SOURCE_DIR}/third_party/bifonia.cpp/data")
    endif()

    add_executable(tugaphone_parity
        tests/parity.cpp
        tests/parity/registry.cpp
        tests/parity/dialects.cpp
        tests/parity/text_normalization.cpp
        tests/parity/number_utils.cpp
        tests/parity/version.cpp
        tests/parity/markov.cpp
        tests/parity/langdetect.cpp
        tests/parity/codeswitch.cpp
        tests/parity/accent.cpp
        tests/parity/lattice_core.cpp
    )
    target_include_directories(tugaphone_parity PRIVATE tests)
    # The phase-internal headers under src/ expose the private functions the goldens call.
    target_include_directories(tugaphone_parity PRIVATE src)
    find_package(Threads REQUIRED)
    target_link_libraries(tugaphone_parity PRIVATE tugaphone orthography2ipa::orthography2ipa ZLIB::ZLIB Threads::Threads)
    # One parity test per module, so that ctest -j runs the modules in parallel
    # and the timeout applies to each module. The engine writes the tugalex
    # lexicon TSVs to its cache, one cache per test.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS tests/parity/modules.hpp)
    file(READ ${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/modules.hpp _parity_header)
    string(REGEX MATCHALL "X\\([a-z_]+\\)" _parity_modules "${_parity_header}")
    list(TRANSFORM _parity_modules REPLACE "X\\(([a-z_]+)\\)" "\\1")
    # The engine goldens take more than an hour under the sanitizers, so a
    # sanitizer build can check every n-th engine record.
    set(TUGAPHONE_LATTICE_CORE_STRIDE 1 CACHE STRING "Check every n-th lattice_core golden record")
    foreach(_module IN LISTS _parity_modules)
        set(_stride 1)
        if(_module STREQUAL "lattice_core")
            set(_stride ${TUGAPHONE_LATTICE_CORE_STRIDE})
        endif()
        add_test(NAME parity_${_module} COMMAND tugaphone_parity ${CMAKE_CURRENT_SOURCE_DIR}/tests/goldens
                 --divergences ${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/expected_divergences.tsv
                 --stride ${_stride} ${_module})
        set_tests_properties(parity_${_module} PROPERTIES ENVIRONMENT
            "${_tugaphone_test_env};TUGAPHONE_LEXICON_DIR=${CMAKE_CURRENT_BINARY_DIR}/lexicon-cache-${_module}")
    endforeach()
    # The accent records include 100,000 code-point subjects for the regular
    # expression engine; a 512 KB thread stack shows that it does not recurse
    # per character.
    add_test(NAME parity_small_stack COMMAND tugaphone_parity ${CMAKE_CURRENT_SOURCE_DIR}/tests/goldens
             --stack-kb 512 accent)
    set_tests_properties(parity_small_stack PROPERTIES ENVIRONMENT
        "${_tugaphone_test_env};TUGAPHONE_LEXICON_DIR=${CMAKE_CURRENT_BINARY_DIR}/lexicon-cache-small-stack")

    add_executable(tugaphone_threads_lattice_core tests/threads_lattice_core.cpp)
    target_link_libraries(tugaphone_threads_lattice_core PRIVATE tugaphone Threads::Threads)
    add_test(NAME threads_lattice_core COMMAND tugaphone_threads_lattice_core)
    set_tests_properties(threads_lattice_core PROPERTIES ENVIRONMENT
        "${_tugaphone_test_env};TUGAPHONE_LEXICON_DIR=${CMAKE_CURRENT_BINARY_DIR}/lexicon-cache-threads")

    add_executable(tugaphone_examples tests/examples.cpp)
    target_link_libraries(tugaphone_examples PRIVATE tugaphone)
    add_test(NAME examples COMMAND tugaphone_examples)
    set_tests_properties(examples PROPERTIES ENVIRONMENT "${_tugaphone_test_env}")
endif()
