project('flootay', 'c')

pkg = import('pkgconfig')

cc = meson.get_compiler('c')

foreach a : ['-Wall', '-Wuninitialized', '-Wempty-body', '-Wformat',
      '-Wformat-security', '-Winit-self', '-Wundef',
      '-Wvla', '-Wpointer-arith', '-Wmissing-declarations']
  if cc.has_argument(a)
    add_project_arguments(a, language : ['c'])
  endif
endforeach

m_dep = cc.find_library('m', required : false)
sdl_dep = dependency('sdl2')
sdl_image_dep = dependency('SDL2_image')
cairo_dep = dependency('cairo')
rsvg_dep = dependency('librsvg-2.0')
expat_dep = dependency('expat')
curl_dep = dependency('libcurl')
json_dep = dependency('json-c')

flootay_deps = [m_dep, cairo_dep, rsvg_dep, expat_dep, curl_dep]

flootay_lib = library('flootay',
                      ['flootay-lib.c',
                       'flt-buffer.c',
                       'flt-color.c',
                       'flt-error.c',
                       'flt-file-error.c',
                       'flt-gpx.c',
                       'flt-lexer.c',
                       'flt-list.c',
                       'flt-map-renderer.c',
                       'flt-parse-stdio.c',
                       'flt-parse-time.c',
                       'flt-parser.c',
                       'flt-renderer.c',
                       'flt-scene.c',
                       'flt-utf8.c',
                       'flt-util.c',
                       'flootay-lib.c'],
                      dependencies: flootay_deps,
                      install: true)

pkg.generate(flootay_lib)

install_headers('flootay.h')

executable('flootay',
           ['flootay.c'],
           link_with: [flootay_lib],
           dependencies: flootay_deps)

executable('generate-logo',
           ['generate-logo.c',
            'flt-util.c'],
           dependencies: [m_dep, cairo_dep, rsvg_dep, expat_dep])

executable('make-key-frames',
           ['make-key-frames.c'],
           link_with: [flootay_lib],
           dependencies: [sdl_dep, sdl_image_dep] + flootay_deps)

executable('run-ffmpeg',
           ['run-ffmpeg.c',
            'flt-list.c',
            'flt-util.c',
            'flt-child-proc.c',
            'flt-buffer.c'])

executable('generate-sound',
           ['generate-sound.c',
            'flt-util.c',
            'flt-child-proc.c',
            'flt-buffer.c',
            'flt-get-video-length.c',
            'flt-list.c'],
            dependencies: [m_dep])

executable('gpx-to-svg',
           ['gpx-to-svg.c'],
           dependencies: [m_dep, expat_dep])

executable('pos-to-time',
           ['pos-to-time.c',
            'flt-buffer.c',
            'flt-error.c',
            'flt-file-error.c',
            'flt-gpx.c',
            'flt-parse-time.c',
            'flt-util.c'],
           dependencies: [m_dep, expat_dep])

executable('test-map-renderer',
           ['test-map-renderer.c',
            'flt-util.c',
            'flt-list.c',
            'flt-buffer.c',
            'flt-file-error.c',
            'flt-error.c',
            'flt-map-renderer.c',
            'flt-gpx.c',
            'flt-error.c',
            'flt-parse-time.c'],
            dependencies: [cairo_dep, m_dep, curl_dep, expat_dep])

test_lexer_src = [
        'flt-util.c',
        'flt-error.c',
        'flt-buffer.c',
        'test-lexer.c',
        'flt-utf8.c',
        'flt-lexer.c',
]
test_lexer = executable('test-lexer', test_lexer_src)
test('lexer', test_lexer)

executable('time-to-pos',
           ['flt-buffer.c',
            'flt-child-proc.c',
            'flt-error.c',
            'flt-file-error.c',
            'flt-get-video-length.c',
            'flt-gpx.c',
            'flt-lexer.c',
            'flt-parse-time.c',
            'flt-utf8.c',
            'flt-util.c',
            'time-to-pos.c'],
           dependencies: [m_dep, expat_dep])

executable('photos',
           ['flt-gpx.c',
            'flt-parse-time.c',
            'flt-error.c',
            'flt-util.c',
            'flt-buffer.c',
            'flt-file-error.c',
            'flt-list.c',
            'flt-get-video-length.c',
            'flt-child-proc.c',
            'photos.c'],
           dependencies: [m_dep, expat_dep])

executable('trace-to-gpx',
           ['flt-trace.c',
            'flt-buffer.c',
            'flt-error.c',
            'flt-file-error.c',
            'flt-util.c',
            'trace-to-gpx.c'],
           dependencies: [json_dep])
