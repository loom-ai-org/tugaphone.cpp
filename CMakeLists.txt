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
    find_package(bifonia 0.3.7 CONFIG REQUIRED)
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
# The library looks for its data relative to the installed library, or to the
# executable it is linked into, before the absolute path, so an install made
# with `cmake --install --prefix` into another prefix finds its data.
file(RELATIVE_PATH _tugaphone_data_from_lib "${CMAKE_INSTALL_FULL_LIBDIR}" "${CMAKE_INSTALL_FULL_DATADIR}/tugaphone")
file(RELATIVE_PATH _tugaphone_data_from_bin "${CMAKE_INSTALL_FULL_BINDIR}" "${CMAKE_INSTALL_FULL_DATADIR}/tugaphone")

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
    src/tokenizer.cpp
    src/tugaphone.cpp
    src/plugin.cpp
    src/pyio.cpp
    src/argparse.cpp
    src/cli.cpp
)
add_library(tugaphone::tugaphone ALIAS tugaphone)
target_include_directories(tugaphone
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
# plugin.hpp derives from orthography2ipa::SyllabifierPlugin, so orthography2ipa
# is part of the public interface.
target_link_libraries(tugaphone
    PUBLIC tugalex::tugalex orthography2ipa::orthography2ipa
    PRIVATE bifonia::bifonia silabificador::silabificador
    PRIVATE ZLIB::ZLIB ${CMAKE_DL_LIBS})
target_compile_definitions(tugaphone PRIVATE TUGAPHONE_DEFAULT_DATA_DIR="${_tugaphone_default_data}"
    TUGAPHONE_DATA_FROM_LIBDIR="${_tugaphone_data_from_lib}" TUGAPHONE_DATA_FROM_BINDIR="${_tugaphone_data_from_bin}")
set_target_properties(tugaphone PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tugaphone PRIVATE -Wall -Wextra -Wpedantic)
    # Python rounds each product and each sum. A fused multiply-add rounds once,
    # so a score differs in the last bit on CPUs with FMA (aarch64, x86-64-v3).
    # The flag is on the target, so a parent project that builds tugaphone from
    # source gets it too.
    target_compile_options(tugaphone PRIVATE -ffp-contract=off)
endif()

# The command line, python -m tugaphone.
add_executable(tugaphone_cli src/main.cpp)
set_target_properties(tugaphone_cli PROPERTIES OUTPUT_NAME tugaphone)
target_include_directories(tugaphone_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tugaphone_cli PRIVATE tugaphone)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tugaphone_cli PRIVATE -Wall -Wextra -Wpedantic)
endif()

# scripts/benchmark.py of the Python repository: a tool of this source tree,
# not installed.
add_library(tugaphone_benchmark_core STATIC src/benchmark.cpp)
target_include_directories(tugaphone_benchmark_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tugaphone_benchmark_core PUBLIC tugaphone)
add_executable(tugaphone_benchmark src/benchmark_main.cpp)
target_link_libraries(tugaphone_benchmark PRIVATE tugaphone_benchmark_core)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tugaphone_benchmark_core PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
    target_compile_options(tugaphone_benchmark PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS tugaphone tugaphone_cli EXPORT tugaphoneTargets
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
        tests/parity/package.cpp
        tests/parity/plugin.cpp
        tests/parity/cli.cpp
        tests/parity/benchmark.cpp
        tests/parity/tokenizer.cpp
    )
    target_include_directories(tugaphone_parity PRIVATE tests)
    # The phase-internal headers under src/ expose the private functions the goldens call.
    target_include_directories(tugaphone_parity PRIVATE src)
    find_package(Threads REQUIRED)
    target_link_libraries(tugaphone_parity PRIVATE tugaphone tugaphone_benchmark_core orthography2ipa::orthography2ipa ZLIB::ZLIB Threads::Threads)
    # One parity test per module, so that ctest -j runs the modules in parallel
    # and the timeout applies to each module. The engine writes the tugalex
    # lexicon TSVs to its cache, one cache per test.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS tests/parity/modules.hpp)
    file(READ ${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/modules.hpp _parity_header)
    string(REGEX MATCHALL "X\\([a-z_]+\\)" _parity_modules "${_parity_header}")
    list(TRANSFORM _parity_modules REPLACE "X\\(([a-z_]+)\\)" "\\1")
    # The engine goldens take more than an hour under the sanitizers, so a
    # sanitizer build can check every n-th engine and package record.
    set(TUGAPHONE_LATTICE_CORE_STRIDE 1 CACHE STRING "Check every n-th lattice_core golden record")
    set(TUGAPHONE_PACKAGE_STRIDE 1 CACHE STRING "Check every n-th package golden record")
    set(_parity_tests)
    foreach(_module IN LISTS _parity_modules)
        if(_module STREQUAL "package")
            continue()
        endif()
        set(_stride 1)
        if(_module STREQUAL "lattice_core")
            set(_stride ${TUGAPHONE_LATTICE_CORE_STRIDE})
        endif()
        add_test(NAME parity_${_module} COMMAND tugaphone_parity ${CMAKE_CURRENT_SOURCE_DIR}/tests/goldens
                 --divergences ${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/expected_divergences.tsv
                 --stride ${_stride} ${_module})
        list(APPEND _parity_tests parity_${_module})
    endforeach()
    # The package records run the whole engine five times each, so eight
    # shard tests share them. With a stride s, shard k checks the records whose
    # index modulo 8s is k: every s-th record in all.
    math(EXPR _package_count "8 * ${TUGAPHONE_PACKAGE_STRIDE}")
    foreach(_k RANGE 7)
        add_test(NAME parity_package_${_k} COMMAND tugaphone_parity ${CMAKE_CURRENT_SOURCE_DIR}/tests/goldens
                 --divergences ${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/expected_divergences.tsv
                 --shard ${_k}/${_package_count} package)
        list(APPEND _parity_tests parity_package_${_k})
    endforeach()
    foreach(_test IN LISTS _parity_tests)
        set_tests_properties(${_test} PROPERTIES ENVIRONMENT
            "${_tugaphone_test_env};TUGAPHONE_LEXICON_DIR=${CMAKE_CURRENT_BINARY_DIR}/lexicon-cache-${_test}")
    endforeach()
    # The command-line records run the executable in the source tree, and the
    # benchmark records read the gold fixtures there.
    set_property(TEST parity_cli APPEND PROPERTY ENVIRONMENT "TUGAPHONE_CLI=$<TARGET_FILE:tugaphone_cli>"
                 "TUGAPHONE_CLI_CWD=${CMAKE_CURRENT_SOURCE_DIR}")
    set_property(TEST parity_benchmark APPEND PROPERTY ENVIRONMENT
                 "TUGAPHONE_BENCHMARK_ROOT=${CMAKE_CURRENT_SOURCE_DIR}")
    # The accent records include 100,000 code-point subjects for the regular
    # expression engine; a 1 MB thread stack shows that it does not recurse
    # per character. 1 MB is the stack that PARITY.md states for the engine:
    # 495 nested possessive repeats need up to about 570 KB in a Debug or
    # sanitizer build.
    add_test(NAME parity_small_stack COMMAND tugaphone_parity ${CMAKE_CURRENT_SOURCE_DIR}/tests/goldens
             --stack-kb 1024 accent)
    set_tests_properties(parity_small_stack PROPERTIES ENVIRONMENT
        "${_tugaphone_test_env};TUGAPHONE_LEXICON_DIR=${CMAKE_CURRENT_BINARY_DIR}/lexicon-cache-small-stack")

    add_executable(tugaphone_threads_lattice_core tests/threads_lattice_core.cpp)
    target_link_libraries(tugaphone_threads_lattice_core PRIVATE tugaphone Threads::Threads)
    add_test(NAME threads_lattice_core COMMAND tugaphone_threads_lattice_core)
    set_tests_properties(threads_lattice_core PROPERTIES ENVIRONMENT
        "${_tugaphone_test_env};TUGAPHONE_LEXICON_DIR=${CMAKE_CURRENT_BINARY_DIR}/lexicon-cache-threads")

    add_executable(tugaphone_threads_tokenizer tests/threads_tokenizer.cpp)
    target_link_libraries(tugaphone_threads_tokenizer PRIVATE tugaphone Threads::Threads)
    add_test(NAME threads_tokenizer COMMAND tugaphone_threads_tokenizer)
    set_tests_properties(threads_tokenizer PROPERTIES ENVIRONMENT "${_tugaphone_test_env}")

    add_executable(tugaphone_threads_package tests/threads_package.cpp)
    target_include_directories(tugaphone_threads_package PRIVATE src)
    target_link_libraries(tugaphone_threads_package PRIVATE tugaphone Threads::Threads)
    add_test(NAME threads_package COMMAND tugaphone_threads_package)
    set_tests_properties(threads_package PROPERTIES ENVIRONMENT
        "${_tugaphone_test_env};TUGAPHONE_LEXICON_DIR=${CMAKE_CURRENT_BINARY_DIR}/lexicon-cache-threads-package")

    add_test(NAME cli_process COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_process.sh
             $<TARGET_FILE:tugaphone_cli> $<TARGET_FILE:tugaphone_benchmark> ${CMAKE_CURRENT_SOURCE_DIR}
             ${CMAKE_CURRENT_BINARY_DIR}/cli-process)
    set_tests_properties(cli_process PROPERTIES ENVIRONMENT
        "${_tugaphone_test_env};TUGAPHONE_LEXICON_DIR=${CMAKE_CURRENT_BINARY_DIR}/lexicon-cache-cli-process")

    add_executable(tugaphone_threads_tokenizer_codeswitch tests/threads_tokenizer_codeswitch.cpp)
    target_link_libraries(tugaphone_threads_tokenizer_codeswitch PRIVATE tugaphone Threads::Threads)
    add_test(NAME threads_tokenizer_codeswitch COMMAND tugaphone_threads_tokenizer_codeswitch)
    set_tests_properties(threads_tokenizer_codeswitch PROPERTIES ENVIRONMENT "${_tugaphone_test_env}")

    # Installs into prefixes that are not CMAKE_INSTALL_PREFIX and runs the
    # installed command line with none of the data variables set. The builds
    # the test makes start from the settings of this build, through an initial
    # cache, so that they find the same dependencies the same way.
    set(_relocated_cache "${CMAKE_CURRENT_BINARY_DIR}/relocated-install-cache.cmake")
    set(_relocated_settings "")
    foreach(_name CMAKE_CXX_COMPILER CMAKE_CXX_FLAGS CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS
                  CMAKE_BUILD_TYPE CMAKE_INSTALL_BINDIR CMAKE_INSTALL_DATADIR CMAKE_PREFIX_PATH
                  TUGAPHONE_USE_SYSTEM_DEPS ZLIB_LIBRARY_RELEASE ZLIB_LIBRARY_DEBUG ZLIB_INCLUDE_DIR
                  Boost_INCLUDE_DIR Boost_DIR BOOST_ROOT BOOST_INCLUDEDIR ICU_ROOT ICU_INCLUDE_DIR
                  ICU_UC_LIBRARY_RELEASE ICU_UC_LIBRARY_DEBUG ICU_DATA_LIBRARY_RELEASE ICU_DATA_LIBRARY_DEBUG)
        if(DEFINED ${_name} AND NOT "${${_name}}" MATCHES "-NOTFOUND$")
            string(APPEND _relocated_settings "set(${_name} [==[${${_name}}]==] CACHE STRING \"\")\n")
        endif()
    endforeach()
    if(TUGAPHONE_USE_SYSTEM_DEPS)
        foreach(_dep silabificador tugalex bifonia orthography2ipa)
            if(${_dep}_DIR)
                string(APPEND _relocated_settings "set(${_dep}_DIR [==[${${_dep}_DIR}]==] CACHE PATH \"\")\n")
            endif()
        endforeach()
    endif()
    file(WRITE "${_relocated_cache}" "${_relocated_settings}")
    add_test(NAME relocated_install
             COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR} -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                     -DPREFIX=${CMAKE_CURRENT_BINARY_DIR}/relocated-install -DINSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
                     -DBINDIR=${CMAKE_INSTALL_BINDIR} -DDATADIR=${CMAKE_INSTALL_DATADIR} "-DGENERATOR=${CMAKE_GENERATOR}"
                     -DINITIAL_CACHE=${_relocated_cache}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/relocated_install.cmake)
    # The test builds the library twice more, a static and a shared build.
    set_tests_properties(relocated_install PROPERTIES TIMEOUT 3600)

    add_executable(tugaphone_examples tests/examples.cpp)
    target_link_libraries(tugaphone_examples PRIVATE tugaphone)
    add_test(NAME examples COMMAND tugaphone_examples)
    set_tests_properties(examples PROPERTIES ENVIRONMENT "${_tugaphone_test_env}")
endif()
