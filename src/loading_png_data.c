// Embeds assets/loading.png into the firmware via GAS .incbin.
//
// We tried ESP-IDF's EMBED_FILES helper first, but its OUTPUT path
// gets the build prefix doubled under PlatformIO on Windows
// (".pio/build/paper_s3/.pio/build/paper_s3/loading.png.S" — the
// custom_command target never resolves). The escape hatch is to
// have the assembler include the file directly with .incbin, with
// the absolute path passed in as a preprocessor define from CMake.
//
// Symbols mirror the conventional `_binary_<name>_start/_end` form
// so callers feel like they're using EMBED_FILES.

#ifndef LOADING_PNG_PATH
#error "LOADING_PNG_PATH must be defined to the absolute path of " \
       "assets/loading.png (set via CMakeLists.txt)"
#endif

#define STR_(x) #x
#define STR(x)  STR_(x)

__asm__(
    ".section .rodata.embedded\n"
    ".balign 4\n"
    ".global loading_png_start\n"
    ".global loading_png_end\n"
    "loading_png_start:\n"
    ".incbin \"" STR(LOADING_PNG_PATH) "\"\n"
    "loading_png_end:\n"
    ".byte 0\n"
);
