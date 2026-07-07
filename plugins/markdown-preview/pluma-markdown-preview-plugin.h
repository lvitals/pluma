#ifndef PLUMA_MARKDOWN_PREVIEW_PLUGIN_H
#define PLUMA_MARKDOWN_PREVIEW_PLUGIN_H

#include <libpeas/peas-extension-base.h>
#include <libpeas/peas-object-module.h>

G_BEGIN_DECLS

#define PLUMA_TYPE_MARKDOWN_PREVIEW_PLUGIN (pluma_markdown_preview_plugin_get_type ())
typedef struct _PlumaMarkdownPreviewPlugin PlumaMarkdownPreviewPlugin;
typedef struct _PlumaMarkdownPreviewPluginClass PlumaMarkdownPreviewPluginClass;

struct _PlumaMarkdownPreviewPluginClass
{
    PeasExtensionBaseClass parent_class;
};

#define PLUMA_MARKDOWN_PREVIEW_PLUGIN(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), PLUMA_TYPE_MARKDOWN_PREVIEW_PLUGIN, PlumaMarkdownPreviewPlugin))

GType pluma_markdown_preview_plugin_get_type (void) G_GNUC_CONST;

G_MODULE_EXPORT void peas_register_types (PeasObjectModule *module);

G_END_DECLS

#endif
