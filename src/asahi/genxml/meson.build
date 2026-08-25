# Copyright 2017 Intel Corporation
# SPDX-License-Identifier: MIT

agx_pack = custom_target(
  input : ['gen_pack.py', 'cmdbuf.xml', 'agx_pack_header.h'],
  output : 'agx_pack.h',
  command : [prog_python, '@INPUT@'],
  capture : true,
)

idep_agx_pack = declare_dependency(
  sources : [agx_pack, files('agx3_cmdstream.h')],
  dependencies : dep_valgrind,
  include_directories : include_directories('.'),
)

if with_tests
  test(
    'asahi-agx3-packing',
    executable(
      'asahi-agx3-packing',
      files('tests/test-agx3-packing.c'),
      include_directories : [inc_src, inc_asahi],
      dependencies : [idep_agx_pack, idep_mesautil],
    ),
    suite : ['asahi'],
  )
endif
