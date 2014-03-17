{
  'targets': [
    {
      'target_name': 'http_server',
      'type': 'static_library',
      'sources': [
        'simple_http.cc',
      ],
      'dependencies': [
        'libuv/uv.gyp:libuv',
        'http-parser/http_parser.gyp:http_parser'
      ],
      'xcode_settings': {
        'CLANG_CXX_LANGUAGE_STANDARD': 'c++11',
      },
    },

    {
      'target_name': 'test_server',
      'type': 'executable',
      'sources': [
        'test_server.cc',
      ],
      'include_dirs': [],
      'dependencies': [
        'http_server.gyp:http_server',
      ],
      'xcode_settings': {
        'CLANG_CXX_LANGUAGE_STANDARD': 'c++11',
      },
    },

    {
      'target_name': 'test_client',
      'type': 'executable',
      'sources': [
        'test_client.cc',
      ],
      'include_dirs': [],
      'dependencies': [
        'libuv/uv.gyp:libuv',
        'http_server.gyp:http_server',
      ],
      'xcode_settings': {
        'CLANG_CXX_LANGUAGE_STANDARD': 'c++11',
      },
    }
  ],
}

