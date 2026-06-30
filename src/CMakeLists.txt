idf_component_register(
    SRCS
        "paperbadge_main.cpp"
        "hal/battery.cpp"
        "hal/display.cpp"
        "hal/epdiy_paper_s3_board.c"
        "hal/sd.cpp"
        "hal/touch.cpp"
        "font/builtin_ui_font.cpp"
        "font/font_lab_assets.cpp"
        "font/text_render.cpp"
        "font/xteink_font.cpp"
        "comic/cbz_book.cpp"
        "comic/image_display.cpp"
        "comic/page_loader.cpp"
        "library/book_db.cpp"
        "library/library.cpp"
        "system/settings.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        epdiy
        JPEGDEC
        PNGdec
        miniz
        esp_timer
        esp_adc
        esp_partition
        driver
        fatfs
        sdmmc
        nvs_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error
)
