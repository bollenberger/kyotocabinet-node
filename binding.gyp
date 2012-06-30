{
    'variables': {
        'kyotocabinet_shared_include_dir': '<(module_root_dir)/deps/kyotocabinet',
        'kyotocabinet_shared_library': '<(module_root_dir)/deps/kyotocabinet/libkyotocabinet.a'
    }, 
    'target_defaults': {
        # HACK:S resolve exception throw compile error !!
        'cflags': [ '-fexceptions' ],
        'cflags_cc': [ '-fexceptions' ],
        'cflags!': [ '-fno-exceptions' ],
        'cflags_cc!': [ '-fno-exception' ],
        # HACK:E resolve exception throw compile error !!
        'configurations': {
            'Debug': {
                'defines': [ 'DEBUG', '_DEBUG' ]
            },
            'Release': {
            }
        },
    },
    'targets': [{
        'target_name': 'kyotocabinet',
        'dependencies': [
          'kyotocabinet_core'
        ],
        'sources': [
            './src/visitor_wrap.cc',
            './src/polydb_wrap.cc',
            './src/kyotocabinet.cc'
        ],
        'libraries': [
            '<(kyotocabinet_shared_library)'
        ], 
        'include_dirs': [
            '<(kyotocabinet_shared_include_dir)'
        ], 
        'cflags': [ '-fcxx-exceptions' ],
        'ldflags': [],
        'conditions': [[
            'OS == "win"', {
            }
        ], [
            'OS=="linux" or OS=="freebsd" or OS=="openbsd" or OS=="solaris"', {
            }
        ], [
            'OS=="mac"', {
                'xcode_settings': {
                    'GCC_ENABLE_CPP_EXCEPTIONS': 'YES'
                }
            }
        ]]
    }, {
        'target_name': 'kyotocabinet_core',
        'type': 'none',
        'actions': [{
            'action_name': 'test',
            'inputs': ['<!@(sh kyotocabinet-config.sh)'],
            'outputs': [''],
            'conditions': [[
                'OS=="win"', {
                    'action': [
                        'echo', 'notsupport'
                    ]
                }, {
                    'action': [
                        # run kyotocabinet `make`
                        'sh', 'kyotocabinet-build.sh'
                    ]
                }
            ]]
        }]
    }]
}
