#ifndef BT_PLUGIN_H
#define BT_PLUGIN_H

#include <geanyplugin.h>
#include <vte/vte.h>
#include "colors.h"

typedef struct {
    GeanyPlugin *geany_plugin;

    /* Reparenting state */
    GtkWidget   *original_parent;
    GtkWidget   *paned;
    gboolean     was_child1;

    /* Tab manager */
    GtkWidget   *tab_notebook;

    /* Menu */
    GtkWidget   *menu_item;
    GtkWidget   *run_menu_item;

    /* Lifecycle */
    gboolean     shutting_down; /* TRUE during cleanup — suppresses child-exited actions */

    /* Settings */
    gchar       *font;          /* Pango font string, e.g. "Monospace 10" */
    gchar       *config_file;   /* path to settings ini */
    gchar       *scheme_name;   /* name of preset (or "Custom") */
    gint         paned_pos;     /* saved paned split position (-1 = auto) */
    GdkRGBA      fg;
    GdkRGBA      bg;
    GdkRGBA      palette[BT_PALETTE_SIZE];
} BottomTermPlugin;

extern GeanyPlugin *geany_plugin;
extern GeanyData   *geany_data;

extern BottomTermPlugin *bt_plugin;

#endif /* BT_PLUGIN_H */
