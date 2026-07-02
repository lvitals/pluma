#ifndef PLUMA_SNIPPETS_PLUGIN_H
#define PLUMA_SNIPPETS_PLUGIN_H

#include <libpeas/peas-extension-base.h>
#include <libpeas/peas-object-module.h>

G_BEGIN_DECLS

#define PLUMA_TYPE_SNIPPETS_PLUGIN (pluma_snippets_plugin_get_type ())
#define PLUMA_SNIPPETS_PLUGIN(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), PLUMA_TYPE_SNIPPETS_PLUGIN, PlumaSnippetsPlugin))

typedef struct _PlumaSnippetsPlugin PlumaSnippetsPlugin;
typedef struct _PlumaSnippetsPluginClass PlumaSnippetsPluginClass;

struct _PlumaSnippetsPluginClass { PeasExtensionBaseClass parent_class; };

GType pluma_snippets_plugin_get_type (void) G_GNUC_CONST;
G_MODULE_EXPORT void peas_register_types (PeasObjectModule *module);

G_END_DECLS
#endif
