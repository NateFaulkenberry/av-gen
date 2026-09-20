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
            # ADR-393. One warning promoted to an error, by name, while the blanket flag below stays
            # off. A census of this build found 1549 warnings, 1462 of them one category in one file
            # (`src/ui/ui_logic.hpp`, `-Wmissing-field-initializers`) -- so `-Werror` today breaks
            # the build in a file another branch is editing, and a blanket flag that has to be
            # fought is a flag somebody turns off again.
            #
            # `-Wformat` is different: it had exactly one instance in the whole tree and that
            # instance was a live bug -- a printf-style ImGui tooltip whose text contained a literal
            # per-cent sign, so it read a double off an empty varargs list. Zero instances remain,
            # so this costs nothing to keep and catches the next one at the compiler rather than in
            # a screenshot.
            #
            # `-Werror=switch` is the other one that earns it and is NOT here yet: it has two live
            # instances, `scene/composition.cpp` (`NodeKind::City`) and `ui/sequence_panel.cpp`
            # (`Drag::Marquee`), and what those two enumerators should do in those switches is a
            # decision for whoever owns them rather than a silencing. It is the natural next step.
            -Werror=format)
        if(AVGEN_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
    if(AVGEN_ENABLE_ASAN AND NOT MSVC)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
    if(AVGEN_ENABLE_TSAN AND NOT MSVC)
        target_compile_options(${target} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=thread)
    endif()
endfunction()
