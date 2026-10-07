project('slatekbd', 'c',
  version : '0.1.0',
  license : 'MIT',
  meson_version : '>=0.60',
  default_options : ['c_std=c11', 'warning_level=2', 'buildtype=debugoptimized'])

cc = meson.get_compiler('c')
add_project_arguments('-D_GNU_SOURCE',
  '-DSLATEKBD_VERSION="@0@"'.format(meson.project_version()), language : 'c')

wayland_client = dependency('wayland-client')
wayland_protos = dependency('wayland-protocols')
xkbcommon = dependency('xkbcommon')
cairo = dependency('cairo')
pango = dependency('pango')
pangocairo = dependency('pangocairo')
libm = cc.find_library('m', required : false)

scanner_dep = dependency('wayland-scanner', native : true)
wayland_scanner = find_program(scanner_dep.get_variable('wayland_scanner'))
wp_dir = wayland_protos.get_variable('pkgdatadir')

protocol_xmls = [
  'protocols/wlr-layer-shell-unstable-v1.xml',
  'protocols/virtual-keyboard-unstable-v1.xml',
  'protocols/input-method-unstable-v2.xml',
  'protocols/hyprland-lock-notify-v1.xml',
  wp_dir / 'stable/xdg-shell/xdg-shell.xml',
  wp_dir / 'stable/viewporter/viewporter.xml',
  wp_dir / 'staging/fractional-scale/fractional-scale-v1.xml',
  wp_dir / 'unstable/xdg-output/xdg-output-unstable-v1.xml',
]

proto_src = []
proto_hdr = []
foreach xml : protocol_xmls
  base = xml.split('/')[-1].replace('.xml', '')
  proto_hdr += custom_target(base + '-client-header',
    input : xml, output : base + '-client-protocol.h',
    command : [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
  proto_src += custom_target(base + '-private-code',
    input : xml, output : base + '-protocol.c',
    command : [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
endforeach

proto_lib = static_library('slatekbd-protocols', proto_src + proto_hdr,
  dependencies : [wayland_client])
proto_dep = declare_dependency(link_with : proto_lib, sources : proto_hdr)

# Pure-logic core: no Wayland calls; shared by the program and the unit tests.
core_lib = static_library('slatekbd-core',
  'src/layout.c', 'src/input.c', 'src/config.c', 'src/visibility.c',
  dependencies : [libm])
core_dep = declare_dependency(link_with : core_lib, include_directories : include_directories('src'))

slatekbd_src = files(
  'src/main.c', 'src/instance.c', 'src/loop.c', 'src/wayland.c', 'src/shm.c',
  'src/render.c', 'src/keymap.c', 'src/settings.c', 'src/im.c', 'src/rotate.c',
  'src/lock.c', 'src/app.c',
)

executable('slatekbd', slatekbd_src,
  dependencies : [core_dep, proto_dep, wayland_client, xkbcommon, cairo, pango, pangocairo, libm],
  install : true)

install_data('slatekbd.conf.example', install_dir : get_option('datadir') / 'doc' / 'slatekbd')
# login screen (greetd): noctalia-greeter in a minimal Hyprland with the keyboard button
install_data('greeter/hyprland.lua', install_dir : get_option('datadir') / 'slatekbd', rename : 'greeter-hyprland.lua')
install_data('greeter/slatekbd-greeter-session', install_dir : get_option('bindir'), install_mode : 'rwxr-xr-x')

# ---- unit tests (no Wayland needed) ----
test_deps = [core_dep, xkbcommon, libm]
foreach t : ['layout', 'geometry', 'input', 'config', 'visibility']
  exe = executable('test_' + t, 'tests/unit/test_' + t + '.c',
    dependencies : test_deps, include_directories : include_directories('tests/unit'))
  test(t, exe)
endforeach
