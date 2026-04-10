#include "tab_manager.h"
#include "plugin.h"
#include "terminal.h"
#include "tmux.h"

static gint tab_counter = 0;

/* --- Tab label with close button --- */

typedef struct {
    GtkNotebook *notebook;
    GtkWidget   *page;
} CloseData;

static void
on_close_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    CloseData *cd = user_data;

    if (!GTK_IS_NOTEBOOK(cd->notebook))
        return;

    gint page_num = gtk_notebook_page_num(cd->notebook, cd->page);
    if (page_num >= 0)
        tab_manager_close_tab(cd->notebook, page_num);
}

static void
on_close_data_destroy(gpointer data, GClosure *closure)
{
    (void)closure;
    g_free(data);
}

static GtkWidget *
tab_label_new(GtkNotebook *notebook, GtkWidget *page, gint tab_num)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

    gchar *title = g_strdup_printf("Term %d", tab_num);
    GtkWidget *label = gtk_label_new(title);
    g_free(title);
    gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);

    GtkWidget *btn = gtk_button_new_from_icon_name("window-close-symbolic",
                                                    GTK_ICON_SIZE_MENU);
    gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
    gtk_widget_set_focus_on_click(btn, FALSE);

    /* Shrink the close button */
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "button { min-width: 16px; min-height: 16px; padding: 0; margin: 0; }",
        -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(btn),
                                   GTK_STYLE_PROVIDER(css),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    CloseData *cd = g_new(CloseData, 1);
    cd->notebook = notebook;
    cd->page = page;

    g_signal_connect_data(btn, "clicked", G_CALLBACK(on_close_clicked),
                          cd, on_close_data_destroy, 0);

    gtk_box_pack_start(GTK_BOX(box), btn, FALSE, FALSE, 0);
    gtk_widget_show_all(box);

    return box;
}

/* --- Context menu on tab right-click --- */

static void
on_ctx_new_tab(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    GtkNotebook *nb = GTK_NOTEBOOK(user_data);
    tab_manager_add_tab(nb);
}

static void
on_ctx_close_tab(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    GtkNotebook *nb = GTK_NOTEBOOK(user_data);
    gint page = gtk_notebook_get_current_page(nb);
    if (page >= 0)
        tab_manager_close_tab(nb, page);
}

static void
on_ctx_close_others(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    GtkNotebook *nb = GTK_NOTEBOOK(user_data);
    gint current = gtk_notebook_get_current_page(nb);

    /* Remove pages from end to start, skipping current */
    for (gint i = gtk_notebook_get_n_pages(nb) - 1; i >= 0; i--) {
        if (i != current)
            gtk_notebook_remove_page(nb, i);
    }
    /* Adjust: if we still have a page, we're good. If not, add one. */
    if (gtk_notebook_get_n_pages(nb) == 0)
        tab_manager_add_tab(nb);
}

static gboolean
on_tab_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    (void)widget;

    if (event->button != 3)
        return FALSE;

    GtkNotebook *nb = GTK_NOTEBOOK(user_data);

    GtkWidget *menu = gtk_menu_new();

    GtkWidget *item_new = gtk_menu_item_new_with_label("New Terminal");
    g_signal_connect(item_new, "activate", G_CALLBACK(on_ctx_new_tab), nb);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_new);

    GtkWidget *item_close = gtk_menu_item_new_with_label("Close");
    g_signal_connect(item_close, "activate", G_CALLBACK(on_ctx_close_tab), nb);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_close);

    GtkWidget *item_others = gtk_menu_item_new_with_label("Close Others");
    g_signal_connect(item_others, "activate", G_CALLBACK(on_ctx_close_others), nb);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_others);

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);

    return TRUE;
}

/* --- "+" button callback --- */

static void
on_add_tab_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    GtkNotebook *nb = GTK_NOTEBOOK(user_data);
    tab_manager_add_tab(nb);
}

/* --- Public API --- */

GtkWidget *
tab_manager_new(void)
{
    GtkWidget *nb = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(nb), TRUE);
    gtk_notebook_popup_enable(GTK_NOTEBOOK(nb));

    /* "+" button as action widget */
    GtkWidget *add_btn = gtk_button_new_from_icon_name("list-add-symbolic",
                                                        GTK_ICON_SIZE_MENU);
    gtk_button_set_relief(GTK_BUTTON(add_btn), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(add_btn, "New terminal tab");
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_add_tab_clicked), nb);
    gtk_widget_show(add_btn);
    gtk_notebook_set_action_widget(GTK_NOTEBOOK(nb), add_btn, GTK_PACK_END);

    /* Right-click context menu on the tab bar area */
    g_signal_connect(nb, "button-press-event", G_CALLBACK(on_tab_button_press), nb);

    /* Start with one terminal tab */
    tab_manager_add_tab(GTK_NOTEBOOK(nb));

    return nb;
}

void
tab_manager_add_tab(GtkNotebook *notebook)
{
    gchar *cwd = terminal_get_working_directory();
    GtkWidget *term = terminal_new(cwd);
    g_free(cwd);

    tab_counter++;

    GtkWidget *label = tab_label_new(notebook, term, tab_counter);

    gtk_notebook_append_page(notebook, term, label);
    gtk_notebook_set_tab_reorderable(notebook, term, TRUE);
    gtk_widget_show_all(term);

    gint page_num = gtk_notebook_page_num(notebook, term);
    gtk_notebook_set_current_page(notebook, page_num);

    gtk_widget_grab_focus(term);
}

void
tab_manager_add_run_tab(GtkNotebook *notebook, const gchar *command,
                        const gchar *working_dir, const gchar *label)
{
    GtkWidget *term = terminal_new_run_command(command, working_dir);

    tab_counter++;

    const gchar *tab_title = (label && label[0]) ? label : "Run";
    GtkWidget *tab_label = tab_label_new(notebook, term, tab_counter);

    gtk_notebook_append_page(notebook, term, tab_label);
    gtk_notebook_set_tab_reorderable(notebook, term, TRUE);
    gtk_widget_show_all(term);

    /* Set the label text to the run title */
    tab_manager_set_tab_title(notebook, term, tab_title);

    gint page_num = gtk_notebook_page_num(notebook, term);
    gtk_notebook_set_current_page(notebook, page_num);
    gtk_widget_grab_focus(term);

    /* Make sure the terminal area is visible */
    if (bt_plugin && bt_plugin->tab_notebook &&
        !gtk_widget_get_visible(bt_plugin->tab_notebook))
        gtk_widget_show(bt_plugin->tab_notebook);
}

void
tab_manager_add_tmux_tab(GtkNotebook *notebook, GtkWidget *vte,
                         const gchar *title)
{
    tab_counter++;

    GtkWidget *label = tab_label_new(notebook, vte, tab_counter);

    /* Prepend a tmux icon to distinguish from regular tabs */
    GtkWidget *icon = gtk_image_new_from_icon_name("view-grid-symbolic",
                                                    GTK_ICON_SIZE_MENU);
    gtk_widget_set_margin_end(icon, 4);
    gtk_box_pack_start(GTK_BOX(label), icon, FALSE, FALSE, 0);
    gtk_box_reorder_child(GTK_BOX(label), icon, 0);
    gtk_widget_show(icon);

    gtk_notebook_append_page(notebook, vte, label);
    gtk_notebook_set_tab_reorderable(notebook, vte, TRUE);
    gtk_widget_show_all(vte);

    tab_manager_set_tab_title(notebook, vte, title);

    gint page_num = gtk_notebook_page_num(notebook, vte);
    gtk_notebook_set_current_page(notebook, page_num);
    gtk_widget_grab_focus(vte);
}

void
tab_manager_close_tab(GtkNotebook *notebook, gint page_num)
{
    /* Notify tmux if this is a tmux tab */
    GtkWidget *page = gtk_notebook_get_nth_page(notebook, page_num);
    if (page && bt_tmux_is_tmux_tab(page))
        bt_tmux_on_tab_closed(page);

    gtk_notebook_remove_page(notebook, page_num);

    /* Always keep at least one tab — but not during shutdown */
    if (bt_plugin && bt_plugin->shutting_down)
        return;

    if (gtk_notebook_get_n_pages(notebook) == 0)
        tab_manager_add_tab(notebook);
}

void
tab_manager_set_tab_title(GtkNotebook *notebook, GtkWidget *page,
                          const gchar *title)
{
    if (!notebook || !page || !title)
        return;

    GtkWidget *tab_box = gtk_notebook_get_tab_label(notebook, page);
    if (!tab_box || !GTK_IS_BOX(tab_box))
        return;

    /* Find the GtkLabel in the tab label box (may not be the first child
     * if a tmux icon was prepended). */
    GList *children = gtk_container_get_children(GTK_CONTAINER(tab_box));
    GtkWidget *label_widget = NULL;
    for (GList *l = children; l; l = l->next) {
        if (GTK_IS_LABEL(l->data)) {
            label_widget = l->data;
            break;
        }
    }
    if (label_widget) {
        /* Truncate long titles */
        gchar *short_title;
        if (g_utf8_strlen(title, -1) > 20) {
            gchar *truncated = g_utf8_substring(title, 0, 18);
            short_title = g_strdup_printf("%s\xe2\x80\xa6", truncated); /* ellipsis */
            g_free(truncated);
        } else {
            short_title = g_strdup(title);
        }

        gtk_label_set_text(GTK_LABEL(label_widget), short_title);
        /* Set full title as tooltip on the tab label box */
        gtk_widget_set_tooltip_text(tab_box, title);
        g_free(short_title);
    }
    g_list_free(children);
}
