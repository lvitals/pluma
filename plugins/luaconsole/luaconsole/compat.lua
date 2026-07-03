--[[
  compat.lua -- must be required (instead of 'LuaGObject' directly)
  before anything in this plugin touches LuaGObject.Gdk or .Gtk.

  Pluma's bundled lua5.1 plugin loader (plugins/lualoader) already points
  package.path/cpath at a private, vendored build of LuaGObject (a lgi
  fork built against girepository-2.0 - see subprojects/LuaGObject and
  plugins/lualoader/VENDORED.md for why: the distro's lgi links the
  legacy libgirepository-1.0 and crashes the whole process if loaded
  alongside Pluma's libgirepository-2.0). So this module doesn't need to
  manipulate package.path itself, just require the module.

  It does still need to pin the GTK/GDK version: this system has both
  Gtk-3.0 and Gtk-4.0 typelibs installed, and an unpinned require
  resolves to whichever is newest (4.0). Pluma itself links GTK3
  (libgtk-3.so), so an unpinned LuaGObject.Gtk here would build widgets
  from an ABI Pluma's own bottom panel can't hold.
]]

local LuaGObject = require('LuaGObject')
LuaGObject.require('Gtk', '3.0')
LuaGObject.require('Gdk', '3.0')

return LuaGObject
