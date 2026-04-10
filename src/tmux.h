#ifndef BT_TMUX_H
#define BT_TMUX_H

#include <gtk/gtk.h>
#include <vte/vte.h>

typedef struct _BtTmuxSession BtTmuxSession;

/* Check if tmux binary exists in PATH */
gboolean        bt_tmux_available       (void);

/* List existing tmux sessions (NULL-terminated array, free with g_strfreev) */
gchar         **bt_tmux_list_sessions   (void);

/* Attach to existing session or create a new one via tmux -CC */
BtTmuxSession  *bt_tmux_attach         (const gchar *session_name);
BtTmuxSession  *bt_tmux_new_session    (const gchar *session_name);

/* Detach from session (leaves tmux session alive) */
void            bt_tmux_detach          (BtTmuxSession *session);

/* Create a new tmux window (appears as a new tab) */
void            bt_tmux_new_window      (BtTmuxSession *session);

/* Forward a keypress from a tmux VTE tab to tmux */
void            bt_tmux_send_keys       (BtTmuxSession *session,
                                         GtkWidget *vte,
                                         GdkEventKey *event);

/* Forward clipboard paste to tmux */
void            bt_tmux_paste           (BtTmuxSession *session,
                                         GtkWidget *vte);

/* Called when a tmux tab is closed from the UI (sends kill-window) */
void            bt_tmux_on_tab_closed   (GtkWidget *vte);

/* Check if a notebook page is a tmux-managed tab */
gboolean        bt_tmux_is_tmux_tab     (GtkWidget *widget);

/* Get the current active session (NULL if none) */
BtTmuxSession  *bt_tmux_get_session     (void);

/* Cleanup everything on plugin unload */
void            bt_tmux_shutdown        (void);

#endif /* BT_TMUX_H */
