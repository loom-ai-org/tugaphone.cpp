# Installs tugaphone into prefixes that are not the CMAKE_INSTALL_PREFIX of the
# build and runs the installed command line with no data variables. The
# library, the tugalex lexicon, the bifonia tables and the orthography2ipa
# specs must all come from the prefix of the command line:
#   1. this build, run by absolute path and by bare name through PATH;
#   2. a static build with a two-level CMAKE_INSTALL_LIBDIR, with the data of
#      another install in <prefix>/../share/tugaphone. That copy lists
#      "cap XII" as a letter expression, so a command line that reads it
#      leaves the numeral as a letter name;
#   3. a shared build with a two-level CMAKE_INSTALL_LIBDIR, the command line
#      copied outside the prefix.
# The expected line is the output of the Python tugaphone of PARITY.md for the
# same arguments.
if(PREFIX STREQUAL INSTALL_PREFIX)
    message(FATAL_ERROR "PREFIX must differ from CMAKE_INSTALL_PREFIX: ${PREFIX}")
endif()
file(REMOVE_RECURSE "${PREFIX}" "${PREFIX}-lexicon-cache" "${PREFIX}-layouts")

function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        list(JOIN ARGN " " command)
        message(FATAL_ERROR "${command}: ${result}")
    endif()
endfunction()

set(expected "ˈtɛɲu ˈvĩt i ˈtɾez ˈɐnuʃ sɨˈɲoɾ ˈsiɫvɐ ˈli o ˈkap ˈdoz\n")
# The command line to run, then the variables to add to its environment.
function(phonemize program)
    file(REMOVE_RECURSE "${PREFIX}-lexicon-cache")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env --unset=TUGAPHONE_DATA_DIR --unset=TUGALEX_DATA_DIR --unset=BIFONIA_DATA_DIR
                --unset=ORTHOGRAPHY2IPA_DATA_DIR "TUGAPHONE_LEXICON_DIR=${PREFIX}-lexicon-cache" ${ARGN}
                "${program}" phonemize --lect pt-PT "Tenho 23 anos, Sr. Silva; li o Cap. XII."
        WORKING_DIRECTORY "${PREFIX}"
        OUTPUT_VARIABLE output ERROR_VARIABLE error RESULT_VARIABLE result)
    if(NOT result EQUAL 0 OR NOT output STREQUAL expected)
        message(FATAL_ERROR "installed tugaphone (${program}): exit ${result}\nstdout: ${output}\nstderr: ${error}")
    endif()
endfunction()

run("${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${PREFIX}")
phonemize("${PREFIX}/${BINDIR}/tugaphone" "PATH=${PREFIX}/${BINDIR}:$ENV{PATH}")
# The bare name goes through PATH, from a directory that holds no file of
# that name, as a shell does.
phonemize(tugaphone "PATH=${PREFIX}/${BINDIR}:$ENV{PATH}")

set(layouts "${PREFIX}-layouts")
set(libdir "lib/x86_64-linux-gnu")
# The configured prefix is never created, so a library that falls back to it
# finds no data.
foreach(shared OFF ON)
    set(build "${layouts}/build-${shared}")
    run("${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${build}" -G "${GENERATOR}" -DTUGAPHONE_BUILD_TESTS=OFF
        "-DBUILD_SHARED_LIBS=${shared}" "-DCMAKE_INSTALL_LIBDIR=${libdir}" "-DCMAKE_INSTALL_PREFIX=${layouts}/configured"
        "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}" "-DCMAKE_CXX_FLAGS=${CXX_FLAGS}" "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
        "-DZLIB_LIBRARY=${ZLIB_LIBRARY}" "-DZLIB_INCLUDE_DIR=${ZLIB_INCLUDE_DIR}")
    run("${CMAKE_COMMAND}" --build "${build}")
endforeach()

set(static "${layouts}/static/usr/local")
run("${CMAKE_COMMAND}" --install "${layouts}/build-OFF" --prefix "${static}")
set(decoy "${layouts}/static/usr/${DATADIR}/tugaphone")
file(COPY "${static}/${DATADIR}/tugaphone" DESTINATION "${layouts}/static/usr/${DATADIR}")
file(READ "${decoy}/letter_expressions.json" expressions)
string(REPLACE "\"expressions\": {" "\"expressions\": {\"cap XII\": \"\"," decoy_expressions "${expressions}")
if(decoy_expressions STREQUAL expressions)
    message(FATAL_ERROR "${decoy}/letter_expressions.json has no expressions object")
endif()
file(WRITE "${decoy}/letter_expressions.json" "${decoy_expressions}")
phonemize("${static}/${BINDIR}/tugaphone")

set(shared "${layouts}/shared")
run("${CMAKE_COMMAND}" --install "${layouts}/build-ON" --prefix "${shared}")
file(COPY "${shared}/${BINDIR}/tugaphone" DESTINATION "${layouts}/outside")
phonemize("${layouts}/outside/tugaphone" "LD_LIBRARY_PATH=${shared}/${libdir}" "DYLD_LIBRARY_PATH=${shared}/${libdir}")
