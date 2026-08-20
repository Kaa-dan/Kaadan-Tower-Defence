# Shared warning configuration for first-party targets.
#
# Applied per-target on purpose: SDL arrives through FetchContent and must never
# inherit these, or a warning in a dependency becomes our build failure.
#
# -Wconversion and -Wdouble-promotion are deliberately absent for now. Both are
# determinism-relevant (an accidental promotion to double changes results) and
# both are planned, but they produce a wave of noise in the existing cast sites.
# They get switched on in the same pass that reworks the simulation math, so the
# churn lands with the work it belongs to.

function(ktd_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(KTD_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wold-style-cast
            -Wfloat-equal
        )
        if(KTD_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
