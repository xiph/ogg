genrule(
  name = "config",
  srcs = select({
    "@bazel_tools//src/conditions:linux_x86_64": [
      "include/ogg/config_types.linux.h",
    ],
    "@bazel_tools//src/conditions:darwin_x86_64": [
      "include/ogg/config_types.macos.h",
    ],
    "//conditions:default": [
      "include/ogg/config_types.linux.h",
    ],
  }),
  outs = [
    "include/ogg/config_types.h",
  ],
  cmd = "cp $< $@",
)

cc_library(
  name = "ogg",
  includes = [
    "include",
  ],
  hdrs = glob([
    "include/**/*.h",
  ]) + [
    ":config",
  ],
  srcs = glob([
    "src/**/*.h",
    "src/**/*.c",
  ]),
  visibility = [
    "//visibility:public",
  ],
)
