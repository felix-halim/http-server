{
  'includes': [ 'common.gypi' ],
  'targets': [
    {
      'target_name': 'http_server',
      'type': 'static_library',
      'sources': [
        'http_server.cc',
      ],
      'dependencies': [
        './libuv/uv.gyp:libuv',
        './http-parser/http_parser.gyp:http_parser'
      ],
      'cflags_cc': [ '-std=gnu++11' ],
      'xcode_settings': {
        'CLANG_CXX_LANGUAGE_STANDARD': 'c++11',
        'CLANG_CXX_LIBRARY': 'libc++',
        'MACOSX_DEPLOYMENT_TARGET': '10.8',
      },
    },

    {
      'target_name': 'test_server',
      'type': 'executable',
      'sources': [
        'test.cc',
      ],
      'include_dirs': [ './http-parser' ],
      'dependencies': [
        './http_server.gyp:http_server',
      ],
      'cflags_cc': [ '-std=gnu++11' ],
      'xcode_settings': {
        'CLANG_CXX_LANGUAGE_STANDARD': 'c++11',
        'CLANG_CXX_LIBRARY': 'libc++',
        'MACOSX_DEPLOYMENT_TARGET': '10.8',
      },
    }
  ],
}

