#include "plugin.h"
#include "colors.h"
#include "tab_manager.h"
#include "terminal.h"
#include "reparent.h"
#include <build.h>

#define BT_CONFIG_GROUP "bottom-terminal"
#define BT_DEFAULT_FONT "Monospace 10"
#define BT_DEFAULT_SCHEME "Tango (Default)"

GeanyPlugin *geany_plugin;
GeanyData   *geany_data;

BottomTermPlugin *bt_plugin = NULL;

static void bt_run_execute_command(void);

/* --- Keybinding IDs --- */

enum {
    KB_NEW_TAB = 0,
    KB_CLOSE_TAB,
    KB_FOCUS_TERMINAL,
    KB_NEXT_TAB,
    KB_PREV_TAB,
    KB_TOGGLE_TERMINAL,
    KB_RUN_IN_TERMINAL,
    KB_COUNT
};

static gboolean
on_keybinding(guint key_id)
{
    if (!bt_plugin || !bt_plugin->tab_notebook)
        return FALSE;

    GtkNotebook *nb = GTK_NOTEBOOK(bt_plugin->tab_notebook);

    switch (key_id) {
    case KB_NEW_TAB:
        tab_manager_add_tab(nb);
        break;

    case KB_CLOSE_TAB: {
        gint page = gtk_notebook_get_current_page(nb);
        if (page >= 0)
            tab_manager_close_tab(nb, page);
        break;
    }

    case KB_FOCUS_TERMINAL: {
        /* Show terminal if hidden */
        if (!gtk_widget_get_visible(bt_plugin->tab_notebook))
            gtk_widget_show(bt_plugin->tab_notebook);

        gint page = gtk_notebook_get_current_page(nb);
        if (page >= 0) {
            GtkWidget *term = gtk_notebook_get_nth_page(nb, page);
            if (term)
                gtk_widget_grab_focus(term);
        }
        break;
    }

    case KB_NEXT_TAB:
        gtk_notebook_next_page(nb);
        break;

    case KB_PREV_TAB:
        gtk_notebook_prev_page(nb);
        break;

    case KB_TOGGLE_TERMINAL:
        if (gtk_widget_get_visible(bt_plugin->tab_notebook))
            gtk_widget_hide(bt_plugin->tab_notebook);
        else
            gtk_widget_show(bt_plugin->tab_notebook);
        break;

    case KB_RUN_IN_TERMINAL:
        bt_run_execute_command();
        break;
    }

    return TRUE;
}

/* --- Menu callback --- */

static void
on_menu_toggle(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    (void)user_data;
    on_keybinding(KB_TOGGLE_TERMINAL);
}

/* --- Run in terminal --- */

/* Expand Geany build command placeholders:
 *   %f = full file path, %d = directory, %e = filename without extension,
 *   %p = project base dir */
static gchar *
bt_expand_placeholders(const gchar *template, GeanyDocument *doc)
{
    if (!template)
        return NULL;

    GString *result = g_string_new(NULL);
    const gchar *p = template;

    while (*p) {
        if (*p == '%' && *(p + 1)) {
            p++;
            switch (*p) {
            case 'f':
                if (doc->real_path)
                    g_string_append(result, doc->real_path);
                else if (doc->file_name)
                    g_string_append(result, doc->file_name);
                break;
            case 'd':
                if (doc->real_path) {
                    gchar *dir = g_path_get_dirname(doc->real_path);
                    g_string_append(result, dir);
                    g_free(dir);
                }
                break;
            case 'e': {
                /* Filename without extension, no path */
                const gchar *path = doc->real_path ? doc->real_path : doc->file_name;
                if (path) {
                    gchar *base = g_path_get_basename(path);
                    gchar *dot = strrchr(base, '.');
                    if (dot)
                        *dot = '\0';
                    g_string_append(result, base);
                    g_free(base);
                }
                break;
            }
            case 'p':
                if (geany_data->app->project &&
                    geany_data->app->project->base_path)
                    g_string_append(result, geany_data->app->project->base_path);
                else if (doc->real_path) {
                    gchar *dir = g_path_get_dirname(doc->real_path);
                    g_string_append(result, dir);
                    g_free(dir);
                }
                break;
            default:
                g_string_append_c(result, '%');
                g_string_append_c(result, *p);
                break;
            }
            p++;
        } else {
            g_string_append_c(result, *p);
            p++;
        }
    }

    return g_string_free(result, FALSE);
}

static void
bt_run_execute_command(void)
{
    if (!bt_plugin || !bt_plugin->tab_notebook)
        return;

    GeanyDocument *doc = document_get_current();
    if (!doc) return;

    /* Try each execute command slot (usually slot 0 = "Execute") */
    guint count = build_get_group_count(GEANY_GBG_EXEC);
    const gchar *cmd_template = NULL;
    const gchar *dir_template = NULL;
    const gchar *label = NULL;

    for (guint i = 0; i < count; i++) {
        cmd_template = build_get_current_menu_item(GEANY_GBG_EXEC, i, GEANY_BC_COMMAND);
        if (cmd_template && cmd_template[0]) {
            dir_template = build_get_current_menu_item(GEANY_GBG_EXEC, i, GEANY_BC_WORKING_DIR);
            label = build_get_current_menu_item(GEANY_GBG_EXEC, i, GEANY_BC_LABEL);
            break;
        }
    }

    if (!cmd_template || !cmd_template[0]) {
        ui_set_statusbar(TRUE, "No execute command configured for this file type.");
        return;
    }

    /* Save the document first */
    if (doc->changed)
        document_save_file(doc, FALSE);

    /* Expand placeholders: %f, %d, %e, etc. */
    gchar *command = bt_expand_placeholders(cmd_template, doc);
    gchar *working_dir = NULL;
    if (dir_template && dir_template[0])
        working_dir = bt_expand_placeholders(dir_template, doc);

    /* If no working dir specified, use document's directory */
    if (!working_dir || !working_dir[0]) {
        g_free(working_dir);
        if (doc->real_path)
            working_dir = g_path_get_dirname(doc->real_path);
        else
            working_dir = g_strdup(g_get_home_dir());
    }

    gchar *tab_label = label ? g_strdup_printf("Run: %s", label) : g_strdup("Run");

    tab_manager_add_run_tab(GTK_NOTEBOOK(bt_plugin->tab_notebook),
                            command, working_dir, tab_label);

    g_free(command);
    g_free(working_dir);
    g_free(tab_label);
}

static void
on_menu_run(GtkMenuItem *item, gpointer user_data)
{
    (void)item;
    (void)user_data;
    bt_run_execute_command();
}

/* --- Key intercept via Geany's plugin "key-press" signal ---
 *
 * Geany's on_key_press_event (keybindings.c) does this:
 *   1. Emit "key-press" on geany_object → if plugin returns TRUE, return TRUE
 *   2. check_vte() for Geany's own VTE → if TRUE, return FALSE (skip keybindings)
 *   3. Process keybindings (Ctrl+C=Copy, Ctrl+X=Cut, etc.)
 *
 * Step 2 only works for Geany's built-in VTE. For our terminals, we hook
 * step 1: when our VTE has focus, we forward the key directly to VTE and
 * return TRUE so Geany skips its keybinding processing entirely.
 */

static gboolean
on_geany_key_press(GObject *obj, GdkEventKey *event, gpointer user_data)
{
    (void)obj;
    (void)user_data;

    if (!bt_plugin || !bt_plugin->tab_notebook)
        return FALSE;

    /* Check if the focused widget is one of our VTE terminals */
    GtkWidget *focused = gtk_window_get_focus(
        GTK_WINDOW(geany_data->main_widgets->window));
    if (!focused || !VTE_IS_TERMINAL(focused))
        return FALSE;

    /* Verify it's inside our tab notebook, not some other VTE */
    GtkWidget *ancestor = gtk_widget_get_ancestor(focused, GTK_TYPE_NOTEBOOK);
    if (ancestor != bt_plugin->tab_notebook)
        return FALSE;

    VteTerminal *term = VTE_TERMINAL(focused);
    guint mod = event->state & gtk_accelerator_get_default_mod_mask();

    /* Alt+C/V or Ctrl+Shift+C/V → explicit copy/paste */
    gboolean is_alt = (mod == GDK_MOD1_MASK);
    gboolean is_ctrl_shift = (mod == (GDK_CONTROL_MASK | GDK_SHIFT_MASK));

    if (is_alt || is_ctrl_shift) {
        gboolean is_copy  = (event->keyval == GDK_KEY_c || event->keyval == GDK_KEY_C);
        gboolean is_paste = (event->keyval == GDK_KEY_v || event->keyval == GDK_KEY_V);

        if (is_copy) {
            vte_terminal_copy_clipboard_format(term, VTE_FORMAT_TEXT);
            return TRUE;
        }
        if (is_paste) {
            vte_terminal_paste_clipboard(term);
            return TRUE;
        }
    }

    /* For all other keys: feed the event directly to VTE, then return
     * TRUE to tell Geany "plugin handled it" — this prevents Geany's
     * keybinding loop from running (Ctrl+C, Ctrl+X, etc. won't fire). */
    gtk_widget_event(focused, (GdkEvent *)event);
    return TRUE;
}

/* --- Helpers: color to/from string --- */

static gchar *
rgba_to_string(const GdkRGBA *c)
{
    return g_strdup_printf("#%02x%02x%02x",
        (guint)(c->red * 255), (guint)(c->green * 255), (guint)(c->blue * 255));
}

static void
string_to_rgba(const gchar *str, GdkRGBA *c)
{
    if (!gdk_rgba_parse(c, str)) {
        c->red = c->green = c->blue = 0.5;
        c->alpha = 1.0;
    }
}

/* --- Settings --- */

static void
bt_apply_scheme_from_preset(BottomTermPlugin *bt, const BtColorScheme *scheme)
{
    bt->fg = scheme->fg;
    bt->bg = scheme->bg;
    memcpy(bt->palette, scheme->palette, sizeof(bt->palette));
}

static void
bt_load_settings(BottomTermPlugin *bt)
{
    bt->config_file = g_build_filename(
        geany_data->app->configdir, "plugins", "bottom-terminal.conf", NULL);

    /* Start with default scheme */
    const BtColorScheme *def = bt_color_scheme_get(0);
    bt_apply_scheme_from_preset(bt, def);
    bt->font = g_strdup(BT_DEFAULT_FONT);
    bt->scheme_name = g_strdup(def->name);
    bt->paned_pos = -1;

    GKeyFile *kf = g_key_file_new();
    if (!g_key_file_load_from_file(kf, bt->config_file, G_KEY_FILE_NONE, NULL)) {
        g_key_file_free(kf);
        return;
    }

    gchar *font = g_key_file_get_string(kf, BT_CONFIG_GROUP, "font", NULL);
    if (font) { g_free(bt->font); bt->font = font; }

    gchar *scheme = g_key_file_get_string(kf, BT_CONFIG_GROUP, "scheme", NULL);
    if (scheme) { g_free(bt->scheme_name); bt->scheme_name = scheme; }

    GError *err = NULL;
    gint pos = g_key_file_get_integer(kf, BT_CONFIG_GROUP, "paned_pos", &err);
    if (!err && pos > 0) bt->paned_pos = pos;
    g_clear_error(&err);

    /* Load fg/bg */
    gchar *s;
    s = g_key_file_get_string(kf, BT_CONFIG_GROUP, "fg", NULL);
    if (s) { string_to_rgba(s, &bt->fg); g_free(s); }
    s = g_key_file_get_string(kf, BT_CONFIG_GROUP, "bg", NULL);
    if (s) { string_to_rgba(s, &bt->bg); g_free(s); }

    /* Load palette */
    for (gint i = 0; i < BT_PALETTE_SIZE; i++) {
        gchar *key = g_strdup_printf("color%d", i);
        s = g_key_file_get_string(kf, BT_CONFIG_GROUP, key, NULL);
        if (s) { string_to_rgba(s, &bt->palette[i]); g_free(s); }
        g_free(key);
    }

    g_key_file_free(kf);
}

static void
bt_save_settings(BottomTermPlugin *bt)
{
    GKeyFile *kf = g_key_file_new();
    g_key_file_set_string(kf, BT_CONFIG_GROUP, "font", bt->font);
    g_key_file_set_string(kf, BT_CONFIG_GROUP, "scheme", bt->scheme_name);

    if (bt->paned_pos > 0)
        g_key_file_set_integer(kf, BT_CONFIG_GROUP, "paned_pos", bt->paned_pos);

    gchar *s;
    s = rgba_to_string(&bt->fg);
    g_key_file_set_string(kf, BT_CONFIG_GROUP, "fg", s); g_free(s);
    s = rgba_to_string(&bt->bg);
    g_key_file_set_string(kf, BT_CONFIG_GROUP, "bg", s); g_free(s);

    for (gint i = 0; i < BT_PALETTE_SIZE; i++) {
        gchar *key = g_strdup_printf("color%d", i);
        s = rgba_to_string(&bt->palette[i]);
        g_key_file_set_string(kf, BT_CONFIG_GROUP, key, s);
        g_free(s); g_free(key);
    }

    gchar *dir = g_path_get_dirname(bt->config_file);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);

    gchar *data = g_key_file_to_data(kf, NULL, NULL);
    g_file_set_contents(bt->config_file, data, -1, NULL);
    g_free(data);
    g_key_file_free(kf);
}

static void
bt_apply_font_to_all(BottomTermPlugin *bt)
{
    GtkNotebook *nb = GTK_NOTEBOOK(bt->tab_notebook);
    gint n = gtk_notebook_get_n_pages(nb);
    for (gint i = 0; i < n; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(nb, i);
        terminal_set_font(page, bt->font);
    }
}

static void
bt_apply_colors_to_all(BottomTermPlugin *bt)
{
    GtkNotebook *nb = GTK_NOTEBOOK(bt->tab_notebook);
    gint n = gtk_notebook_get_n_pages(nb);
    for (gint i = 0; i < n; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(nb, i);
        terminal_set_colors(page, &bt->fg, &bt->bg, bt->palette, BT_PALETTE_SIZE);
    }
}

/* --- Configure dialog --- */

/* Dialog-local state so callbacks can update the UI */
typedef struct {
    GtkWidget   *scheme_combo;
    GtkWidget   *fg_btn;
    GtkWidget   *bg_btn;
    GtkWidget   *palette_btns[BT_PALETTE_SIZE];
    gboolean     updating;  /* suppress feedback loops */
} ConfigWidgets;

static void
config_update_color_buttons(ConfigWidgets *cw)
{
    cw->updating = TRUE;
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(cw->fg_btn), &bt_plugin->fg);
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(cw->bg_btn), &bt_plugin->bg);
    for (gint i = 0; i < BT_PALETTE_SIZE; i++)
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(cw->palette_btns[i]),
                                   &bt_plugin->palette[i]);
    cw->updating = FALSE;
}

static void
on_scheme_changed(GtkComboBox *combo, gpointer user_data)
{
    ConfigWidgets *cw = user_data;
    if (cw->updating)
        return;

    gint idx = gtk_combo_box_get_active(combo);
    if (idx < 0)
        return;

    const BtColorScheme *scheme = bt_color_scheme_get(idx);
    bt_apply_scheme_from_preset(bt_plugin, scheme);

    g_free(bt_plugin->scheme_name);
    bt_plugin->scheme_name = g_strdup(scheme->name);

    config_update_color_buttons(cw);
    bt_apply_colors_to_all(bt_plugin);
    bt_save_settings(bt_plugin);
}

static void
mark_custom_scheme(ConfigWidgets *cw)
{
    /* When user tweaks a color, switch scheme label to "Custom" */
    g_free(bt_plugin->scheme_name);
    bt_plugin->scheme_name = g_strdup("Custom");

    /* Set combo to -1 (no selection) to indicate custom */
    cw->updating = TRUE;
    gtk_combo_box_set_active(GTK_COMBO_BOX(cw->scheme_combo), -1);
    cw->updating = FALSE;
}

static void
on_fg_color_set(GtkColorButton *button, gpointer user_data)
{
    ConfigWidgets *cw = user_data;
    if (cw->updating) return;

    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &bt_plugin->fg);
    mark_custom_scheme(cw);
    bt_apply_colors_to_all(bt_plugin);
    bt_save_settings(bt_plugin);
}

static void
on_bg_color_set(GtkColorButton *button, gpointer user_data)
{
    ConfigWidgets *cw = user_data;
    if (cw->updating) return;

    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &bt_plugin->bg);
    mark_custom_scheme(cw);
    bt_apply_colors_to_all(bt_plugin);
    bt_save_settings(bt_plugin);
}

static void
on_palette_color_set(GtkColorButton *button, gpointer user_data)
{
    ConfigWidgets *cw = user_data;
    if (cw->updating) return;

    /* Find which palette index this button is */
    for (gint i = 0; i < BT_PALETTE_SIZE; i++) {
        if (cw->palette_btns[i] == GTK_WIDGET(button)) {
            gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button),
                                       &bt_plugin->palette[i]);
            break;
        }
    }

    mark_custom_scheme(cw);
    bt_apply_colors_to_all(bt_plugin);
    bt_save_settings(bt_plugin);
}

static void
on_import_geany_theme(GtkButton *button, gpointer user_data)
{
    (void)button;
    ConfigWidgets *cw = user_data;

    GdkRGBA fg, bg, palette[BT_PALETTE_SIZE];

    /* Read the live color scheme name from Geany's in-memory prefs,
     * not from geany.conf (which only flushes on exit) */
    const gchar *scheme = geany_data->editor_prefs->color_scheme;

    if (!bt_import_geany_theme(scheme, geany_data->app->configdir, &fg, &bg, palette)) {
        GtkWidget *dlg = gtk_message_dialog_new(
            NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
            "Could not read Geany's color scheme.\n"
            "Make sure a color scheme is selected in Edit > Preferences > Editor > Display.");
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        return;
    }

    bt_plugin->fg = fg;
    bt_plugin->bg = bg;
    memcpy(bt_plugin->palette, palette, sizeof(bt_plugin->palette));

    g_free(bt_plugin->scheme_name);
    bt_plugin->scheme_name = g_strdup("Geany Theme");

    cw->updating = TRUE;
    gtk_combo_box_set_active(GTK_COMBO_BOX(cw->scheme_combo), -1);
    cw->updating = FALSE;

    config_update_color_buttons(cw);
    bt_apply_colors_to_all(bt_plugin);
    bt_save_settings(bt_plugin);
}

static void
on_font_set(GtkFontButton *button, gpointer user_data)
{
    (void)user_data;

    const gchar *font_str = gtk_font_chooser_get_font(GTK_FONT_CHOOSER(button));
    if (!font_str)
        return;

    g_free(bt_plugin->font);
    bt_plugin->font = g_strdup(font_str);

    bt_apply_font_to_all(bt_plugin);
    bt_save_settings(bt_plugin);
}

static void
on_config_destroy(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    g_free(user_data);
}

static GtkWidget *
bt_configure(GeanyPlugin *plugin, GtkDialog *dialog, gpointer pdata)
{
    (void)plugin;
    (void)dialog;
    (void)pdata;

    ConfigWidgets *cw = g_new0(ConfigWidgets, 1);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);

    /* --- Font row --- */
    GtkWidget *font_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(font_hbox),
        gtk_label_new("Font:"), FALSE, FALSE, 0);
    GtkWidget *font_btn = gtk_font_button_new_with_font(
        bt_plugin->font ? bt_plugin->font : BT_DEFAULT_FONT);
    gtk_font_button_set_use_font(GTK_FONT_BUTTON(font_btn), TRUE);
    g_signal_connect(font_btn, "font-set", G_CALLBACK(on_font_set), NULL);
    gtk_box_pack_start(GTK_BOX(font_hbox), font_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), font_hbox, FALSE, FALSE, 0);

    /* --- Scheme preset dropdown --- */
    GtkWidget *scheme_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(scheme_hbox),
        gtk_label_new("Color scheme:"), FALSE, FALSE, 0);

    cw->scheme_combo = gtk_combo_box_text_new();
    for (gint i = 0; i < bt_color_schemes_count(); i++)
        gtk_combo_box_text_append_text(
            GTK_COMBO_BOX_TEXT(cw->scheme_combo),
            bt_color_scheme_get(i)->name);

    gint active = bt_color_scheme_find(bt_plugin->scheme_name);
    gtk_combo_box_set_active(GTK_COMBO_BOX(cw->scheme_combo), active);

    g_signal_connect(cw->scheme_combo, "changed",
                     G_CALLBACK(on_scheme_changed), cw);
    gtk_box_pack_start(GTK_BOX(scheme_hbox), cw->scheme_combo, TRUE, TRUE, 0);

    GtkWidget *import_btn = gtk_button_new_with_label("Import from Geany theme");
    g_signal_connect(import_btn, "clicked", G_CALLBACK(on_import_geany_theme), cw);
    gtk_box_pack_start(GTK_BOX(scheme_hbox), import_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), scheme_hbox, FALSE, FALSE, 0);

    /* --- Foreground / Background row --- */
    GtkWidget *fgbg_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    gtk_box_pack_start(GTK_BOX(fgbg_hbox),
        gtk_label_new("Text:"), FALSE, FALSE, 0);
    cw->fg_btn = gtk_color_button_new_with_rgba(&bt_plugin->fg);
    g_signal_connect(cw->fg_btn, "color-set", G_CALLBACK(on_fg_color_set), cw);
    gtk_box_pack_start(GTK_BOX(fgbg_hbox), cw->fg_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(fgbg_hbox),
        gtk_label_new("Background:"), FALSE, FALSE, 8);
    cw->bg_btn = gtk_color_button_new_with_rgba(&bt_plugin->bg);
    g_signal_connect(cw->bg_btn, "color-set", G_CALLBACK(on_bg_color_set), cw);
    gtk_box_pack_start(GTK_BOX(fgbg_hbox), cw->bg_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), fgbg_hbox, FALSE, FALSE, 0);

    /* --- Palette grid: 2 rows x 8 columns --- */
    GtkWidget *palette_label = gtk_label_new("ANSI palette:");
    gtk_widget_set_halign(palette_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), palette_label, FALSE, FALSE, 0);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);

    for (gint i = 0; i < BT_PALETTE_SIZE; i++) {
        gint col = i % 8;
        gint row = i / 8;

        cw->palette_btns[i] = gtk_color_button_new_with_rgba(&bt_plugin->palette[i]);
        gtk_widget_set_tooltip_text(cw->palette_btns[i], bt_color_label(i));
        g_signal_connect(cw->palette_btns[i], "color-set",
                         G_CALLBACK(on_palette_color_set), cw);
        gtk_grid_attach(GTK_GRID(grid), cw->palette_btns[i], col, row, 1, 1);
    }

    gtk_box_pack_start(GTK_BOX(vbox), grid, FALSE, FALSE, 0);

    /* Free ConfigWidgets when dialog is destroyed */
    g_signal_connect(vbox, "destroy", G_CALLBACK(on_config_destroy), cw);

    gtk_widget_show_all(vbox);
    return vbox;
}

/* --- Plugin lifecycle --- */

static gboolean
bt_init(GeanyPlugin *plugin, gpointer pdata)
{
    (void)pdata;

    geany_plugin = plugin;
    geany_data = plugin->geany_data;

    bt_plugin = g_new0(BottomTermPlugin, 1);
    bt_plugin->geany_plugin = plugin;

    /* Load settings (font, etc.) before creating terminals */
    bt_load_settings(bt_plugin);

    /* Create the tabbed terminal notebook */
    bt_plugin->tab_notebook = tab_manager_new();

    /* Inject our paned into the widget hierarchy */
    reparent_inject_paned(bt_plugin);

    /* Register keybindings */
    GeanyKeyGroup *group = plugin_set_key_group(plugin,
        "bottom_terminal", KB_COUNT, on_keybinding);

    keybindings_set_item(group, KB_NEW_TAB,
        NULL, 0, 0, "bt_new_tab", "New terminal tab", bt_plugin->menu_item);
    keybindings_set_item(group, KB_CLOSE_TAB,
        NULL, 0, 0, "bt_close_tab", "Close terminal tab", NULL);
    keybindings_set_item(group, KB_FOCUS_TERMINAL,
        NULL, 0, 0, "bt_focus_terminal", "Focus terminal", NULL);
    keybindings_set_item(group, KB_NEXT_TAB,
        NULL, 0, 0, "bt_next_tab", "Next terminal tab", NULL);
    keybindings_set_item(group, KB_PREV_TAB,
        NULL, 0, 0, "bt_prev_tab", "Previous terminal tab", NULL);
    keybindings_set_item(group, KB_TOGGLE_TERMINAL,
        NULL, 0, 0, "bt_toggle", "Toggle terminal visibility", NULL);
    keybindings_set_item(group, KB_RUN_IN_TERMINAL,
        NULL, 0, 0, "bt_run", "Run in terminal", NULL);

    /* Add menu items under Tools */
    bt_plugin->menu_item = gtk_menu_item_new_with_mnemonic("Toggle _Bottom Terminal");
    g_signal_connect(bt_plugin->menu_item, "activate",
                     G_CALLBACK(on_menu_toggle), NULL);
    gtk_widget_show(bt_plugin->menu_item);
    gtk_container_add(GTK_CONTAINER(geany_data->main_widgets->tools_menu),
                      bt_plugin->menu_item);

    GtkWidget *run_item = gtk_menu_item_new_with_mnemonic("_Run in Terminal");
    g_signal_connect(run_item, "activate", G_CALLBACK(on_menu_run), NULL);
    gtk_widget_show(run_item);
    gtk_container_add(GTK_CONTAINER(geany_data->main_widgets->tools_menu), run_item);
    bt_plugin->run_menu_item = run_item;

    /* Hook Geany's plugin "key-press" signal — this fires inside Geany's
     * on_key_press_event, BEFORE keybinding processing. Returning TRUE
     * from our handler tells Geany to skip its keybindings entirely. */
    plugin_signal_connect(plugin, geany_data->object, "key-press", FALSE,
                          G_CALLBACK(on_geany_key_press), NULL);

    return TRUE;
}

static void
bt_cleanup(GeanyPlugin *plugin, gpointer pdata)
{
    (void)plugin;
    (void)pdata;

    if (!bt_plugin)
        return;

    /* plugin_signal_connect auto-disconnects on cleanup, no manual disconnect needed */

    /* Remove menu items */
    if (bt_plugin->menu_item && GTK_IS_WIDGET(bt_plugin->menu_item))
        gtk_widget_destroy(bt_plugin->menu_item);
    if (bt_plugin->run_menu_item && GTK_IS_WIDGET(bt_plugin->run_menu_item))
        gtk_widget_destroy(bt_plugin->run_menu_item);

    /* Restore original widget hierarchy (also captures paned position) */
    reparent_restore(bt_plugin);

    /* Save settings (persists paned position) */
    bt_save_settings(bt_plugin);

    g_free(bt_plugin->font);
    g_free(bt_plugin->scheme_name);
    g_free(bt_plugin->config_file);
    g_free(bt_plugin);
    bt_plugin = NULL;
}

/* --- Plugin entry point --- */

G_MODULE_EXPORT void
geany_load_module(GeanyPlugin *plugin)
{
    plugin->info->name        = "Bottom Terminal";
    plugin->info->description = "Multi-tabbed VTE terminal below the document area";
    plugin->info->version     = PLUGIN_VERSION;
    plugin->info->author      = "April";

    plugin->funcs->init      = bt_init;
    plugin->funcs->cleanup   = bt_cleanup;
    plugin->funcs->configure = bt_configure;

    GEANY_PLUGIN_REGISTER(plugin, 250);
}
