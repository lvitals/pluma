--[[
  init.lua -- plugin object for the Lua console plugin.

  Loaded by libpeas' native "lua5.1" loader (Loader=lua5.1 in
  luaconsole.plugin). Mirrors pythonconsole's __init__.py: a
  Pluma.WindowActivatable that docks a REPL widget in the bottom panel, plus
  PeasGtk.Configurable for the preferences dialog.
]]

local lgi = require('luaconsole.compat')
local GObject = lgi.GObject
local Gtk = lgi.Gtk
local PeasGtk = lgi.PeasGtk
local Pluma = lgi.Pluma

local Console = require('luaconsole.console')
local ConfigWidget = require('luaconsole.config').ConfigWidget

local LUA_ICON = 'utilities-terminal-symbolic'

local LuaConsolePlugin = GObject.Object:derive('LuaConsolePlugin',
    { Pluma.WindowActivatable, PeasGtk.Configurable })

LuaConsolePlugin._property.window = GObject.ParamSpecObject('window', 'window', 'window',
    Pluma.Window._gtype, { GObject.ParamFlags.READABLE, GObject.ParamFlags.WRITABLE })

function LuaConsolePlugin:do_activate()
    self.priv.console = Console.new({ pluma = Pluma, window = self.window })

    local bottom = self.window:get_bottom_panel()
    local image = Gtk.Image()
    image:set_from_icon_name(LUA_ICON, Gtk.IconSize.MENU)
    bottom:add_item(self.priv.console.widget, 'Lua Console', image)
end

function LuaConsolePlugin:do_deactivate()
    self.priv.console:stop()
    local bottom = self.window:get_bottom_panel()
    bottom:remove_item(self.priv.console.widget)
    self.priv.console = nil
end

function LuaConsolePlugin:do_update_state()
end

function LuaConsolePlugin:do_create_configure_widget()
    if not self.priv.config_widget then
        self.priv.config_widget = ConfigWidget.new(self.priv.plugin_info:get_data_dir())
    end
    return self.priv.config_widget:configure_widget()
end

return { LuaConsolePlugin }
