#ifndef PLUMA_TERMINAL_PLUGIN_H
#define PLUMA_TERMINAL_PLUGIN_H
#include <libpeas/peas-extension-base.h>
#include <libpeas/peas-object-module.h>
G_BEGIN_DECLS
#define PLUMA_TYPE_TERMINAL_PLUGIN (pluma_terminal_plugin_get_type ())
#define PLUMA_TERMINAL_PLUGIN(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), PLUMA_TYPE_TERMINAL_PLUGIN, PlumaTerminalPlugin))
typedef struct _PlumaTerminalPlugin PlumaTerminalPlugin;
typedef struct _PlumaTerminalPluginClass PlumaTerminalPluginClass;
struct _PlumaTerminalPluginClass { PeasExtensionBaseClass parent_class; };
GType pluma_terminal_plugin_get_type (void) G_GNUC_CONST;
G_MODULE_EXPORT void peas_register_types (PeasObjectModule *module);
G_END_DECLS
#endif
