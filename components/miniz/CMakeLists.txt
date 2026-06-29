idf_component_register(
    SRCS "miniz.c"
    # ESP-IDF's esp_rom component ships its own stripped-down miniz.h
    # (with MINIZ_NO_STDIO baked in). To avoid a header-name collision
    # we put our copy under include/local/miniz.h and consumers spell
    # it as #include "local/miniz.h".
    INCLUDE_DIRS "include"
)

# Just quiet the warnings — keep all of miniz's APIs available so the
# reader path (mz_zip_reader_*) links cleanly.
component_compile_options(
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-but-set-variable
)
