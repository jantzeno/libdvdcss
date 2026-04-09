if not get_option('enable_examples')
    subdir_done()
endif

fs = import('fs')

csstest_src = files('csstest.c')
if fs.exists(meson.current_source_dir() / 'csstest.cpp')
    csstest_src = files('csstest.cpp')
endif

dvd_region_src = files('dvd_region.c')
if fs.exists(meson.current_source_dir() / 'dvd_region.cpp')
    dvd_region_src = files('dvd_region.cpp')
endif

executable('csstest', csstest_src,
    dependencies: libdvdcss_dep)

executable('dvd_region', dvd_region_src,
    dependencies: libdvdcss_dep,
    include_directories: dvdcss_inc_dirs)
