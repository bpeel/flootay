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

executable('flootay',
           ['flootay.c',
            'flt-util.c'],
           dependencies: [m_dep, cairo_dep])

executable('make-key-frames',
           ['make-key-frames.c'],
           dependencies: [sdl_dep, sdl_image_dep])
