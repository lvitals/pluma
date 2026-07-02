#ifndef PLUMA_EXTERNALTOOLS_PLUGIN_H
#define PLUMA_EXTERNALTOOLS_PLUGIN_H

#include <glib.h>
#include <glib-object.h>
#include <libpeas/peas-extension-base.h>
#include <libpeas/peas-object-module.h>

G_BEGIN_DECLS

#define PLUMA_TYPE_EXTERNALTOOLS_PLUGIN         (pluma_externaltools_plugin_get_type ())
#define PLUMA_EXTERNALTOOLS_PLUGIN(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), PLUMA_TYPE_EXTERNALTOOLS_PLUGIN, PlumaExternalToolsPlugin))
#define PLUMA_EXTERNALTOOLS_PLUGIN_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), PLUMA_TYPE_EXTERNALTOOLS_PLUGIN, PlumaExternalToolsPluginClass))
#define PLUMA_IS_EXTERNALTOOLS_PLUGIN(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), PLUMA_TYPE_EXTERNALTOOLS_PLUGIN))
#define PLUMA_IS_EXTERNALTOOLS_PLUGIN_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), PLUMA_TYPE_EXTERNALTOOLS_PLUGIN))
#define PLUMA_EXTERNALTOOLS_PLUGIN_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), PLUMA_TYPE_EXTERNALTOOLS_PLUGIN, PlumaExternalToolsPluginClass))

typedef struct _PlumaExternalToolsPlugin        PlumaExternalToolsPlugin;
typedef struct _PlumaExternalToolsPluginClass   PlumaExternalToolsPluginClass;
typedef struct _PlumaExternalToolsPluginPrivate PlumaExternalToolsPluginPrivate;

struct _PlumaExternalToolsPlugin {
    PeasExtensionBase parent_instance;
    PlumaExternalToolsPluginPrivate *priv;
};

struct _PlumaExternalToolsPluginClass {
    PeasExtensionBaseClass parent_class;
};

GType pluma_externaltools_plugin_get_type (void) G_GNUC_CONST;
G_MODULE_EXPORT void peas_register_types (PeasObjectModule *module);

G_END_DECLS

#endif
