# Installs the build into PREFIX, which is not the CMAKE_INSTALL_PREFIX of the
# build, and runs the installed command line with no data variables. The
# library, the tugalex lexicon, the bifonia tables and the orthography2ipa
# specs must all come from PREFIX. The expected line is the output of the
# Python tugaphone of PARITY.md for the same arguments.
if(PREFIX STREQUAL INSTALL_PREFIX)
    message(FATAL_ERROR "PREFIX must differ from CMAKE_INSTALL_PREFIX: ${PREFIX}")
endif()
file(REMOVE_RECURSE "${PREFIX}" "${PREFIX}-lexicon-cache")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${PREFIX}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "install failed: ${result}")
endif()
# The second run starts the command line by bare name through PATH, from a
# directory that holds no file of that name, as a shell does.
set(expected "ˈtɛɲu ˈvĩt i ˈtɾez ˈɐnuʃ sɨˈɲoɾ ˈsiɫvɐ ˈli o ˈkap ˈdoz\n")
foreach(program "${PREFIX}/${BINDIR}/tugaphone" tugaphone)
    file(REMOVE_RECURSE "${PREFIX}-lexicon-cache")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env --unset=TUGAPHONE_DATA_DIR --unset=TUGALEX_DATA_DIR --unset=BIFONIA_DATA_DIR
                --unset=ORTHOGRAPHY2IPA_DATA_DIR "TUGAPHONE_LEXICON_DIR=${PREFIX}-lexicon-cache"
                "PATH=${PREFIX}/${BINDIR}:$ENV{PATH}"
                "${program}" phonemize --lect pt-PT "Tenho 23 anos, Sr. Silva; li o Cap. XII."
        WORKING_DIRECTORY "${PREFIX}"
        OUTPUT_VARIABLE output ERROR_VARIABLE error RESULT_VARIABLE result)
    if(NOT result EQUAL 0 OR NOT output STREQUAL expected)
        message(FATAL_ERROR "installed tugaphone (${program}): exit ${result}\nstdout: ${output}\nstderr: ${error}")
    endif()
endforeach()
