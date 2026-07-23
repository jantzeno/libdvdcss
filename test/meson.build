if get_option('enable_tests')
    c_api_smoke = executable('c_api_smoke', files('c_api_smoke.c'),
        dependencies: libdvdcss_dep)
    test('c-api-smoke', c_api_smoke)

    stream_api_test = executable('stream_api_test', files('stream_api_test.cpp'),
        dependencies: libdvdcss_dep)
    test('stream-api', stream_api_test,
        env: ['DVDCSS_CACHE=off', 'DVDCSS_VERBOSE=0'])

    if host_machine.system() == 'linux' and \
       get_option('default_library') != 'static'
        python = import('python').find_installation('python3')
        nm = find_program('nm')
        test('exported-c-abi', python,
            args: [files('verify_exports.py'), nm.full_path(),
                   libdvdcss.full_path()])
    endif
endif

if get_option('enable_examples')
    executable('csstest', files('csstest.cpp'),
        dependencies: libdvdcss_dep)

    if host_machine.system() == 'linux'
        executable('dvd_region', files('dvd_region.cpp'))
    endif
endif
