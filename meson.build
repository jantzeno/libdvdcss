project('libdvdcss', 'cpp',
    version: '1.6.0',
    meson_version: '>= 0.60.0',
    default_options: ['buildtype=debugoptimized',
                      'cpp_std=c++20',
                      'default_library=both',
                      'warning_level=2'])

dvdcss_src_root = meson.current_source_dir()
dvdcss_build_root = meson.current_build_dir()
cpp = meson.get_compiler('cpp')

# Configuration data for config.h
cdata = configuration_data()

# Include directories
dvdcss_inc_dirs = include_directories('.', 'src', 'src/dvdcss')

# The version number for the shared library
dvdcss_soname_version = '2.4.0'

dvdcss_version = meson.project_version()
dvdcss_version_split = dvdcss_version.split('.')
cdata.set_quoted('PACKAGE_VERSION', dvdcss_version)

#
# OS/Compiler checks and defines
#

# Arguments in test_args will be used even on feature tests
test_args = ['-D_DEFAULT_SOURCE']
extra_dependencies = []

if host_machine.system() == 'windows' and cpp.get_argument_syntax() == 'msvc'
    test_args += ['-D_CRT_SECURE_NO_WARNINGS', '-D_CRT_NONSTDC_NO_WARNINGS',
                  '-D_WIN32_IE=0x0501', '-DWIN32_LEAN_AND_MEAN', '-DPATH_MAX=2048']
elif host_machine.system() == 'sunos'
    test_args += '-D_POSIX_C_SOURCE=200809L'
endif

# Header checks

check_headers = [
    'errno.h',
    'fcntl.h',
    'io.h',
    'pwd.h',
    'scsi/sg.h',
    'cam/scsi/scsi_sg.h',
    'sys/ioctl.h',
    'sys/param.h',
    'sys/stat.h',
    'sys/types.h',
    'sys/uio.h',
    'unistd.h',
]

foreach h : check_headers
    if cpp.has_header(h, args: test_args)
        define_name = 'HAVE_' + h.underscorify().to_upper()
        cdata.set(define_name, 1)
    endif
endforeach

# DVD ioctl support (OS-specific)

if host_machine.system() == 'windows'\
   and cpp.has_header('winioctl.h', args: test_args)
   # Windows is supported as long as winioctl.h is found
elif host_machine.system() == 'darwin'
    add_project_arguments(cpp.get_supported_arguments('-no-cpp-precomp'), language: 'cpp')
    cdata.set('DARWIN_DVD_IOCTL', 1)
    extra_dependencies += dependency('appleframeworks', modules: ['CoreFoundation', 'IOKit'])
elif host_machine.system() == 'sunos' \
     and cpp.has_header('sys/scsi/scsi_types.h', args: test_args) \
     and cpp.has_header('sys/scsi/impl/uscsi.h', args: test_args)
    cdata.set('SOLARIS_USCSI', 1)
elif host_machine.system() == 'haiku' \
     and cpp.has_header('scsi.h', args: test_args)
    # Haiku is supported as long as scsi.h is found
elif host_machine.system() == 'openbsd' \
     and cpp.has_header('sys/cdio.h', args: test_args)
    cdata.set('DVD_STRUCT_IN_SYS_CDIO_H', 1)
    cdata.set('HAVE_LINUX_DVD_STRUCT', 1)
    cdata.set('HAVE_OPENBSD_DVD_STRUCT', 1)
elif host_machine.system() in ['linux', 'android'] \
     or cpp.has_header_symbol('linux/cdrom.h', 'dvd_struct', args: test_args)
    cdata.set('DVD_STRUCT_IN_LINUX_CDROM_H', 1)
    cdata.set('HAVE_LINUX_DVD_STRUCT', 1)
elif cpp.has_header_symbol('sys/dvdio.h', 'dvd_struct',
        prefix: '#include <sys/types.h>', args: test_args) # NetBSD
    cdata.set('DVD_STRUCT_IN_SYS_DVDIO_H', 1)
    cdata.set('HAVE_LINUX_DVD_STRUCT', 1)
elif cpp.has_header_symbol('sys/dvdio.h', 'struct dvd_struct', 
        prefix: '#include <sys/types.h>', args: test_args) # FreeBSD
    cdata.set('DVD_STRUCT_IN_SYS_DVDIO_H', 1)
    cdata.set('HAVE_BSD_DVD_STRUCT', 1)
else
    warning('No DVD ioctls found for this system. libdvdcss will probably fail to build.')
endif

posix_mkdir = ''
foreach h: ['io.h', 'sys/stat.h', 'sys/types.h']
    if cdata.has('HAVE_' + h.underscorify().to_upper())
        posix_mkdir += f'#include <@h@>\n'
    endif
endforeach

posix_mkdir += '''
int mkdir(const char *path, mode_t mode);
int main() { }
'''

if not cpp.compiles(posix_mkdir, args: test_args, name: 'POSIX mkdir')
    cdata.set('HAVE_BROKEN_MKDIR', 1)
endif

cdata.set('SUPPORT_ATTRIBUTE_VISIBILITY_DEFAULT', cpp.has_function_attribute('visibility:default'))

add_project_arguments(test_args, language: 'cpp')

# Generate config.h
configure_file(output: 'config.h', configuration: cdata)

install_data(['AUTHORS', 'COPYING', 'NEWS', 'README.md'],
    install_dir: get_option('datadir') / 'doc/libdvdcss',
    install_tag: 'doc')

# Include subdir meson.build files
# The order is important!

subdir('src/dvdcss')

subdir('src')

subdir('test')

subdir('doc')
