#include "reparent.h"

void
reparent_inject_paned(BottomTermPlugin *bt)
{
    GtkWidget *notebook = geany_data->main_widgets->notebook;
    GtkWidget *parent = gtk_widget_get_parent(notebook);

    g_return_if_fail(parent != NULL);

    bt->original_parent = parent;

    /* Protect notebook from destruction during reparenting */
    g_object_ref(notebook);

    if (GTK_IS_PANED(parent)) {
        bt->was_child1 = (gtk_paned_get_child1(GTK_PANED(parent)) == notebook);
        gtk_container_remove(GTK_CONTAINER(parent), notebook);

        bt->paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
        gtk_paned_pack1(GTK_PANED(bt->paned), notebook, TRUE, FALSE);
        gtk_paned_pack2(GTK_PANED(bt->paned), bt->tab_notebook, FALSE, TRUE);

        if (bt->was_child1)
            gtk_paned_pack1(GTK_PANED(parent), bt->paned, TRUE, FALSE);
        else
            gtk_paned_pack2(GTK_PANED(parent), bt->paned, TRUE, FALSE);
    } else {
        /* Generic container fallback */
        gtk_container_remove(GTK_CONTAINER(parent), notebook);

        bt->paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
        gtk_paned_pack1(GTK_PANED(bt->paned), notebook, TRUE, FALSE);
        gtk_paned_pack2(GTK_PANED(bt->paned), bt->tab_notebook, FALSE, TRUE);

        gtk_container_add(GTK_CONTAINER(parent), bt->paned);
    }

    g_object_unref(notebook);

    /* Set initial split: 70% document, 30% terminal */
    GtkAllocation alloc;
    gtk_widget_get_allocation(bt->original_parent, &alloc);
    if (alloc.height > 100)
        gtk_paned_set_position(GTK_PANED(bt->paned), alloc.height * 7 / 10);
    else
        gtk_paned_set_position(GTK_PANED(bt->paned), 400);

    gtk_widget_show_all(bt->paned);
}

void
reparent_restore(BottomTermPlugin *bt)
{
    if (!bt->paned || !bt->original_parent)
        return;

    GtkWidget *notebook = geany_data->main_widgets->notebook;

    if (!GTK_IS_WIDGET(notebook) || !GTK_IS_WIDGET(bt->paned))
        return;

    /*
     * Teardown order matters:
     * 1. Set shutting_down so child-exited handlers don't create new tabs
     * 2. Remove the tab_notebook from our paned (ref it so it survives)
     * 3. Destroy the tab_notebook explicitly (kills all VTE terminals)
     * 4. Remove Geany's notebook from our paned
     * 5. Remove our paned from the original parent
     * 6. Re-add Geany's notebook to its original parent
     */

    bt->shutting_down = TRUE;

    /* Step 1: Detach and destroy the terminal notebook.
     * Ref it first so removing from paned doesn't destroy it mid-iteration. */
    if (bt->tab_notebook && GTK_IS_WIDGET(bt->tab_notebook)) {
        g_object_ref(bt->tab_notebook);
        gtk_container_remove(GTK_CONTAINER(bt->paned), bt->tab_notebook);
        gtk_widget_destroy(bt->tab_notebook);
        g_object_unref(bt->tab_notebook);
        bt->tab_notebook = NULL;
    }

    /* Step 2: Detach Geany's notebook from our paned */
    g_object_ref(notebook);
    gtk_container_remove(GTK_CONTAINER(bt->paned), notebook);

    /* Step 3: Remove our (now empty) paned from the original parent */
    if (GTK_IS_WIDGET(bt->original_parent))
        gtk_container_remove(GTK_CONTAINER(bt->original_parent), bt->paned);

    /* Step 4: Restore notebook to its original position */
    if (GTK_IS_PANED(bt->original_parent)) {
        if (bt->was_child1)
            gtk_paned_pack1(GTK_PANED(bt->original_parent), notebook, TRUE, FALSE);
        else
            gtk_paned_pack2(GTK_PANED(bt->original_parent), notebook, TRUE, FALSE);
    } else if (GTK_IS_WIDGET(bt->original_parent)) {
        gtk_container_add(GTK_CONTAINER(bt->original_parent), notebook);
    }

    g_object_unref(notebook);
    gtk_widget_show_all(notebook);

    bt->paned = NULL;
    bt->original_parent = NULL;
}
