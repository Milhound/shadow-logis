if(CPACK_GENERATOR STREQUAL "TGZ")
    # Relocatable directory layout: extract, then run bin/shadow-logis.
    set(CPACK_PACKAGING_INSTALL_PREFIX "/")
endif()
