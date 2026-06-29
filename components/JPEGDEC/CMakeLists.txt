idf_component_register(
    SRCS
        "src/JPEGDEC.cpp"
        "src/s3_simd_420.S"
        "src/s3_simd_444.S"
        "src/s3_simd_dequant.S"
    INCLUDE_DIRS
        "src"
)

# JPEGDEC contains many fall-through cases and warnings unrelated to our
# integration; quiet them so build output stays useful.
component_compile_options(
    -Wno-unused-but-set-variable
    -Wno-unused-variable
    -Wno-implicit-fallthrough
    -Wno-deprecated-declarations
)
