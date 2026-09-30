# Copyright 2018 Rob Clark
# Copyright 2019 Collabora, Ltd.
# SPDX-License-Identifier: MIT

# This is used only for the r/e tooling on macOS
dep_iokit = dependency('IOKit', required : false)

inc_asahi = include_directories([
   '.', 'layout', 'lib', 'genxml', 'compiler'
])

subdir('isa')

with_asahi_d3d12 = get_option('asahi-d3d12')
with_asahi_common = with_gallium_asahi or with_asahi_vk or \
                    with_asahi_d3d12 or with_tools.contains('asahi')

if with_asahi_common
   subdir('genxml')
   subdir('libagx')
   subdir('compiler')
   if with_asahi_vk
      subdir('compiler_neo')
   endif
   subdir('clc')
endif

if with_gallium_asahi or with_asahi_vk or with_asahi_d3d12
   subdir('layout')
   subdir('lib')
elif dep_iokit.found()
   # Just build enough for libwrap.dylib
   if not with_asahi_common
      subdir('genxml')
   endif
   subdir('lib')
endif

if with_asahi_vk
   subdir('vulkan')
endif

if with_asahi_d3d12
   subdir('d3d12')
endif

if with_tools.contains('drm-shim')
  subdir('drm-shim')
endif
