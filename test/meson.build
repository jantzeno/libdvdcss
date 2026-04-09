if not get_option('enable_examples')
    subdir_done()
endif

executable('csstest', files('csstest.cpp'),
    dependencies: libdvdcss_dep)

executable('dvd_region', files('dvd_region.cpp'),
    dependencies: libdvdcss_dep,
    include_directories: dvdcss_inc_dirs)
