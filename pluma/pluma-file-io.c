#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <glib/gi18n.h>
#include "pluma-file-io.h"
#include "pluma-settings.h"

static GSettings *
get_settings (void)
{
	static GSettings *settings = NULL;
	if (g_once_init_enter (&settings))
	{
		GSettings *s = g_settings_new (PLUMA_SCHEMA_ID);
		g_once_init_leave (&settings, s);
	}
	return settings;
}

gchar *
pluma_file_read_with_encoding (const gchar          *path,
                               gsize                *out_len,
                               const PlumaEncoding **matched_encoding,
                               GError              **error)
{
	gchar *raw_contents = NULL;
	gsize raw_len = 0;

	if (!g_file_get_contents (path, &raw_contents, &raw_len, error))
		return NULL;

	const PlumaEncoding *found_enc = NULL;
	gchar *utf8_text = NULL;
	gsize utf8_len = 0;

	/* 0. Try UTF-16 BOM first to prevent false-positives with single-byte encodings */
	if (raw_len >= 2 && (guchar)raw_contents[0] == 0xff && (guchar)raw_contents[1] == 0xfe)
	{
		found_enc = pluma_encoding_get_from_charset ("UTF-16LE");
	}
	else if (raw_len >= 2 && (guchar)raw_contents[0] == 0xfe && (guchar)raw_contents[1] == 0xff)
	{
		found_enc = pluma_encoding_get_from_charset ("UTF-16BE");
	}

	if (found_enc != NULL)
	{
		const gchar *charset = pluma_encoding_get_charset (found_enc);
		GError *conv_err = NULL;
		gsize bytes_read = 0;
		gsize bytes_written = 0;
		gchar *converted = g_convert (raw_contents, raw_len, "UTF-8", charset, &bytes_read, &bytes_written, &conv_err);
		if (converted != NULL)
		{
			utf8_text = converted;
			utf8_len = bytes_written;
		}
		g_clear_error (&conv_err);
	}
	else if (g_utf8_validate (raw_contents, raw_len, NULL))
	{
		found_enc = pluma_encoding_get_utf8 ();
		utf8_text = g_memdup2 (raw_contents, raw_len + 1);
		utf8_text[raw_len] = '\0';
		utf8_len = raw_len;
	}
	else
	{
		/* 2. Try auto-detected encodings list from GSettings */
		GSettings *settings = get_settings ();
		gchar **enc_strv = g_settings_get_strv (settings, "auto-detected-encodings");
		GSList *encodings = _pluma_encoding_strv_to_list ((const gchar * const *) enc_strv);
		g_strfreev (enc_strv);

		for (GSList *l = encodings; l != NULL; l = l->next)
		{
			const PlumaEncoding *enc = (const PlumaEncoding *) l->data;
			const gchar *charset = pluma_encoding_get_charset (enc);

			/* Skip UTF-8 since we already checked it */
			if (g_ascii_strcasecmp (charset, "UTF-8") == 0)
				continue;

			GError *conv_err = NULL;
			gsize bytes_read = 0;
			gsize bytes_written = 0;
			gchar *converted = g_convert (raw_contents, raw_len, "UTF-8", charset, &bytes_read, &bytes_written, &conv_err);
			if (converted != NULL && bytes_read == raw_len)
			{
				found_enc = enc;
				utf8_text = converted;
				utf8_len = bytes_written;
				g_clear_error (&conv_err);
				break;
			}
			g_free (converted);
			g_clear_error (&conv_err);
		}
		g_slist_free (encodings);
	}

	if (utf8_text == NULL)
	{
		g_free (raw_contents);
		g_set_error (error, G_CONVERT_ERROR, G_CONVERT_ERROR_NO_CONVERSION,
		             _("Could not detect the file encoding."));
		return NULL;
	}

	if (out_len != NULL)
		*out_len = utf8_len;
	if (matched_encoding != NULL)
		*matched_encoding = found_enc;

	return utf8_text;
}

gboolean
pluma_file_write_with_encoding (const gchar         *path,
                                const gchar         *contents,
                                const PlumaEncoding *encoding,
                                GError             **error)
{
	gchar *raw_out = NULL;
	gsize raw_out_len = 0;
	gboolean success = FALSE;

	if (encoding == NULL)
		encoding = pluma_encoding_get_utf8 ();

	const gchar *charset = pluma_encoding_get_charset (encoding);

	if (g_ascii_strcasecmp (charset, "UTF-8") == 0)
	{
		raw_out = (gchar *) contents;
		raw_out_len = strlen (contents);
	}
	else
	{
		raw_out = g_convert (contents, strlen (contents), charset, "UTF-8", NULL, &raw_out_len, error);
		if (raw_out == NULL)
			return FALSE;
	}

	struct stat stat_buf;
	gboolean has_stat = (stat (path, &stat_buf) == 0);

	success = g_file_set_contents (path, raw_out, raw_out_len, error);

	if (raw_out != contents)
		g_free (raw_out);

	if (success && has_stat)
	{
		if (chmod (path, stat_buf.st_mode & 07777) != 0)
		{
			g_warning ("Failed to restore permissions (chmod) for %s: %s", path, g_strerror (errno));
		}
		if (chown (path, stat_buf.st_uid, stat_buf.st_gid) != 0)
		{
			/* ignore chown errors */
		}
	}

	return success;
}

gchar *
pluma_file_bytes_to_utf8 (GBytes *bytes)
{
	if (bytes == NULL)
		return NULL;

	gsize size = 0;
	const gchar *data = g_bytes_get_data (bytes, &size);
	gchar *utf8 = NULL;

	if (size > 0)
		utf8 = g_utf8_make_valid (data, size);
	else
		utf8 = g_strdup ("");

	g_bytes_unref (bytes);
	return utf8;
}
