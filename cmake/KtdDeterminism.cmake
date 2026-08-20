# Float semantics for the simulation.
#
# ADR 0002 keeps the game on float rather than fixed-point, on the grounds that
# IEEE-754 + - * / and sqrt are correctly rounded and therefore bit-identical
# across macOS, Windows and Linux. That argument only holds if the compiler is
# actually told to respect IEEE-754, which by default it mostly is -- except for
# FMA contraction, which is on by default in GCC and Clang and silently changes
# results by fusing a multiply and an add into one differently-rounded
# operation. Whether it fires depends on the optimiser, so it is also the hazard
# most likely to make Debug and Release disagree on the same machine.
#
# Applied PUBLIC on ktd_game so anything that compiles simulation headers inline
# -- tests, the app's draw code, the hash dump tool -- gets the same semantics.
# A header-inlined length() evaluated under contraction in one translation unit
# and without it in another is exactly the kind of divergence this prevents.

function(ktd_set_determinism_flags target)
    if(MSVC)
        target_compile_options(${target} PUBLIC /fp:precise /fp:contract-)
    else()
        target_compile_options(${target} PUBLIC -ffp-contract=off -fno-fast-math)
    endif()
endfunction()

# Fail at configure time if fast-math has been switched on anywhere.
#
# This is not hypothetical: -Ofast is the obvious thing to reach for during a
# performance experiment, and it implies -ffast-math, which permits the compiler
# to reassociate float arithmetic and flush denormals. That does not produce a
# crash or a warning -- it produces a save file that loads to a different game
# on someone else's machine. Better to refuse to configure.
function(ktd_deny_fast_math)
    set(_denied "-ffast-math" "-Ofast" "/fp:fast" "/Ofast")
    set(_vars
        CMAKE_CXX_FLAGS
        CMAKE_CXX_FLAGS_DEBUG
        CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_RELWITHDEBINFO
        CMAKE_CXX_FLAGS_MINSIZEREL
        CMAKE_C_FLAGS
        CMAKE_EXE_LINKER_FLAGS
        CMAKE_SHARED_LINKER_FLAGS
    )
    foreach(_var IN LISTS _vars)
        foreach(_flag IN LISTS _denied)
            string(FIND "${${_var}}" "${_flag}" _found)
            if(NOT _found EQUAL -1)
                message(FATAL_ERROR
                    "${_var} contains ${_flag}, which breaks cross-platform reproducibility.\n"
                    "Replays and save files depend on bit-identical simulation results; see\n"
                    "docs/decisions/0002-float-not-fixed-point.md. If you need this for a\n"
                    "throwaway performance experiment, do it on a branch and do not merge it.")
            endif()
        endforeach()
    endforeach()
endfunction()
