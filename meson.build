project('cefbrowser', 'cpp', 'c',
  version : '0.1',
  default_options : ['warning_level=1', 'cpp_std=c++17', 'default_library=static', 'optimization=3', 'buildtype=debug' ],
  meson_version: '>=0.63.0')

add_global_arguments('-O3', language : 'cpp')

arch=get_option('arch')

if get_option('vdrsternelec')
    subarch = '-march=' + get_option('subarch')
    install_prefix = get_option('prefix')
endif

cmake = import('cmake')
CXX = meson.get_compiler('cpp')

#
# Download and extract subprojects
#
run_command('./setup.sh', arch, meson.current_build_dir(), check: false)

#
# Build cef
#
cef_opt_var = cmake.subproject_options()

if arch == 'arm'
    cef_opt_var.add_cmake_defines({'PROJECT_ARCH': 'armhf'})
elif arch == 'arm64'
    cef_opt_var.add_cmake_defines({'PROJECT_ARCH': 'arm64'})
elif arch == 'x86_64'
    cef_opt_var.add_cmake_defines({'PROJECT_ARCH': 'x86_64'})
endif

if get_option('vdrsternelec')
    cef_opt_var.add_cmake_defines({'CMAKE_CXX_FLAGS': subarch})
    cef_opt_var.add_cmake_defines({'CMAKE_C_FLAGS': subarch})
endif

cef_opt_var.append_compile_args('cpp', '-Wno-unused-variable')
cef_opt_var.add_cmake_defines({'CMAKE_BUILD_TYPE': 'Debug'})
cef_proj = cmake.subproject('cef', options: cef_opt_var)
cef_lib = cef_proj.dependency('libcef_dll_wrapper')

#
# spdlog
#
spdlog_proj = subproject('spdlog')
spdlog_dep = spdlog_proj.get_variable('spdlog_dep', ['tests=disabled', 'compile_library=true'] )

#
# tiny-process-library
#
tiny_process_library_proj = subproject('tiny_process_library')
tiny_process_library_dep = tiny_process_library_proj.get_variable('tiny_process_library_dep')

#
# mINI
#
mini_proj = subproject('mini')
mini_dep = mini_proj.get_variable('mini_dep')

#
# sqlite3
#
sqlite3_proj = subproject('sqlite3')
sqlite3_dep = sqlite3_proj.get_variable('sqlite3_dep')

#
# ssl
#
ssl_dep = dependency('libssl')
crypto_dep = dependency('libcrypto')

#
# thrift
#
if get_option('vdrsternelec')
    thrift_dep = dependency('thrift')
else
    thrift_opts = cmake.subproject_options()
    thrift_opts.add_cmake_defines({'WITH_AS3': 'OFF',
                                   'WITH_QT5': 'OFF',
                                   'BUILD_JAVA': 'OFF',
                                   'BUILD_JAVASCRIPT': 'OFF',
                                   'BUILD_PYTHON': 'OFF',
                                   'BUILD_TESTING': 'OFF',
                                   'WITH_C_GLIB': 'OFF',
                                   'WITH_OPENSSL': 'OFF',
                                   'BUILD_SHARED_LIBS': 'OFF',
                                   'BUILD_COMPILER': 'OFF',
                                   'CMAKE_INSTALL_PREFIX': '.',
                                   'CMAKE_POSITION_INDEPENDENT_CODE': 'ON',
                                   'CMAKE_CXX_STANDARD': '11'})

    thrift_proj = cmake.subproject('thrift', options: thrift_opts)
    thrift_dep = thrift_proj.get_variable('thrift_dep')
endif

#
# curl
#
curl_dep = dependency('libcurl', fallback: ['curl', 'curl_dep'])

#
# cpr
#
cpr_proj = subproject('cpr', default_options: [])
cpr_dep = cpr_proj.get_variable('cpr_dep')

libidn2 = dependency('libidn2', required: true)

#
# Build browser
#
shared_libs = ['cef', 'z']
deps = []
foreach lib_name : shared_libs
        deps += CXX.find_library(lib_name, dirs : [meson.project_source_root() + '/subprojects/cef/Release'])
endforeach

incdir = include_directories(['thrift-services/src-gen', 'thrift-services/src-client'])
browser_compile_args = ['-DPHTTPLIB_ZLIB_SUPPORT', '-DCPPHTTPLIB_OPENSSL_SUPPORT']

if get_option('debug_thrift')
    browser_compile_args += '-DDEBUG_THRIFTCLIENT'
endif

sources = ['cefbrowser', 'main.cpp', 'mainapp.cpp', 'logger.cpp',
           'browserclient.cpp', 'osdsender.cpp', 'browserserver.cpp', 'moviestream.cpp', 'mserequest.cpp',
           'httpinterception.cpp', 'keycodes.cpp', 'trackinginterception.cpp', 'statichandler.cpp', 'jsinterception.cpp',
           'v8handler.cpp', 'tools.cpp', 'database.cpp', 'xhrinterception.cpp', 'pagemodifier.cpp', 'baseinterception.cpp',
           'thrift-services/src-client/VdrClient.cpp', 'thrift-services/src-client/TranscoderClient.cpp',
           'thrift-services/src-gen/CommonService.cpp', 'thrift-services/src-gen/common_types.cpp',
           'thrift-services/src-gen/CefBrowser.cpp', 'thrift-services/src-gen/cefbrowser_types.cpp',
           'thrift-services/src-gen/VdrPluginWeb.cpp', 'thrift-services/src-gen/pluginweb_types.cpp',
           'thrift-services/src-gen/RemoteTranscoder.cpp', 'thrift-services/src-gen/remotetranscoder_types.cpp']

deps = [deps, mini_dep, cef_lib, spdlog_dep, sqlite3_dep, ssl_dep, crypto_dep, thrift_dep, curl_dep, cpr_dep, libidn2]

if get_option('vdrsternelec')
    exe = executable(sources,
                     include_directories : incdir,
                     cpp_args : browser_compile_args,
                     install : true,
                     link_args: '-lrt',
                     dependencies: deps)
else
    exe = executable(sources,
                 include_directories : incdir,
                 cpp_args : browser_compile_args,
                 install : true,
                 install_dir : meson.current_build_dir() + '/Release',
                 dependencies: deps)
endif

#
# install static content
#
if get_option('vdrsternelec')
    install_subdir('static-content/js', install_dir : 'cefbrowser')
    install_subdir('static-content/css', install_dir : 'cefbrowser')
    install_subdir('static-content/database', install_dir : 'cefbrowser')
    install_subdir('static-content/application', install_dir : 'cefbrowser')
    install_subdir('static-content/fonts', install_dir : 'cefbrowser')
else
    install_subdir('static-content/js', install_dir : meson.current_build_dir() + '/Release')
    install_subdir('static-content/css', install_dir : meson.current_build_dir() + '/Release')
    install_subdir('static-content/database', install_dir : meson.current_build_dir() + '/Release')
    install_subdir('static-content/application', install_dir : meson.current_build_dir() + '/Release')
    install_subdir('static-content/fonts', install_dir : meson.current_build_dir() + '/Release')
endif

