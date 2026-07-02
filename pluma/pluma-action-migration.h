#ifndef PLUMA_ACTION_MIGRATION_H
#define PLUMA_ACTION_MIGRATION_H

#include <gio/gio.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct
{
	const gchar *legacy_name;
	const gchar *action_name;
	const gchar * const *accelerators;
} PlumaLegacyActionMapping;

void pluma_action_migration_mirror_group (GtkApplication                 *application,
	                                      GActionMap                     *target,
	                                      GtkActionGroup                 *legacy_group,
	                                      const PlumaLegacyActionMapping *mappings,
	                                      gsize                           n_mappings);

GSimpleActionGroup *pluma_action_migration_create_plugin_group
	                                     (const GActionEntry *entries,
	                                      gsize               n_entries,
	                                      gpointer            user_data);
void pluma_action_migration_insert_plugin_group (GtkWidget          *window,
	                                             const gchar        *prefix,
	                                             GSimpleActionGroup *group);
GMenuModel *pluma_action_migration_load_menu (const gchar *filename,
	                                          const gchar *object_id,
	                                          GError     **error);

G_END_DECLS
#endif
