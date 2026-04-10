#include "tmux.h"
#include "plugin.h"
#include "tab_manager.h"
#include "terminal.h"

#include <unistd.h>
#include <signal.h>

/* ── Types ─────────────────────────────────────────────────────────── */

typedef enum {
    TMUX_CMD_NONE,
    TMUX_CMD_LIST_WINDOWS,  /* initial window enumeration after attach */
    TMUX_CMD_CAPTURE_PANE,  /* replay pane content to VTE after resize */
    TMUX_CMD_CURSOR_Y,      /* query cursor row to fix VTE cursor pos */
} TmuxCmdType;

typedef struct {
    TmuxCmdType type;
    gint        window_id;  /* for CAPTURE_PANE */
} TmuxPendingCmd;

typedef struct {
    gint        window_id;
    gint        pane_id;   /* -1 until known */
    GtkWidget  *vte;
    gchar      *title;
} BtTmuxWindow;

struct _BtTmuxSession {
    gchar      *session_name;
    GPid        pid;
    gint        stdin_fd;
    gint        stdout_fd;
    GIOChannel *io_channel;
    guint       io_watch_id;
    GString    *line_buf;

    /* window_id → BtTmuxWindow* */
    GHashTable *windows_by_wid;
    /* pane_id   → BtTmuxWindow* (alias pointers, not owned) */
    GHashTable *windows_by_pane;

    /* command / response tracking */
    gboolean    in_begin;
    gboolean    connected;     /* TRUE after initial handshake */
    GString    *cmd_response;
    GQueue     *cmd_queue;

    /* resize debounce */
    guint       resize_timeout_id;
    glong       last_cols;
    glong       last_rows;

    /* initial sync: capture panes after first resize */
    gboolean    needs_initial_capture;

    /* notebook signals */
    gulong      switch_page_id;
};

/* ── Globals ───────────────────────────────────────────────────────── */

static BtTmuxSession *active_session = NULL;

/* ── Forward declarations ──────────────────────────────────────────── */

static void     session_destroy         (BtTmuxSession *s);
static void     send_command            (BtTmuxSession *s, const gchar *cmd);
static void     remove_all_tmux_tabs    (BtTmuxSession *s);
static void     handle_line             (BtTmuxSession *s, const gchar *line);

/* ── Octal decoder for %output data ───────────────────────────────── */

/* Strip ESC k ... ESC \ (tmux/screen window-title sequences) from decoded
 * output.  VTE doesn't understand ESC k and would render the title as
 * literal text.  If title_out is non-NULL, the extracted title is returned
 * (caller frees).  Only the last title in the buffer is returned. */
static gsize
strip_esck(gchar *data, gsize len, gchar **title_out)
{
    gsize r = 0, w = 0;
    while (r < len) {
        if (data[r] == '\033' && r + 1 < len && data[r + 1] == 'k') {
            /* Extract title between ESC k and ESC \ */
            r += 2;
            gsize title_start = r;
            while (r < len) {
                if (data[r] == '\033' && r + 1 < len && data[r + 1] == '\\') {
                    if (title_out) {
                        g_free(*title_out);
                        *title_out = g_strndup(data + title_start,
                                               r - title_start);
                    }
                    r += 2;
                    break;
                }
                r++;
            }
        } else {
            data[w++] = data[r++];
        }
    }
    return w;
}

static gchar *
decode_output(const gchar *data, gsize *out_len)
{
    gsize len = strlen(data);
    GByteArray *out = g_byte_array_sized_new(len);

    for (gsize i = 0; i < len; i++) {
        if (data[i] == '\\' && i + 1 < len) {
            if (data[i + 1] == '\\') {
                guint8 c = '\\';
                g_byte_array_append(out, &c, 1);
                i++;
            } else if (data[i + 1] >= '0' && data[i + 1] <= '7') {
                guint val = 0;
                gint j;
                for (j = 1; j <= 3 && (i + j) < len &&
                     data[i + j] >= '0' && data[i + j] <= '7'; j++)
                    val = val * 8 + (data[i + j] - '0');
                guint8 byte = (guint8)val;
                g_byte_array_append(out, &byte, 1);
                i += j - 1;
            } else {
                guint8 c = data[i];
                g_byte_array_append(out, &c, 1);
            }
        } else {
            g_byte_array_append(out, (const guint8 *)&data[i], 1);
        }
    }

    *out_len = out->len;
    return (gchar *)g_byte_array_free(out, FALSE);
}

/* ── Key translation: GdkEventKey → tmux send-keys command ────────── */

typedef struct {
    guint       gdk_key;
    const char *tmux_name;
} KeyMapEntry;

static const KeyMapEntry special_keys[] = {
    { GDK_KEY_Return,       "Enter"   },
    { GDK_KEY_KP_Enter,     "Enter"   },
    { GDK_KEY_Tab,          "Tab"     },
    { GDK_KEY_ISO_Left_Tab, "BTab"    },
    { GDK_KEY_BackSpace,    "BSpace"  },
    { GDK_KEY_Delete,       "DC"      },
    { GDK_KEY_Insert,       "IC"      },
    { GDK_KEY_Home,         "Home"    },
    { GDK_KEY_End,          "End"     },
    { GDK_KEY_Page_Up,      "PPage"   },
    { GDK_KEY_Page_Down,    "NPage"   },
    { GDK_KEY_Up,           "Up"      },
    { GDK_KEY_Down,         "Down"    },
    { GDK_KEY_Left,         "Left"    },
    { GDK_KEY_Right,        "Right"   },
    { GDK_KEY_Escape,       "Escape"  },
    { GDK_KEY_F1,           "F1"      },
    { GDK_KEY_F2,           "F2"      },
    { GDK_KEY_F3,           "F3"      },
    { GDK_KEY_F4,           "F4"      },
    { GDK_KEY_F5,           "F5"      },
    { GDK_KEY_F6,           "F6"      },
    { GDK_KEY_F7,           "F7"      },
    { GDK_KEY_F8,           "F8"      },
    { GDK_KEY_F9,           "F9"      },
    { GDK_KEY_F10,          "F10"     },
    { GDK_KEY_F11,          "F11"     },
    { GDK_KEY_F12,          "F12"     },
    { 0, NULL }
};

static const char *
lookup_special_key(guint keyval)
{
    for (const KeyMapEntry *e = special_keys; e->tmux_name; e++) {
        if (e->gdk_key == keyval)
            return e->tmux_name;
    }
    return NULL;
}

/* ── Command helpers ──────────────────────────────────────────────── */

static void
send_command(BtTmuxSession *s, const gchar *cmd)
{
    if (s->stdin_fd < 0)
        return;
    /* tmux control mode reads one command per line.
     * Every command generates a %begin/%end response, so auto-push a
     * NONE entry to keep the command queue in sync. */
    if (write(s->stdin_fd, cmd, strlen(cmd)) < 0)
        g_warning("tmux: write failed");

    TmuxPendingCmd *pc = g_new0(TmuxPendingCmd, 1);
    pc->type = TMUX_CMD_NONE;
    g_queue_push_tail(s->cmd_queue, pc);
}

static void
send_commandf(BtTmuxSession *s, const gchar *fmt, ...)
    G_GNUC_PRINTF(2, 3);

static void
send_commandf(BtTmuxSession *s, const gchar *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    gchar *cmd = g_strdup_vprintf(fmt, ap);
    va_end(ap);
    send_command(s, cmd);
    g_free(cmd);
}

/* Override the type of the most recently sent command's queue entry. */
static void
set_pending_type(BtTmuxSession *s, TmuxCmdType type, gint window_id)
{
    TmuxPendingCmd *cmd = g_queue_peek_tail(s->cmd_queue);
    if (cmd) {
        cmd->type = type;
        cmd->window_id = window_id;
    }
}

/* ── VTE creation for tmux tabs ───────────────────────────────────── */

static gboolean resize_timeout_cb(gpointer data);

static void
on_tmux_vte_size_allocate(GtkWidget *widget, GdkRectangle *alloc, gpointer data)
{
    (void)widget;
    (void)alloc;
    BtTmuxSession *s = data;

    if (s->resize_timeout_id)
        g_source_remove(s->resize_timeout_id);

    s->resize_timeout_id = g_timeout_add(100, resize_timeout_cb, s);
}

static gboolean
resize_timeout_cb(gpointer data)
{
    BtTmuxSession *s = data;
    s->resize_timeout_id = 0;

    if (!bt_plugin || !bt_plugin->tab_notebook)
        return G_SOURCE_REMOVE;

    /* Find any tmux VTE to get its dimensions */
    GtkNotebook *nb = GTK_NOTEBOOK(bt_plugin->tab_notebook);
    gint n = gtk_notebook_get_n_pages(nb);
    for (gint i = 0; i < n; i++) {
        GtkWidget *page = gtk_notebook_get_nth_page(nb, i);
        if (bt_tmux_is_tmux_tab(page)) {
            glong cols = vte_terminal_get_column_count(VTE_TERMINAL(page));
            glong rows = vte_terminal_get_row_count(VTE_TERMINAL(page));
            if (cols != s->last_cols || rows != s->last_rows ||
                s->needs_initial_capture) {
                s->last_cols = cols;
                s->last_rows = rows;
                send_commandf(s, "refresh-client -C %ld,%ld\n", cols, rows);

                /* On first resize after attach, capture each pane's content.
                 * tmux processes commands sequentially, so the pane is
                 * already resized to our VTE width when capture runs. */
                if (s->needs_initial_capture) {
                    s->needs_initial_capture = FALSE;
                    GHashTableIter it;
                    gpointer k, v;
                    g_hash_table_iter_init(&it, s->windows_by_wid);
                    while (g_hash_table_iter_next(&it, &k, &v)) {
                        BtTmuxWindow *w = v;
                        send_commandf(s,
                            "capture-pane -e -p -t @%d\n", w->window_id);
                        set_pending_type(s, TMUX_CMD_CAPTURE_PANE,
                                         w->window_id);
                        send_commandf(s,
                            "display-message -t @%d -p '#{cursor_x} #{cursor_y}'\n",
                            w->window_id);
                        set_pending_type(s, TMUX_CMD_CURSOR_Y,
                                         w->window_id);
                    }
                }
            }
            break;
        }
    }

    return G_SOURCE_REMOVE;
}

static GtkWidget *
create_tmux_vte(BtTmuxSession *s, gint window_id, gint pane_id)
{
    GtkWidget *term = vte_terminal_new();

    vte_terminal_set_scrollback_lines(VTE_TERMINAL(term), 10000);
    vte_terminal_set_scroll_on_output(VTE_TERMINAL(term), FALSE);
    vte_terminal_set_scroll_on_keystroke(VTE_TERMINAL(term), TRUE);
    vte_terminal_set_mouse_autohide(VTE_TERMINAL(term), TRUE);

    /* Apply font and colors from plugin settings */
    const gchar *font_str = (bt_plugin && bt_plugin->font)
                            ? bt_plugin->font : "Monospace 10";
    terminal_set_font(term, font_str);

    if (bt_plugin)
        terminal_set_colors(term, &bt_plugin->fg, &bt_plugin->bg,
                            bt_plugin->palette, BT_PALETTE_SIZE);

    /* Tag this VTE as a tmux tab with metadata */
    g_object_set_data(G_OBJECT(term), "bt-tmux-session", s);
    g_object_set_data(G_OBJECT(term), "bt-tmux-wid",
                      GINT_TO_POINTER(window_id));
    g_object_set_data(G_OBJECT(term), "bt-tmux-pane",
                      GINT_TO_POINTER(pane_id));

    /* Resize tracking */
    g_signal_connect(term, "size-allocate",
                     G_CALLBACK(on_tmux_vte_size_allocate), s);

    return term;
}

/* ── Tab management ───────────────────────────────────────────────── */

static void
add_tmux_tab(BtTmuxSession *s, BtTmuxWindow *win)
{
    (void)s;
    if (!bt_plugin || !bt_plugin->tab_notebook)
        return;

    GtkNotebook *nb = GTK_NOTEBOOK(bt_plugin->tab_notebook);
    const gchar *title = win->title ? win->title : "tmux";

    tab_manager_add_tmux_tab(nb, win->vte, title);

    /* Show the terminal pane if hidden */
    if (!gtk_widget_get_visible(bt_plugin->tab_notebook))
        gtk_widget_show(bt_plugin->tab_notebook);
}

static void
remove_tmux_tab(BtTmuxWindow *win)
{
    if (!bt_plugin || !bt_plugin->tab_notebook || !win->vte)
        return;

    GtkNotebook *nb = GTK_NOTEBOOK(bt_plugin->tab_notebook);
    gint page = gtk_notebook_page_num(nb, win->vte);
    if (page >= 0)
        gtk_notebook_remove_page(nb, page);
}

static void
tmux_window_free(BtTmuxWindow *win)
{
    g_free(win->title);
    g_free(win);
}

static void
remove_all_tmux_tabs(BtTmuxSession *s)
{
    if (!bt_plugin || !bt_plugin->tab_notebook)
        return;

    GtkNotebook *nb = GTK_NOTEBOOK(bt_plugin->tab_notebook);

    /* Remove from end to start to avoid index shifting */
    for (gint i = gtk_notebook_get_n_pages(nb) - 1; i >= 0; i--) {
        GtkWidget *page = gtk_notebook_get_nth_page(nb, i);
        if (page && g_object_get_data(G_OBJECT(page), "bt-tmux-session") == s)
            gtk_notebook_remove_page(nb, i);
    }

    /* Ensure at least one tab remains */
    if (gtk_notebook_get_n_pages(nb) == 0 &&
        !(bt_plugin && bt_plugin->shutting_down))
        tab_manager_add_tab(nb);
}

/* ── Notification handlers ────────────────────────────────────────── */

static void
handle_output(BtTmuxSession *s, const gchar *line)
{
    /* Format: %output %<pane_id> <data> */
    if (line[0] != '%')
        return;

    const gchar *p = line + 1; /* skip the % of pane id */
    gchar *end;
    glong pane_id = strtol(p, &end, 10);
    if (end == p || *end != ' ')
        return;

    const gchar *data = end + 1;

    BtTmuxWindow *win = g_hash_table_lookup(
        s->windows_by_pane, GINT_TO_POINTER((gint)pane_id));

    /* Lazy pane→window mapping: if this pane is unknown, assign it to
     * the first window that doesn't have a pane yet (works for
     * single-pane-per-window, which is all v1 supports). */
    if (!win) {
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, s->windows_by_wid);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            BtTmuxWindow *candidate = value;
            if (candidate->pane_id < 0) {
                candidate->pane_id = (gint)pane_id;
                g_object_set_data(G_OBJECT(candidate->vte), "bt-tmux-pane",
                                  GINT_TO_POINTER(candidate->pane_id));
                g_hash_table_insert(s->windows_by_pane,
                                    GINT_TO_POINTER(candidate->pane_id),
                                    candidate);
                win = candidate;
                break;
            }
        }
    }

    if (win && win->vte) {
        gsize decoded_len;
        gchar *decoded = decode_output(data, &decoded_len);

        /* Strip ESC k <title> ESC \ sequences (tmux window title).
         * VTE doesn't handle ESC k and would render the title as text.
         * Use the extracted title to update the tab label instead. */
        gchar *title = NULL;
        decoded_len = strip_esck(decoded, decoded_len, &title);

        if (title && title[0]) {
            gchar *tab_title = g_strdup_printf("%s,@%d: %s",
                s->session_name, win->window_id, title);
            g_free(win->title);
            win->title = tab_title;
            if (bt_plugin && bt_plugin->tab_notebook)
                tab_manager_set_tab_title(
                    GTK_NOTEBOOK(bt_plugin->tab_notebook),
                    win->vte, tab_title);
        }
        g_free(title);

        vte_terminal_feed(VTE_TERMINAL(win->vte), decoded, decoded_len);
        g_free(decoded);
    }
}

static void
handle_window_add(BtTmuxSession *s, gint window_id)
{
    /* Check if we already know this window */
    if (g_hash_table_lookup(s->windows_by_wid, GINT_TO_POINTER(window_id)))
        return;

    BtTmuxWindow *win = g_new0(BtTmuxWindow, 1);
    win->window_id = window_id;
    win->pane_id = -1;
    win->title = g_strdup_printf("%s,@%d: tmux", s->session_name, window_id);
    win->vte = create_tmux_vte(s, window_id, -1);

    g_hash_table_insert(s->windows_by_wid, GINT_TO_POINTER(window_id), win);
    add_tmux_tab(s, win);
}

static void
handle_window_close(BtTmuxSession *s, gint window_id)
{
    BtTmuxWindow *win = g_hash_table_lookup(
        s->windows_by_wid, GINT_TO_POINTER(window_id));
    if (!win)
        return;

    if (win->pane_id >= 0)
        g_hash_table_remove(s->windows_by_pane, GINT_TO_POINTER(win->pane_id));

    remove_tmux_tab(win);
    g_hash_table_remove(s->windows_by_wid, GINT_TO_POINTER(window_id));
}

static void
handle_window_renamed(BtTmuxSession *s, gint window_id, const gchar *name)
{
    BtTmuxWindow *win = g_hash_table_lookup(
        s->windows_by_wid, GINT_TO_POINTER(window_id));
    if (!win)
        return;

    gchar *tab_title = g_strdup_printf("%s,@%d: %s",
                                       s->session_name, window_id, name);
    g_free(win->title);
    win->title = tab_title;

    if (win->vte && bt_plugin && bt_plugin->tab_notebook)
        tab_manager_set_tab_title(GTK_NOTEBOOK(bt_plugin->tab_notebook),
                                  win->vte, tab_title);
}

static void
handle_exit(BtTmuxSession *s)
{
    g_debug("tmux control mode exited");
    remove_all_tmux_tabs(s);
    session_destroy(s);
    if (active_session == s)
        active_session = NULL;
}

/* ── Command response handlers ────────────────────────────────────── */

static void
process_list_windows_response(BtTmuxSession *s, const gchar *response)
{
    /* Response from: list-windows -F '#{window_id} #{pane_id} #{window_name}'
     * Each line: @<wid> %<pid> <name> */
    gchar **lines = g_strsplit(response, "\n", -1);

    for (gint i = 0; lines[i] && lines[i][0]; i++) {
        const gchar *line = lines[i];
        if (line[0] != '@')
            continue;

        gchar *end;
        glong wid = strtol(line + 1, &end, 10);
        if (*end != ' ')
            continue;

        const gchar *p = end + 1;
        if (p[0] != '%')
            continue;

        glong pid = strtol(p + 1, &end, 10);
        const gchar *name = (*end == ' ') ? end + 1 : NULL;

        /* Skip if window already tracked */
        if (g_hash_table_lookup(s->windows_by_wid, GINT_TO_POINTER((gint)wid)))
            continue;

        gchar *clean_name = NULL;
        if (name && name[0]) {
            clean_name = g_strdup(name);
            g_strstrip(clean_name);
        }

        BtTmuxWindow *win = g_new0(BtTmuxWindow, 1);
        win->window_id = (gint)wid;
        win->pane_id = (gint)pid;
        win->title = g_strdup_printf("%s,@%d: %s", s->session_name,
                        (gint)wid, clean_name ? clean_name : "tmux");
        g_free(clean_name);
        win->vte = create_tmux_vte(s, win->window_id, win->pane_id);

        g_hash_table_insert(s->windows_by_wid,
                            GINT_TO_POINTER(win->window_id), win);
        g_hash_table_insert(s->windows_by_pane,
                            GINT_TO_POINTER(win->pane_id), win);

        add_tmux_tab(s, win);
    }

    g_strfreev(lines);

    /* Defer pane capture until the first resize callback, when we know
     * the actual VTE dimensions and can tell tmux the correct size first. */
    s->needs_initial_capture = TRUE;
}

static void
process_capture_pane_response(BtTmuxSession *s, gint window_id,
                              const gchar *response)
{
    BtTmuxWindow *win = g_hash_table_lookup(
        s->windows_by_wid, GINT_TO_POINTER(window_id));
    if (!win || !win->vte)
        return;

    VteTerminal *vte = VTE_TERMINAL(win->vte);

    /* Feed each line to VTE.  Strip trailing whitespace to avoid
     * issues if capture width doesn't perfectly match VTE width. */
    gchar **lines = g_strsplit(response, "\n", -1);

    /* Find last non-empty line to skip trailing blank rows */
    gint last = -1;
    for (gint i = 0; lines[i]; i++) {
        if (lines[i][0])
            last = i;
    }

    for (gint i = 0; i <= last; i++) {
        if (i > 0)
            vte_terminal_feed(vte, "\r\n", 2);
        if (lines[i][0]) {
            g_strchomp(lines[i]);  /* strip trailing spaces */
            vte_terminal_feed(vte, lines[i], -1);
        }
    }
    g_strfreev(lines);
}

static void
process_cursor_y_response(BtTmuxSession *s, gint window_id,
                          const gchar *response)
{
    BtTmuxWindow *win = g_hash_table_lookup(
        s->windows_by_wid, GINT_TO_POINTER(window_id));
    if (!win || !win->vte)
        return;

    /* Response: "<cursor_x> <cursor_y>\n" (both 0-based).
     * Position VTE's cursor there with an absolute CUP sequence so
     * subsequent relative moves (Reverse Index, etc.) are correct. */
    gint cursor_x = 0, cursor_y = 0;
    sscanf(response, "%d %d", &cursor_x, &cursor_y);

    VteTerminal *vte = VTE_TERMINAL(win->vte);
    gchar cup[32];
    g_snprintf(cup, sizeof(cup), "\033[%d;%dH", cursor_y + 1, cursor_x + 1);
    vte_terminal_feed(vte, cup, -1);
}

static void
process_command_response(BtTmuxSession *s)
{
    TmuxPendingCmd *cmd = g_queue_pop_head(s->cmd_queue);
    if (!cmd) {
        /* No pending command — this is the initial handshake response.
         * Now that we're connected, query existing windows. */
        if (!s->connected) {
            s->connected = TRUE;
            send_command(s,
                "list-windows -F '#{window_id} #{pane_id} #{window_name}'\n");
            set_pending_type(s, TMUX_CMD_LIST_WINDOWS, 0);
        }
        return;
    }

    const gchar *response = s->cmd_response->str;

    switch (cmd->type) {
    case TMUX_CMD_LIST_WINDOWS:
        process_list_windows_response(s, response);
        break;
    case TMUX_CMD_CAPTURE_PANE:
        process_capture_pane_response(s, cmd->window_id, response);
        break;
    case TMUX_CMD_CURSOR_Y:
        process_cursor_y_response(s, cmd->window_id, response);
        break;
    case TMUX_CMD_NONE:
        break;
    }

    g_free(cmd);
}

/* ── Line parser ──────────────────────────────────────────────────── */

static void
handle_line(BtTmuxSession *s, const gchar *line)
{
    /* Inside a %begin/%end block — accumulate response */
    if (s->in_begin) {
        if (g_str_has_prefix(line, "%end ")) {
            s->in_begin = FALSE;
            process_command_response(s);
            g_string_truncate(s->cmd_response, 0);
        } else if (g_str_has_prefix(line, "%error ")) {
            s->in_begin = FALSE;
            g_debug("tmux command error: %s", s->cmd_response->str);
            /* Pop the pending command so the queue stays in sync */
            TmuxPendingCmd *cmd = g_queue_pop_head(s->cmd_queue);
            g_free(cmd);
            g_string_truncate(s->cmd_response, 0);
        } else {
            g_string_append(s->cmd_response, line);
            g_string_append_c(s->cmd_response, '\n');
        }
        return;
    }

    /* Notifications */
    if (g_str_has_prefix(line, "%begin ")) {
        s->in_begin = TRUE;
        g_string_truncate(s->cmd_response, 0);
        return;
    }

    if (g_str_has_prefix(line, "%output ")) {
        /* %output %<pane_id> <data> */
        handle_output(s, line + 8);
        return;
    }

    if (g_str_has_prefix(line, "%window-add @")) {
        gint wid = atoi(line + 13);
        handle_window_add(s, wid);
        return;
    }

    if (g_str_has_prefix(line, "%window-close @")) {
        gint wid = atoi(line + 15);
        handle_window_close(s, wid);
        return;
    }

    if (g_str_has_prefix(line, "%window-renamed @")) {
        const gchar *p = line + 17;
        gchar *end;
        glong wid = strtol(p, &end, 10);
        const gchar *name = (*end == ' ') ? end + 1 : NULL;
        if (name)
            handle_window_renamed(s, (gint)wid, name);
        return;
    }

    if (g_str_has_prefix(line, "%exit")) {
        handle_exit(s);
        return;
    }

    /* Ignore other notifications (%session-changed, %layout-change, etc.) */
}

/* ── Async I/O callback ───────────────────────────────────────────── */

static gboolean
on_tmux_io(GIOChannel *source, GIOCondition condition, gpointer data)
{
    (void)source;
    BtTmuxSession *s = data;

    if (condition & G_IO_IN) {
        gchar buf[4096];
        ssize_t n = read(s->stdout_fd, buf, sizeof(buf));
        if (n > 0) {
            g_string_append_len(s->line_buf, buf, n);

            /* Process complete lines */
            gchar *start = s->line_buf->str;
            gchar *nl;
            while ((nl = strchr(start, '\n')) != NULL) {
                *nl = '\0';
                handle_line(s, start);
                /* handle_exit may have destroyed the session */
                if (active_session != s)
                    return G_SOURCE_REMOVE;
                start = nl + 1;
            }

            /* Keep any incomplete line in the buffer */
            if (start != s->line_buf->str) {
                gsize remaining = s->line_buf->len - (start - s->line_buf->str);
                g_string_erase(s->line_buf, 0,
                               start - s->line_buf->str);
                (void)remaining;
            }
        } else if (n == 0) {
            /* EOF */
            handle_exit(s);
            return G_SOURCE_REMOVE;
        }
    }

    if (condition & (G_IO_HUP | G_IO_ERR)) {
        handle_exit(s);
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

/* ── Page switch handler ──────────────────────────────────────────── */

static void
on_page_switched(GtkNotebook *nb, GtkWidget *page,
                 guint page_num, gpointer data)
{
    (void)nb;
    (void)page_num;
    BtTmuxSession *s = data;

    if (!page || !bt_tmux_is_tmux_tab(page))
        return;

    gint wid = GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(page), "bt-tmux-wid"));
    send_commandf(s, "select-window -t @%d\n", wid);
}

/* ── Connection management ────────────────────────────────────────── */

static BtTmuxSession *
session_create(const gchar *name)
{
    BtTmuxSession *s = g_new0(BtTmuxSession, 1);
    s->session_name = g_strdup(name);
    s->stdin_fd = -1;
    s->stdout_fd = -1;
    s->line_buf = g_string_new(NULL);
    s->cmd_response = g_string_new(NULL);
    s->cmd_queue = g_queue_new();

    s->windows_by_wid = g_hash_table_new_full(
        g_direct_hash, g_direct_equal,
        NULL, (GDestroyNotify)tmux_window_free);
    s->windows_by_pane = g_hash_table_new(
        g_direct_hash, g_direct_equal);

    return s;
}

static void
session_destroy(BtTmuxSession *s)
{
    if (!s)
        return;

    if (s->resize_timeout_id)
        g_source_remove(s->resize_timeout_id);

    if (s->io_watch_id)
        g_source_remove(s->io_watch_id);

    if (s->io_channel) {
        g_io_channel_unref(s->io_channel);
        s->io_channel = NULL;
    }

    /* Disconnect notebook signal */
    if (s->switch_page_id && bt_plugin && bt_plugin->tab_notebook) {
        g_signal_handler_disconnect(bt_plugin->tab_notebook, s->switch_page_id);
        s->switch_page_id = 0;
    }

    if (s->stdin_fd >= 0)
        close(s->stdin_fd);
    if (s->stdout_fd >= 0)
        close(s->stdout_fd);

    if (s->pid > 0) {
        kill(s->pid, SIGTERM);
        g_spawn_close_pid(s->pid);
    }

    /* Clear windows (removes VTE widgets from hash table but not notebook —
     * caller should have called remove_all_tmux_tabs first if needed) */
    g_hash_table_destroy(s->windows_by_pane);
    g_hash_table_destroy(s->windows_by_wid);

    g_string_free(s->line_buf, TRUE);
    g_string_free(s->cmd_response, TRUE);

    while (!g_queue_is_empty(s->cmd_queue))
        g_free(g_queue_pop_head(s->cmd_queue));
    g_queue_free(s->cmd_queue);

    g_free(s->session_name);
    g_free(s);
}

static gboolean
session_spawn(BtTmuxSession *s, gchar **argv)
{
    gint child_stdin, child_stdout;
    GError *err = NULL;

    gboolean ok = g_spawn_async_with_pipes(
        NULL,           /* working dir */
        argv,
        NULL,           /* envp — inherit */
        G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
        NULL, NULL,     /* child setup */
        &s->pid,
        &child_stdin,
        &child_stdout,
        NULL,           /* stderr — ignore */
        &err);

    if (!ok) {
        g_warning("tmux: spawn failed: %s", err->message);
        g_error_free(err);
        return FALSE;
    }

    s->stdin_fd = child_stdin;
    s->stdout_fd = child_stdout;

    /* Set up async I/O on stdout */
    s->io_channel = g_io_channel_unix_new(s->stdout_fd);
    g_io_channel_set_encoding(s->io_channel, NULL, NULL);
    g_io_channel_set_buffered(s->io_channel, FALSE);
    s->io_watch_id = g_io_add_watch(s->io_channel,
                                     G_IO_IN | G_IO_HUP | G_IO_ERR,
                                     on_tmux_io, s);

    /* Connect to notebook switch-page to track active window */
    if (bt_plugin && bt_plugin->tab_notebook) {
        s->switch_page_id = g_signal_connect(
            bt_plugin->tab_notebook, "switch-page",
            G_CALLBACK(on_page_switched), s);
    }

    return TRUE;
}

/* ── Public API ───────────────────────────────────────────────────── */

gboolean
bt_tmux_available(void)
{
    gchar *path = g_find_program_in_path("tmux");
    if (path) {
        g_free(path);
        return TRUE;
    }
    return FALSE;
}

gchar **
bt_tmux_list_sessions(void)
{
    gchar *out = NULL;
    GError *err = NULL;

    gboolean ok = g_spawn_command_line_sync(
        "tmux list-sessions -F '#{session_name}'",
        &out, NULL, NULL, &err);

    if (!ok || !out) {
        g_clear_error(&err);
        g_free(out);
        return NULL;
    }

    gchar **lines = g_strsplit(g_strstrip(out), "\n", -1);
    g_free(out);

    /* Filter empty strings */
    GPtrArray *result = g_ptr_array_new();
    for (gint i = 0; lines[i]; i++) {
        if (lines[i][0])
            g_ptr_array_add(result, g_strdup(lines[i]));
    }
    g_ptr_array_add(result, NULL);
    g_strfreev(lines);

    return (gchar **)g_ptr_array_free(result, FALSE);
}

BtTmuxSession *
bt_tmux_attach(const gchar *session_name)
{
    if (active_session) {
        g_warning("tmux: already attached to a session");
        return NULL;
    }

    BtTmuxSession *s = session_create(session_name);
    gchar *argv[] = {
        "tmux", "-C", "attach-session", "-t", (gchar *)session_name, NULL
    };

    if (!session_spawn(s, argv)) {
        session_destroy(s);
        return NULL;
    }

    active_session = s;
    return s;
}

BtTmuxSession *
bt_tmux_new_session(const gchar *session_name)
{
    if (active_session) {
        g_warning("tmux: already attached to a session");
        return NULL;
    }

    BtTmuxSession *s = session_create(session_name);
    gchar *argv[] = {
        "tmux", "-C", "new-session", "-s", (gchar *)session_name, NULL
    };

    if (!session_spawn(s, argv)) {
        session_destroy(s);
        return NULL;
    }

    active_session = s;
    return s;
}

void
bt_tmux_detach(BtTmuxSession *s)
{
    if (!s)
        return;

    /* Send detach command — tmux session stays alive */
    send_command(s, "detach\n");

    remove_all_tmux_tabs(s);
    session_destroy(s);

    if (active_session == s)
        active_session = NULL;
}

void
bt_tmux_new_window(BtTmuxSession *s)
{
    if (!s)
        return;
    send_command(s, "new-window\n");
}

void
bt_tmux_send_keys(BtTmuxSession *s, GtkWidget *vte, GdkEventKey *event)
{
    if (!s || !vte)
        return;

    /* Target by window ID — works immediately, no pane mapping needed */
    gint wid = GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(vte), "bt-tmux-wid"));

    guint mod = event->state & gtk_accelerator_get_default_mod_mask();
    guint keyval = event->keyval;
    gboolean ctrl  = (mod & GDK_CONTROL_MASK) != 0;
    gboolean alt   = (mod & GDK_MOD1_MASK) != 0;
    gboolean shift = (mod & GDK_SHIFT_MASK) != 0;

    const char *special = lookup_special_key(keyval);

    if (special) {
        /* Special key — optionally with modifiers */
        GString *cmd = g_string_new(NULL);
        g_string_printf(cmd, "send-keys -t @%d ", wid);
        if (ctrl)  g_string_append(cmd, "C-");
        if (alt)   g_string_append(cmd, "M-");
        if (shift && !g_str_has_prefix(special, "BTab"))
            g_string_append(cmd, "S-");
        g_string_append(cmd, special);
        g_string_append_c(cmd, '\n');
        send_command(s, cmd->str);
        g_string_free(cmd, TRUE);
        return;
    }

    /* Regular character */
    gunichar uc = gdk_keyval_to_unicode(keyval);
    if (uc == 0)
        return;

    if (ctrl) {
        /* Ctrl+key → C-<lowercase> */
        gchar c = (gchar)g_unichar_tolower(uc);
        if (c >= 'a' && c <= 'z') {
            if (alt)
                send_commandf(s, "send-keys -t @%d C-M-%c\n", wid, c);
            else
                send_commandf(s, "send-keys -t @%d C-%c\n", wid, c);
        }
        return;
    }

    if (alt) {
        gchar buf[7];
        gint len = g_unichar_to_utf8(uc, buf);
        buf[len] = '\0';
        send_commandf(s, "send-keys -t @%d M-%s\n", wid, buf);
        return;
    }

    /* Literal character — use send-keys -l with quoting */
    gchar buf[7];
    gint len = g_unichar_to_utf8(uc, buf);
    buf[len] = '\0';

    GString *cmd = g_string_new(NULL);
    g_string_printf(cmd, "send-keys -t @%d -l \"", wid);
    for (gint i = 0; i < len; i++) {
        if (buf[i] == '"' || buf[i] == '\\')
            g_string_append_c(cmd, '\\');
        g_string_append_c(cmd, buf[i]);
    }
    g_string_append(cmd, "\"\n");
    send_command(s, cmd->str);
    g_string_free(cmd, TRUE);
}

void
bt_tmux_paste(BtTmuxSession *s, GtkWidget *vte)
{
    if (!s || !vte)
        return;

    gint wid = GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(vte), "bt-tmux-wid"));

    GtkClipboard *clip = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gchar *text = gtk_clipboard_wait_for_text(clip);
    if (!text || !text[0]) {
        g_free(text);
        return;
    }

    /* Split by newlines, send each line with send-keys -l, then Enter */
    gchar **lines = g_strsplit(text, "\n", -1);
    for (gint i = 0; lines[i]; i++) {
        if (lines[i][0]) {
            /* Escape the line for tmux quoting */
            GString *cmd = g_string_new(NULL);
            g_string_printf(cmd, "send-keys -t @%d -l \"", wid);
            for (const gchar *p = lines[i]; *p; p++) {
                if (*p == '"' || *p == '\\')
                    g_string_append_c(cmd, '\\');
                g_string_append_c(cmd, *p);
            }
            g_string_append(cmd, "\"\n");
            send_command(s, cmd->str);
            g_string_free(cmd, TRUE);
        }

        /* Send Enter between lines (but not after the last line
         * unless the original text ended with a newline) */
        if (lines[i + 1] != NULL) {
            send_commandf(s, "send-keys -t @%d Enter\n", wid);
        }
    }

    g_strfreev(lines);
    g_free(text);
}

void
bt_tmux_on_tab_closed(GtkWidget *vte)
{
    BtTmuxSession *s = g_object_get_data(G_OBJECT(vte), "bt-tmux-session");
    if (!s)
        return;

    gint wid = GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(vte), "bt-tmux-wid"));

    /* Remove from our tracking (don't try to remove the tab again —
     * the caller is already doing that via tab_manager_close_tab) */
    BtTmuxWindow *win = g_hash_table_lookup(
        s->windows_by_wid, GINT_TO_POINTER(wid));
    if (win) {
        /* Prevent remove_tmux_tab from running again */
        win->vte = NULL;
        if (win->pane_id >= 0)
            g_hash_table_remove(s->windows_by_pane,
                                GINT_TO_POINTER(win->pane_id));
        g_hash_table_remove(s->windows_by_wid, GINT_TO_POINTER(wid));
    }

    /* Tell tmux to close the window */
    send_commandf(s, "kill-window -t @%d\n", wid);
}

gboolean
bt_tmux_is_tmux_tab(GtkWidget *widget)
{
    if (!widget)
        return FALSE;
    return g_object_get_data(G_OBJECT(widget), "bt-tmux-session") != NULL;
}

BtTmuxSession *
bt_tmux_get_session(void)
{
    return active_session;
}

void
bt_tmux_shutdown(void)
{
    if (!active_session)
        return;

    BtTmuxSession *s = active_session;
    active_session = NULL;

    /* Send detach so the tmux session survives plugin unload */
    send_command(s, "detach\n");

    /* Don't remove tabs here — bt_cleanup already destroys the notebook */
    session_destroy(s);
}
