# libdvdcss source files
dvdcss_src = files(
    'cpxm.cpp',
    'css.cpp',
    'device.cpp',
    'error.cpp',
    'ioctl.cpp',
    'libdvdcpxm.cpp',
    'libdvdcss.cpp',
)

libdvdcss_link_args = []
libdvdcss_platform_kwargs = {}
if host_machine.system() == 'linux'
    libdvdcss_link_args += '-Wl,--version-script=' + meson.current_source_dir() / 'libdvdcss.map'
elif host_machine.system() == 'darwin'
    libdvdcss_link_args += '-Wl,-exported_symbols_list,' + meson.current_source_dir() / 'libdvdcss.exports'
elif host_machine.system() == 'windows'
    libdvdcss_platform_kwargs += {
        'vs_module_defs': files('libdvdcss.def')[0],
    }
endif

#
# Library definitions
#

if host_machine.system() == 'windows' and get_option('default_library') != 'static'
    api_export_flags = '-DLIBDVDCSS_EXPORTS'
else
    api_export_flags = []
endif

# The final libdvdcss library
libdvdcss = library('dvdcss', dvdcss_src,
    include_directories: dvdcss_inc_dirs,
    dependencies: extra_dependencies,
    c_args: [api_export_flags],
    cpp_args: [api_export_flags],
    gnu_symbol_visibility: 'hidden',
    link_args: libdvdcss_link_args,
    version: dvdcss_soname_version,
    install: true,
    kwargs: libdvdcss_platform_kwargs,
)

# Make the library usable as a dependency in this and other projects
libdvdcss_dep = declare_dependency(link_with: libdvdcss,
    include_directories: include_directories('.'))

meson.override_dependency('libdvdcss', libdvdcss_dep)

# Generate pkg-config .pc file
pkg_mod = import('pkgconfig')
cpp_runtime_private = []
if host_machine.system() == 'darwin'
    cpp_runtime_private += '-lc++'
elif cpp.get_argument_syntax() == 'gcc'
    cpp_runtime_private += '-lstdc++'
endif
pkg_mod.generate(libraries: libdvdcss,
    libraries_private: [cpp_runtime_private, threads_dep],
    version: meson.project_version(),
    name: 'libdvdcss',
    description: 'DVD access and decryption library',
)
