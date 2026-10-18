# Compiler warnings are errors on every platform.
#
# MSVC: /W4 /WX. GCC and Clang: -Wall -Wextra -Wpedantic -Werror.
# CMake's own developer warnings are turned into errors by the presets
# ("errors": {"dev": true}), which is the same as passing -Werror=dev.
function(app_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()
