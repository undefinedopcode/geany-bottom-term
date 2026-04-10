#ifndef BT_TAB_MANAGER_H
#define BT_TAB_MANAGER_H

#include <gtk/gtk.h>

GtkWidget  *tab_manager_new(void);
void        tab_manager_add_tab(GtkNotebook *notebook);
void        tab_manager_add_run_tab(GtkNotebook *notebook, const gchar *command,
                                    const gchar *working_dir, const gchar *label);
void        tab_manager_add_tmux_tab(GtkNotebook *notebook, GtkWidget *vte,
                                    const gchar *title);
void        tab_manager_close_tab(GtkNotebook *notebook, gint page_num);
void        tab_manager_set_tab_title(GtkNotebook *notebook, GtkWidget *page,
                                      const gchar *title);

#endif /* BT_TAB_MANAGER_H */
