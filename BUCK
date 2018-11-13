cxx_library(
  name = 'ogg',
  header_namespace = '',
  exported_headers = {
    'ogg/ogg.h': 'include/ogg/ogg.h',
    'ogg/os_types.h': 'include/ogg/os_types.h',
  },
  exported_platform_headers = [
    ('macos.*', {
      'ogg/config_types.h': 'include/ogg/config_types.macos.h', 
    }), 
  ], 
  platform_headers = [
    ('macos.*', {
      'config.h': 'config.macos.h', 
    }), 
  ], 
  srcs = glob([
    'src/*.c',
  ]),
  visibility = [
    'PUBLIC',
  ],
)
