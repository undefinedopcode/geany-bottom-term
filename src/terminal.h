#ifndef BT_TERMINAL_H
#define BT_TERMINAL_H

#include <gtk/gtk.h>

GtkWidget  *terminal_new(const gchar *working_directory);
GtkWidget  *terminal_new_run_command(const gchar *command, const gchar *working_directory);
gchar      *terminal_get_working_directory(void);
void        terminal_set_font(GtkWidget *term, const gchar *font_str);
void        terminal_set_colors(GtkWidget *term, const GdkRGBA *fg,
                                const GdkRGBA *bg, const GdkRGBA *palette,
                                gsize palette_size);

#endif /* BT_TERMINAL_H */
