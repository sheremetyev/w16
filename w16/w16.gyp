{
  'includes': ['../build/common.gypi'],
  'targets': [
    {
      'target_name': 'w16',
      'type': 'executable',
      'dependencies': [
        '../tools/gyp/v8.gyp:v8',
      ],
      'include_dirs': [
        '../src',
      ],
      'sources': [
        'main.cc',
        'primes.js',
      ],
    },
  ],
}
