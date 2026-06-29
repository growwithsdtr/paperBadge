idf_component_register(
    SRCS
        "src/PNGdec.cpp"
        "src/adler32.c"
        "src/crc32.c"
        "src/infback.c"
        "src/inffast.c"
        "src/inflate.c"
        "src/inftrees.c"
        "src/zutil.c"
        "src/s3_simd_rgb565.S"
    INCLUDE_DIRS
        "src"
)

# PNGdec ships its own miniature zlib and reuses lots of upstream
# warning-prone idioms; quiet them so build output stays useful.
component_compile_options(
    -Wno-unused-but-set-variable
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-implicit-fallthrough
    -Wno-deprecated-declarations
    -Wno-sign-compare
)
