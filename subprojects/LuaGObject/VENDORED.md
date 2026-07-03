Vendored from https://github.com/vtrlx/LuaGObject
commit 63c8099190c8830e9ba40dba1e48d7b000c7714e (2026-02-04), version 0.10.5.

LuaGObject is a maintained fork of lgi (https://github.com/lgi-devs/lgi)
ported to GIRepository-2 (>= 2.80.0). It's vendored here because:

- lgi itself (packaged by distros as lua51-lgi/lua53-lgi/lua54-lgi) is
  still linked against the legacy, no-longer-built-against
  `libgirepository-1.0` compat library, while Pluma, libpeas, and modern
  gobject-introspection all use `libgirepository-2.0`. Loading both in
  one process crashes on "cannot register existing type 'GIRepository'"
  the moment libpeas' lua5.1 loader actually runs — see the "Lua plugin
  support" project memory for the full diagnosis.
- LuaGObject fixes this by being built against girepository-2.0 from the
  start, eliminating the double-registration entirely.

Only the pieces needed to build `lua_gobject_core.so` + the pure-Lua
runtime are kept (docs/, samples/, tools/, the GNU Makefile, and the
luarocks rockspec were dropped; this tree is built via
`plugins/lualoader/meson.build` as a Meson subproject, and referenced
directly by relative path from `plugins/lualoader/Makefile.am` for the
Autotools build). Neither build installs it to any system-wide Lua path
- both install into Pluma's own private plugin loader runtime dir via
the `runtime-install-dir` Meson option added to `meson.build` and
`LuaGObject/meson.build` (empty by default, meaning "install to the
normal system Lua paths" for anyone building this subproject tree
standalone).

Functional patch beyond path/install-dir plumbing: `override/Gtk3.lua`
and `override/Gtk4.lua` both unconditionally call `Gtk.disable_setlocale()`
before `Gtk.init_check()`, assuming LuaGObject itself is what's
initializing GTK. When loaded as a Pluma plugin, Pluma's own `gtk_init()`
already ran long before any plugin loads, so that call is a late no-op
that only produces a "gtk_disable_setlocale() must be called before
gtk_init()" warning. Both files now skip `disable_setlocale()` (but still
call `init_check()`, which is safe to call again) when
`Gdk.Display.get_default()` already returns non-nil - a reliable sign
gtk_init() already happened elsewhere.

To update: replace `LuaGObject.lua`, `LuaGObject/`, `meson.build`,
`meson_options.txt` with a fresh checkout, then re-apply the
`runtime-install-dir` option (both `meson.build` files) and the
`Gdk.Display.get_default()` guard in `override/Gtk3.lua` /
`override/Gtk4.lua` (search this tree for `runtime-install-dir` and
`Gdk.Display.get_default`). `plugins/lualoader/Makefile.am`'s file lists
(`luagobject_srcdir`-relative paths) will also need updating if upstream
adds/removes/renames any source files.
