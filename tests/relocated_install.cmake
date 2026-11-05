# Installs tugaphone into prefixes that are not the CMAKE_INSTALL_PREFIX of the
# build and runs the installed command line with no data variables. The
# library, the tugalex lexicon and the bifonia tables must all come from the
# prefix of the command line. The orthography2ipa specs come from its source
# tree when that tree is on disk, as orthography2ipa.cpp reads it first, so
# this test does not check where they come from:
#   1. this build, run by absolute path and by bare name through PATH;
#   2. a static build with a two-level CMAKE_INSTALL_LIBDIR, with the data of
#      another install in <prefix>/../share/tugaphone;
#   3. a shared build with a two-level CMAKE_INSTALL_LIBDIR, the command line
#      copied outside the prefix.
# Each prefix gets its own letter_expressions.json edited to list "capítulo
# XII" as a letter expression, so the command line that reads it leaves the
# numeral as a letter name. Every other copy of the data stays unedited: the
# source tree, the working directory, the decoy of case 2, the configured
# prefix of cases 2 and 3, and <prefix>/lib/share of case 3. A command line
# that reads any of them prints the numeral, and the test fails. The expected
# line is the output of the Python tugaphone of PARITY.md for the same
# arguments and data.
if(PREFIX STREQUAL INSTALL_PREFIX)
    message(FATAL_ERROR "PREFIX must differ from CMAKE_INSTALL_PREFIX: ${PREFIX}")
endif()
file(REMOVE_RECURSE "${PREFIX}" "${PREFIX}-lexicon-cache" "${PREFIX}-layouts" "${PREFIX}-cwd")
# Each case runs from a directory that holds an unedited copy of the data at
# share/tugaphone, so a lookup relative to the working directory prints the
# numeral.
file(COPY "${SOURCE_DIR}/data/" DESTINATION "${PREFIX}-cwd/${DATADIR}/tugaphone")

function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        list(JOIN ARGN " " command)
        message(FATAL_ERROR "${command}: ${result}")
    endif()
endfunction()

set(expected "ˈtɛɲu ˈvĩt i ˈtɾez ˈɐnuʃ sɨˈɲoɾ ˈsiɫvɐ ˈli o kɐˈpitulu ʃiˈi\n")

# Lists "capítulo XII" as a letter expression in the data directory *dir*.
# "Cap." expands to "Capítulo" before the numeral rule reads the word before
# "XII".
function(edit_expressions dir)
    file(READ "${dir}/letter_expressions.json" expressions)
    string(REPLACE "\"expressions\": {" "\"expressions\": {\"capítulo XII\": \"\"," edited "${expressions}")
    if(edited STREQUAL expressions)
        message(FATAL_ERROR "${dir}/letter_expressions.json has no expressions object")
    endif()
    file(WRITE "${dir}/letter_expressions.json" "${edited}")
endfunction()
# The command line to run, then the variables to add to its environment.
function(phonemize program)
    file(REMOVE_RECURSE "${PREFIX}-lexicon-cache")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env --unset=TUGAPHONE_DATA_DIR --unset=TUGALEX_DATA_DIR --unset=BIFONIA_DATA_DIR
                --unset=ORTHOGRAPHY2IPA_DATA_DIR "TUGAPHONE_LEXICON_DIR=${PREFIX}-lexicon-cache" ${ARGN}
                "${program}" phonemize --lect pt-PT "Tenho 23 anos, Sr. Silva; li o Cap. XII."
        WORKING_DIRECTORY "${PREFIX}-cwd"
        OUTPUT_VARIABLE output ERROR_VARIABLE error RESULT_VARIABLE result)
    if(NOT result EQUAL 0 OR NOT output STREQUAL expected)
        message(FATAL_ERROR "installed tugaphone (${program}): exit ${result}\nstdout: ${output}\nstderr: ${error}")
    endif()
endfunction()

run("${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${PREFIX}")
edit_expressions("${PREFIX}/${DATADIR}/tugaphone")
phonemize("${PREFIX}/${BINDIR}/tugaphone" "PATH=${PREFIX}/${BINDIR}:$ENV{PATH}")
# The bare name goes through PATH, from a directory that holds no file of
# that name, as a shell does.
phonemize(tugaphone "PATH=${PREFIX}/${BINDIR}:$ENV{PATH}")

set(layouts "${PREFIX}-layouts")
set(libdir "lib/x86_64-linux-gnu")
cmake_host_system_information(RESULT jobs QUERY NUMBER_OF_PHYSICAL_CORES)
if(DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL})
    set(jobs "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
endif()
foreach(shared OFF ON)
    set(build "${layouts}/build-${shared}")
    run("${CMAKE_COMMAND}" -C "${INITIAL_CACHE}" -S "${SOURCE_DIR}" -B "${build}" -G "${GENERATOR}"
        -DTUGAPHONE_BUILD_TESTS=OFF "-DBUILD_SHARED_LIBS=${shared}" "-DCMAKE_INSTALL_LIBDIR=${libdir}"
        "-DCMAKE_INSTALL_PREFIX=${layouts}/configured")
    run("${CMAKE_COMMAND}" --build "${build}" --parallel "${jobs}")
endforeach()
# The configured prefix holds only an unedited copy of the tugaphone data, so
# a library that falls back to it prints the numeral, and a dependency that
# falls back to it finds no data.
file(COPY "${SOURCE_DIR}/data/" DESTINATION "${layouts}/configured/${DATADIR}/tugaphone")

set(static "${layouts}/static/usr/local")
run("${CMAKE_COMMAND}" --install "${layouts}/build-OFF" --prefix "${static}")
file(COPY "${static}/${DATADIR}/tugaphone" DESTINATION "${layouts}/static/usr/${DATADIR}")
edit_expressions("${static}/${DATADIR}/tugaphone")
phonemize("${static}/${BINDIR}/tugaphone")

set(shared "${layouts}/shared")
run("${CMAKE_COMMAND}" --install "${layouts}/build-ON" --prefix "${shared}")
# The path from BINDIR applied to the library dir, <prefix>/lib/share, holds an
# unedited copy.
file(COPY "${SOURCE_DIR}/data/" DESTINATION "${shared}/lib/${DATADIR}/tugaphone")
edit_expressions("${shared}/${DATADIR}/tugaphone")
file(COPY "${shared}/${BINDIR}/tugaphone" DESTINATION "${layouts}/outside")
# Prepended, so that a build with shared system dependencies still finds them.
set(library_paths "")
foreach(variable LD_LIBRARY_PATH DYLD_LIBRARY_PATH)
    set(path "${shared}/${libdir}")
    if(DEFINED ENV{${variable}} AND NOT "$ENV{${variable}}" STREQUAL "")
        string(APPEND path ":$ENV{${variable}}")
    endif()
    list(APPEND library_paths "${variable}=${path}")
endforeach()
phonemize("${layouts}/outside/tugaphone" ${library_paths})
