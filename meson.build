project('flootay', 'c')

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

executable('flootay',
           ['flootay.c',
            'flt-buffer.c',
            'flt-error.c',
            'flt-file-error.c',
            'flt-gpx.c',
            'flt-lexer.c',
            'flt-list.c',
            'flt-map-renderer.c',
            'flt-parser.c',
            'flt-scene.c',
            'flt-utf8.c',
            'flt-util.c'],
           dependencies: [m_dep, cairo_dep, rsvg_dep, expat_dep])

executable('generate-logo',
           ['generate-logo.c',
            'flt-util.c'],
           dependencies: [m_dep, cairo_dep, rsvg_dep, expat_dep])

executable('make-key-frames',
           ['make-key-frames.c',
            'flt-buffer.c',
            'flt-util.c'],
           dependencies: [sdl_dep, sdl_image_dep])

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
            'flt-list.c'],
            dependencies: [m_dep])

executable('test-map-renderer',
           ['test-map-renderer.c',
            'flt-util.c',
            'flt-list.c',
            'flt-buffer.c',
            'flt-file-error.c',
            'flt-error.c',
            'flt-map-renderer.c'],
            dependencies: [cairo_dep, m_dep])
