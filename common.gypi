{
  'variables': {
    'conditions': [
      ['OS == "mac"', {
        'target_arch%': 'x64'
      }, {
        'target_arch%': 'ia32'
      }]
    ]
  },
  'conditions': [
    ['OS=="mac" or OS=="ios"', {
      'target_defaults': {
        'mac_bundle': 0,
        'xcode_settings': {
          'ALWAYS_SEARCH_USER_PATHS': 'NO',
          # Don't link in libarclite_macosx.a, see http://crbug.com/156530.
          'CLANG_LINK_OBJC_RUNTIME': 'NO',          # -fno-objc-link-runtime
          'GCC_C_LANGUAGE_STANDARD': 'c99',         # -std=c99
          'GCC_CW_ASM_SYNTAX': 'NO',                # No -fasm-blocks
          'GCC_ENABLE_CPP_EXCEPTIONS': 'NO',        # -fno-exceptions
          'GCC_ENABLE_CPP_RTTI': 'NO',              # -fno-rtti
          'GCC_ENABLE_PASCAL_STRINGS': 'NO',        # No -mpascal-strings
          # GCC_INLINES_ARE_PRIVATE_EXTERN maps to -fvisibility-inlines-hidden
          'GCC_INLINES_ARE_PRIVATE_EXTERN': 'YES',
          'GCC_OBJC_CALL_CXX_CDTORS': 'YES',        # -fobjc-call-cxx-cdtors
          'GCC_SYMBOLS_PRIVATE_EXTERN': 'YES',      # -fvisibility=hidden
          'GCC_THREADSAFE_STATICS': 'NO',           # -fno-threadsafe-statics
          'GCC_TREAT_WARNINGS_AS_ERRORS': 'YES',    # -Werror
          'USE_HEADERMAP': 'NO',
          'WARNING_CFLAGS': [
            '-Wall',
            '-Wendif-labels',
            '-Wextra',
            '-Wno-unused-parameter',
            '-Wno-missing-field-initializers',
          ],
          'CLANG_WARN_CXX0X_EXTENSIONS': 'NO',
          'CLANG_WARN_OBJC_MISSING_PROPERTY_SYNTHESIS': 'YES',
          'GCC_VERSION': 'com.apple.compilers.llvm.clang.1_0',
          'WARNING_CFLAGS': [
            '-Wno-c++11-narrowing',
            '-Wno-char-subscripts',
            '-Wno-unused-function',
            '-Wno-covered-switch-default',
            '-Wno-deprecated-register',
          ],
        },
      },  # target_defaults
    }],  # OS=="mac" or OS=="ios"
  ],
  'target_defaults': {
    'default_configuration': 'release',
    #'defines': [ 'HTTP_PARSER_STRICT=0' ],
    'conditions': [
      ['OS == "mac"', {
        'defines': [ 'DARWIN' ]
      }, {
        'defines': [ 'LINUX' ]
      }],
      ['OS == "mac" and target_arch == "x64"', {
        'xcode_settings': {
          'ARCHS': [ 'x86_64' ]
        },
      }]
    ],
    'configurations': {
      'debug': {
        'cflags': [ '-g', '-O0' ],
        'defines': [ 'DEBUG' ],
        'xcode_settings': {
          'OTHER_CFLAGS': [ '-g', '-O0' ]
        }
      },
      'release': {
        'cflags': [ '-O3' ],
        #'defines': [ 'NDEBUG' ],
        'xcode_settings': {
          'OTHER_CFLAGS': [ '-O3' ]
        }
      }
    }
  }
}
