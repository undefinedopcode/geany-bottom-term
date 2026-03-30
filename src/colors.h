#ifndef BT_COLORS_H
#define BT_COLORS_H

#include <gdk/gdk.h>

#define BT_PALETTE_SIZE 16

typedef struct {
    const gchar *name;
    GdkRGBA      fg;
    GdkRGBA      bg;
    GdkRGBA      palette[BT_PALETTE_SIZE];
} BtColorScheme;

/* Number of built-in schemes */
gint                bt_color_schemes_count(void);

/* Get a built-in scheme by index */
const BtColorScheme *bt_color_scheme_get(gint index);

/* Find a built-in scheme by name; returns -1 if not found */
gint                bt_color_scheme_find(const gchar *name);

/* ANSI color labels for the 16 palette entries */
const gchar        *bt_color_label(gint index);

/* Import colors from a Geany color scheme .conf file.
 * scheme_file: filename like "bespin.conf" (or NULL/empty for default)
 * geany_config_dir: ~/.config/geany
 * Returns TRUE on success, filling fg, bg, and palette. */
gboolean bt_import_geany_theme(const gchar *scheme_file,
                               const gchar *geany_config_dir,
                               GdkRGBA *fg, GdkRGBA *bg,
                               GdkRGBA palette[BT_PALETTE_SIZE]);

#endif /* BT_COLORS_H */
