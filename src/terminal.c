#include "terminal.h"
#include "plugin.h"
#include "tab_manager.h"

#include <sys/wait.h>

static void
on_spawn_callback(VteTerminal *terminal,
                  GPid         pid,
                  GError      *error,
                  gpointer     user_data)
{
    (void)terminal;
    (void)user_data;

    if (error) {
        g_warning("Failed to spawn terminal: %s", error->message);
        return;
    }

    g_debug("Terminal spawned with PID %d", (int)pid);
}

/* --- Right-click context menu --- */

static void
on_ctx_copy(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    vte_terminal_copy_clipboard_format(VTE_TERMINAL(user_data), VTE_FORMAT_TEXT);
}

static void
on_ctx_paste(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    vte_terminal_paste_clipboard(VTE_TERMINAL(user_data));
}

static void
on_ctx_select_all(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    vte_terminal_select_all(VTE_TERMINAL(user_data));
}

static void
on_ctx_clear(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    VteTerminal *term = VTE_TERMINAL(user_data);
    vte_terminal_reset(term, TRUE, TRUE);
}

static void
on_ctx_new_tab(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    (void)user_data;
    if (bt_plugin && bt_plugin->tab_notebook)
        tab_manager_add_tab(GTK_NOTEBOOK(bt_plugin->tab_notebook));
}

static void
on_ctx_close_tab(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    GtkWidget *term = GTK_WIDGET(user_data);
    if (!bt_plugin || !bt_plugin->tab_notebook)
        return;

    GtkNotebook *nb = GTK_NOTEBOOK(bt_plugin->tab_notebook);
    gint page = gtk_notebook_page_num(nb, term);
    if (page >= 0)
        tab_manager_close_tab(nb, page);
}

static void
on_ctx_close_others(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    GtkWidget *keep = GTK_WIDGET(user_data);
    if (!bt_plugin || !bt_plugin->tab_notebook)
        return;

    GtkNotebook *nb = GTK_NOTEBOOK(bt_plugin->tab_notebook);
    for (gint i = gtk_notebook_get_n_pages(nb) - 1; i >= 0; i--) {
        GtkWidget *page = gtk_notebook_get_nth_page(nb, i);
        if (page != keep)
            gtk_notebook_remove_page(nb, i);
    }
}

static GtkWidget *
make_menu_item(const gchar *label, const gchar *accel,
               GCallback cb, gpointer data)
{
    GtkWidget *item;

    if (accel) {
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_box_pack_start(GTK_BOX(box), gtk_label_new(label), FALSE, FALSE, 0);
        GtkWidget *accel_label = gtk_label_new(accel);
        gtk_widget_set_opacity(accel_label, 0.5);
        gtk_box_pack_end(GTK_BOX(box), accel_label, FALSE, FALSE, 0);
        item = gtk_menu_item_new();
        gtk_container_add(GTK_CONTAINER(item), box);
    } else {
        item = gtk_menu_item_new_with_label(label);
    }

    g_signal_connect(item, "activate", cb, data);
    return item;
}

static gboolean
on_terminal_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    (void)user_data;

    if (event->button != 3)
        return FALSE;

    VteTerminal *term = VTE_TERMINAL(widget);
    gboolean has_sel = vte_terminal_get_has_selection(term);

    GtkWidget *menu = gtk_menu_new();

    /* Clipboard */
    GtkWidget *item_copy = make_menu_item("Copy", "Alt+C",
        G_CALLBACK(on_ctx_copy), term);
    gtk_widget_set_sensitive(item_copy, has_sel);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_copy);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu),
        make_menu_item("Paste", "Alt+V", G_CALLBACK(on_ctx_paste), term));

    gtk_menu_shell_append(GTK_MENU_SHELL(menu),
        make_menu_item("Select All", NULL, G_CALLBACK(on_ctx_select_all), term));

    /* Separator */
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    /* Terminal actions */
    gtk_menu_shell_append(GTK_MENU_SHELL(menu),
        make_menu_item("Clear Scrollback", NULL, G_CALLBACK(on_ctx_clear), term));

    /* Separator */
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    /* Tab management */
    gtk_menu_shell_append(GTK_MENU_SHELL(menu),
        make_menu_item("New Terminal", NULL, G_CALLBACK(on_ctx_new_tab), term));

    gtk_menu_shell_append(GTK_MENU_SHELL(menu),
        make_menu_item("Close Terminal", NULL, G_CALLBACK(on_ctx_close_tab), term));

    /* Only show "Close Others" when there's more than one tab */
    if (bt_plugin && bt_plugin->tab_notebook &&
        gtk_notebook_get_n_pages(GTK_NOTEBOOK(bt_plugin->tab_notebook)) > 1) {
        gtk_menu_shell_append(GTK_MENU_SHELL(menu),
            make_menu_item("Close Other Terminals", NULL,
                           G_CALLBACK(on_ctx_close_others), term));
    }

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);

    return TRUE;
}

/* --- Terminal title changed — update tab label --- */

static void
on_termprop_changed(VteTerminal *terminal, const gchar *prop, gpointer user_data)
{
    (void)user_data;

    if (g_strcmp0(prop, VTE_TERMPROP_XTERM_TITLE) != 0)
        return;

    const gchar *title = vte_terminal_get_termprop_string(terminal, prop, NULL);
    if (!title || title[0] == '\0')
        return;

    if (!bt_plugin || !bt_plugin->tab_notebook)
        return;

    GtkNotebook *nb = GTK_NOTEBOOK(bt_plugin->tab_notebook);
    tab_manager_set_tab_title(nb, GTK_WIDGET(terminal), title);
}

/* --- Child exited handler --- */

static void
on_child_exited(VteTerminal *terminal,
                gint         status,
                gpointer     user_data)
{
    (void)status;
    (void)user_data;

    /* Don't try to manage tabs during plugin shutdown */
    if (bt_plugin && bt_plugin->shutting_down)
        return;

    GtkWidget *widget = GTK_WIDGET(terminal);
    GtkWidget *parent = gtk_widget_get_parent(widget);

    if (!parent)
        return;

    /* The terminal might be inside a scrolled window */
    if (GTK_IS_SCROLLED_WINDOW(parent))
        parent = gtk_widget_get_parent(parent);

    if (!parent || !GTK_IS_NOTEBOOK(parent))
        return;

    GtkNotebook *nb = GTK_NOTEBOOK(parent);
    gint page_num = gtk_notebook_page_num(nb, gtk_widget_get_parent(widget));
    if (page_num < 0)
        page_num = gtk_notebook_page_num(nb, widget);

    if (page_num >= 0)
        tab_manager_close_tab(nb, page_num);
}

GtkWidget *
terminal_new(const gchar *working_directory)
{
    GtkWidget *term = vte_terminal_new();

    vte_terminal_set_scrollback_lines(VTE_TERMINAL(term), 10000);
    vte_terminal_set_scroll_on_output(VTE_TERMINAL(term), FALSE);
    vte_terminal_set_scroll_on_keystroke(VTE_TERMINAL(term), TRUE);
    vte_terminal_set_mouse_autohide(VTE_TERMINAL(term), TRUE);

    /* Apply configured font */
    const gchar *font_str = (bt_plugin && bt_plugin->font) ? bt_plugin->font : "Monospace 10";
    terminal_set_font(term, font_str);

    /* Apply configured colors */
    if (bt_plugin)
        terminal_set_colors(term, &bt_plugin->fg, &bt_plugin->bg,
                            bt_plugin->palette, BT_PALETTE_SIZE);

    /* Determine shell */
    const gchar *shell = vte_get_user_shell();
    if (!shell)
        shell = g_getenv("SHELL");
    if (!shell)
        shell = "/bin/sh";

    const gchar *argv[] = { shell, NULL };

    /* Determine working directory */
    const gchar *cwd = working_directory;
    if (!cwd)
        cwd = g_get_home_dir();

    vte_terminal_spawn_async(
        VTE_TERMINAL(term),
        VTE_PTY_DEFAULT,
        cwd,
        (gchar **)argv,
        NULL,           /* envv - inherit */
        G_SPAWN_DEFAULT,
        NULL, NULL, NULL,  /* child_setup, data, destroy */
        -1,             /* timeout */
        NULL,           /* cancellable */
        on_spawn_callback,
        NULL            /* user_data */
    );

    g_signal_connect(term, "child-exited", G_CALLBACK(on_child_exited), NULL);
    g_signal_connect(term, "termprop-changed", G_CALLBACK(on_termprop_changed), NULL);
    g_signal_connect(term, "button-press-event", G_CALLBACK(on_terminal_button_press), NULL);

    return term;
}

/* --- Run-command terminal ---
 * Runs a command via sh -c, then waits for Enter before closing.
 * child-exited does NOT auto-close — the user dismisses it. */

static void
on_run_child_exited(VteTerminal *terminal, gint status, gpointer user_data)
{
    (void)user_data;

    if (bt_plugin && bt_plugin->shutting_down)
        return;

    /* Print exit status and prompt */
    gchar *msg;
    if (WIFEXITED(status))
        msg = g_strdup_printf("\r\n\033[1m[Process exited with code %d — press Enter to close]\033[0m",
                              WEXITSTATUS(status));
    else
        msg = g_strdup("\r\n\033[1m[Process terminated — press Enter to close]\033[0m");

    vte_terminal_feed(terminal, msg, -1);
    g_free(msg);

    /* Store a flag so the key handler knows to close on Enter */
    g_object_set_data(G_OBJECT(terminal), "bt-run-finished", GINT_TO_POINTER(1));
}

static gboolean
on_run_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    (void)user_data;

    if (!g_object_get_data(G_OBJECT(widget), "bt-run-finished"))
        return FALSE;

    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
        /* Close this tab */
        if (bt_plugin && bt_plugin->tab_notebook) {
            GtkNotebook *nb = GTK_NOTEBOOK(bt_plugin->tab_notebook);
            gint page = gtk_notebook_page_num(nb, widget);
            if (page >= 0)
                tab_manager_close_tab(nb, page);
        }
        return TRUE;
    }

    return FALSE;
}

GtkWidget *
terminal_new_run_command(const gchar *command, const gchar *working_directory)
{
    GtkWidget *term = vte_terminal_new();

    vte_terminal_set_scrollback_lines(VTE_TERMINAL(term), 10000);
    vte_terminal_set_scroll_on_output(VTE_TERMINAL(term), TRUE);
    vte_terminal_set_scroll_on_keystroke(VTE_TERMINAL(term), TRUE);
    vte_terminal_set_mouse_autohide(VTE_TERMINAL(term), TRUE);

    const gchar *font_str = (bt_plugin && bt_plugin->font) ? bt_plugin->font : "Monospace 10";
    terminal_set_font(term, font_str);

    if (bt_plugin)
        terminal_set_colors(term, &bt_plugin->fg, &bt_plugin->bg,
                            bt_plugin->palette, BT_PALETTE_SIZE);

    /* Run via: sh -c "command" */
    const gchar *shell = "/bin/sh";
    const gchar *argv[] = { shell, "-c", command, NULL };

    const gchar *cwd = working_directory;
    if (!cwd)
        cwd = g_get_home_dir();

    vte_terminal_spawn_async(
        VTE_TERMINAL(term),
        VTE_PTY_DEFAULT,
        cwd,
        (gchar **)argv,
        NULL,
        G_SPAWN_DEFAULT,
        NULL, NULL, NULL,
        -1,
        NULL,
        on_spawn_callback,
        NULL
    );

    /* Use run-specific child-exited (shows exit code, waits for Enter) */
    g_signal_connect(term, "child-exited", G_CALLBACK(on_run_child_exited), NULL);
    g_signal_connect(term, "key-press-event", G_CALLBACK(on_run_key_press), NULL);
    g_signal_connect(term, "termprop-changed", G_CALLBACK(on_termprop_changed), NULL);
    g_signal_connect(term, "button-press-event", G_CALLBACK(on_terminal_button_press), NULL);

    return term;
}

gchar *
terminal_get_working_directory(void)
{
    GeanyDocument *doc = document_get_current();

    if (doc && doc->real_path)
        return g_path_get_dirname(doc->real_path);

    if (doc && doc->file_name)
        return g_path_get_dirname(doc->file_name);

    return g_strdup(g_get_home_dir());
}

void
terminal_set_font(GtkWidget *term, const gchar *font_str)
{
    if (!VTE_IS_TERMINAL(term) || !font_str)
        return;

    PangoFontDescription *font = pango_font_description_from_string(font_str);
    vte_terminal_set_font(VTE_TERMINAL(term), font);
    pango_font_description_free(font);
}

void
terminal_set_colors(GtkWidget *term, const GdkRGBA *fg,
                    const GdkRGBA *bg, const GdkRGBA *palette,
                    gsize palette_size)
{
    if (!VTE_IS_TERMINAL(term))
        return;

    vte_terminal_set_colors(VTE_TERMINAL(term), fg, bg, palette, palette_size);
}
