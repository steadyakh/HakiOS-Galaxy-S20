# Copyright © 2026 Darwin on redwood contributors
# SPDX-License-Identifier: MIT
#
# E1 (z3sios) AIR frontend, vendored for the libredwoodair trial. Every target
# is renamed from the upstream `air` names so this directory can be built in
# the same Mesa tree as our own AIR frontend without a target-name collision.
# Copied to src/compiler/redwoodair by the workflow; metal-e1/meson.patch adds
# the subdir() that reaches it.

files_redwoodair_fe = files(
  'air_bitcode.c',
  'air_bitcode.h',
  'air_layout.c',
  'air_metallib.c',
  'air_module.c',
  'air_module.h',
  'air_print.c',
  'air_private.h',
  'air_to_nir.c',
  'air_intrinsics.c',
  'nir_air.h',
)

libredwoodair_fe = static_library(
  'redwoodair_fe',
  files_redwoodair_fe,
  include_directories : [inc_include, inc_src],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  dependencies : [idep_nir, idep_mesautil],
  build_by_default : false,
)

idep_redwoodair = declare_dependency(
  dependencies : [idep_nir, idep_mesautil],
  link_with : libredwoodair_fe,
  include_directories : include_directories('.'),
)

# air_run.c and air_metal_test.c are consumers of this frontend; they need
# libgallium, which meson has not defined when this file is read. They are
# deliberately not built here. The Metal library (air_metal.c) is declared in
# src/gallium/drivers/panfrost/meson.build by metal-e1/meson.patch for the same
# reason, and links the dependency above.
