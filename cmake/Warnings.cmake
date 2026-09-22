# Warning flags applied to engine targets only (never to dependencies).
function(avgen_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        if(AVGEN_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast
            -Wcast-align -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion
            -Wno-unused-parameter
            # ADR-420. One warning promoted to an error by name, while the blanket flag below stays
            # off. A full recompile of the engine targets -- `src` AND `tests` -- on 2026-09-20
            # counted **3211** warnings, of which **2584** are `-Wmissing-field-initializers` and
            # **2577** of those are in one file, `src/ui/ui_logic.hpp`. A blanket `-Werror` would
            # therefore break the build in one header for one reason, which is a flag somebody turns
            # off again rather than a guard.
            #
            # (ADR-393 on `agent/ocean2` reached the same conclusion from a census of 1549/1462. That
            # census counted `src` only and predates the header growing; the numbers above supersede
            # it. A stale census in an accepted ADR is the same shape as a stale sentence in one.)
            #
            # `-Wformat` is different and earns the promotion: it had exactly one instance in the
            # whole tree and that instance was a live bug -- `ui/control_panel.cpp`'s supersampling
            # tooltip was printf-style with two literal per-cent signs in its text, so it read two
            # doubles off an empty varargs list every frame the pointer rested on that row. Zero
            # instances remain, so this costs nothing to keep and catches the next one at the
            # compiler rather than in a screenshot.
            #
            # `-Werror=switch` is the other one that earns it and is deliberately NOT here. It has
            # two live instances -- `scene/composition.cpp` (`NodeKind::City`) and
            # `ui/sequence_panel.cpp` (`Drag::Marquee`) -- and what those enumerators should do in
            # those switches is a design decision owned by whoever owns those enums, not a silencing
            # for a passing branch to perform. It is the natural next step and it needs their answer.
            # ADR-392 and ADR-420 are both about a kind falling through a switch, so this is the
            # compiler offering to catch that whole class for free.
            -Werror=format)
        if(AVGEN_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
    avgen_set_sanitizers(${target})
endfunction()

# The sanitizer flags, separable from the warning flags because a target can need these without
# those. Every executable in `tools/` links `avgen_core`, which IS instrumented under
# `--preset asan`; a tool that does not also link the sanitizer runtime fails with undefined
# `___asan_*` symbols. Splitting this out lets those targets opt into the runtime without also
# taking on the engine's warning set, which is a separate decision with its own risk.
function(avgen_set_sanitizers target)
    if(AVGEN_ENABLE_ASAN AND NOT MSVC)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
    if(AVGEN_ENABLE_TSAN AND NOT MSVC)
        target_compile_options(${target} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=thread)
    endif()
endfunction()
