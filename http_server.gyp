{
  'targets': [
    {
      'target_name': 'http_server',
      'type': 'static_library',
      'sources': [
        'http_server.cc',
        'http_server_impl.cc',
        'simple_parser.cc',
        'http_client.cc',
        'logger.cc',
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
      'target_name': 'test_add',
      'type': 'executable',
      'sources': [
        'test_add.cc',
      ],
      'include_dirs': [],
      'dependencies': [
        'libuv/uv.gyp:libuv',
        'http_server.gyp:http_server',
      ],
      'xcode_settings': {
        'CLANG_CXX_LANGUAGE_STANDARD': 'c++11',
      },
    },

    {
      'target_name': 'test_add_async',
      'type': 'executable',
      'sources': [
        'test_add_async.cc',
      ],
      'include_dirs': [],
      'dependencies': [
        'libuv/uv.gyp:libuv',
        'http_server.gyp:http_server',
      ],
      'xcode_settings': {
        'CLANG_CXX_LANGUAGE_STANDARD': 'c++11',
      },
    },

    {
      'target_name': 'test_add_flush',
      'type': 'executable',
      'sources': [
        'test_add_flush.cc',
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

