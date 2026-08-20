# Shared warning configuration for first-party targets.
#
# Applied per-target on purpose: SDL arrives through FetchContent and must never
# inherit these, or a warning in a dependency becomes our build failure.
#
# -Wconversion and -Wdouble-promotion are on as of the determinism phase, and
# they are not stylistic. An implicit narrowing changes a value; an implicit
# promotion to double changes which rounding a float expression gets, and
# whether it happens can depend on the optimiser. Both are ways a simulation
# stops being reproducible without anyone writing a line of wrong-looking code.
#
# The MSVC equivalents are listed individually because /W4 does not include
# them: C4242/C4244 are narrowing conversions and C4305 is truncation on
# assignment.

function(ktd_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /w14242 /w14244 /w14305)
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
            -Wconversion
            -Wdouble-promotion
        )
        if(KTD_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
