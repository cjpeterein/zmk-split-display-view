if(CONFIG_ZMK_SPLIT_DISPLAY_VIEW)
  zephyr_library()
  zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
  zephyr_library_sources(
    src/status_screen.c
    src/assets/bongo_cat_images.c
    src/assets/modifiers_sym.c
    src/assets/output_status_sym.c
  )
endif()

if(CONFIG_ZMK_SPLIT_DISPLAY_VIEW_CENTRAL)
  zephyr_library()
  zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
  zephyr_library_sources(src/central.c)
endif()
