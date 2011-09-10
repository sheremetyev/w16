{
  'include': '../build/common.gypi',
  'targets': [
    {
      'target_name': 'w16',
      'type': 'executable',
      'dependencies': [
        '../tools/gyp/v8.gyp:v8_base',
        '../tools/gyp/v8.gyp:js2c',
        '../tools/gyp/v8.gyp:v8_nosnapshot',
      ],
      'include_dirs': [
        '../src',
      ],
      'sources': [
        'main.cc',
        'primes.js',
      ],
      # disabled warning
      #  VC\include\typeinfo(157): warning C4275: non dll-interface class
      # 'stdext::exception' used as base for dll-interface class 'std::bad_cast'
      'msvs_disabled_warnings': [4275],
      'msvs_settings': {
        'VCCLCompilerTool': {
          'Optimization': '0',
          'RuntimeLibrary': '1',  # /MTd
        },
        'VCLinkerTool': {
          'LinkIncremental': '2',
        },
      },
    },
  ],
}
