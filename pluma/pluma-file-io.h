#ifndef __PLUMA_FILE_IO_H__
#define __PLUMA_FILE_IO_H__

#include <glib.h>
#include "pluma-encodings.h"

G_BEGIN_DECLS

gchar *pluma_file_read_with_encoding (const gchar          *path,
                                      gsize                *out_len,
                                      const PlumaEncoding **matched_encoding,
                                      GError              **error);

gboolean pluma_file_write_with_encoding (const gchar         *path,
                                         const gchar         *contents,
                                         const PlumaEncoding *encoding,
                                         GError             **error);

/* Converts GBytes to a valid UTF-8 string, consuming/unref'ing the GBytes reference. */
gchar *pluma_file_bytes_to_utf8 (GBytes *bytes);

G_END_DECLS

#endif /* __PLUMA_FILE_IO_H__ */
