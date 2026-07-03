Source: libpeas 1.38 branch, `loaders/lua5.1/` (gitlab.gnome.org/GNOME/libpeas).

Vendored here for the same reason `plugins/pythonloader/` vendors
libpeas' Python 3 loader: build_bundled_python_loader in the top-level
`meson.build` does the same "the system doesn't ship a usable one, build
our own copy" thing for Python. For Lua the problem isn't that the
system loader is missing - `liblua51loader.so` usually exists - it's
that it's unsafe to use: it dlopen()s the system's `lgi` (lua51-lgi),
whose C core is linked against the legacy `libgirepository-1.0`. Pluma,
libpeas, and modern gobject-introspection all use `libgirepository-2.0`;
loading both in one process crashes with "cannot register existing type
'GIRepository'" the instant a `Loader=lua5.1` plugin is actually probed
- not just when one is activated.

Functional patches on top of the unmodified libpeas source:

1. The hardcoded `require 'lgi'` - once in C
   (`peas_lua_utils_require (L, "lgi")` in
   `peas-plugin-loader-lua.c`) and once in the embedded
   `resources/peas-lua-internal.lua` GResource - changed to
   `require 'LuaGObject'`, a fork of lgi that's actually built against
   `girepository-2.0`. LuaGObject itself is `third_party/LuaGObject`, a
   **pristine, unmodified git submodule** pinned to
   `github.com/vtrlx/LuaGObject` (see the repo root README's "Lua plugin
   loader" section for the clone instructions) - it is never patched
   in-tree; see point 3 below for how the one behavioral change it needs
   is applied without touching it.
2. A new `PEAS_LUA_EXTRA_PATH` C macro (baked in at build time to Pluma's
   private runtime dir) plus a small `luaL_dostring()` call injected
   right after `luaL_openlibs()` in `peas_plugin_loader_lua_initialize()`,
   so this loader's Lua state finds the bundled LuaGObject instead of any
   system-wide Lua install. `meson.build`/`Makefile.am` in this directory
   build LuaGObject's C core and install its Lua files directly from
   `third_party/LuaGObject/LuaGObject/*` by path (mirroring each other),
   instead of using LuaGObject's own build system, since that installs to
   system-wide Lua paths we don't want and the submodule can't carry a
   local patch to redirect it.

The two vendored private headers (`libpeas/peas-plugin-loader.h`,
`libpeas/peas-plugin-info-priv.h`) also needed their
`#include "peas-plugin-info.h"` changed to
`#include <libpeas/peas-plugin-info.h>` - this libpeas source revision's
quoted include didn't resolve the same way `plugins/pythonloader`'s older
vendored copy does; the angle-bracket form matches the public header
actually installed under `/usr/include/libpeas-1.0/libpeas/`.

`LGI_MAJOR_VERSION`/`_MINOR_VERSION`/`_MICRO_VERSION` are hardcoded to
`0`/`10`/`5` (both here and in `meson.build`) to match the vendored
LuaGObject's `_VERSION` - `peas_lua_utils_check_version()` just compares
this against the string the required module reports at runtime, nothing
more.
