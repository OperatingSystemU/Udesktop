/*
 *  desktop-menu-plugin.c - xfce4-panel plugin that displays the desktop menu
 *
 *  Copyright (C) 2004 Brian Tarricone, <bjt23@cornell.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  Contributors:
 *    Jean-Francois Wauthy (option panel for choice between icon/text)
 *    Jasper Huijsmans (menu placement function, toggle button, scaled image
 *                      fixes)
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include <libxfcegui4/libxfcegui4.h>
#include <libxfce4panel/xfce-panel-plugin.h>
#include <libxfce4panel/xfce-panel-convenience.h>

#include "desktop-menu-stub.h"

#define BORDER 8
#define DEFAULT_BUTTON_ICON  DATADIR "/pixmaps/xfce4_xicon1.png"
#define DEFAULT_BUTTON_TITLE "Xfce Menu"

typedef struct _DMPlugin {
    XfcePanelPlugin *plugin;
    
    GtkWidget *button;
    GtkWidget *box;
    GtkWidget *image;
    GtkWidget *label;
    XfceDesktopMenu *desktop_menu;
    gboolean use_default_menu;
    gchar *menu_file;
    gchar *icon_file;
    gboolean show_menu_icons;
    gchar *button_title;
    gboolean show_button_title;
    
    GtkWidget *file_entry;
    GtkWidget *file_fb;
    GtkWidget *icon_entry;
    GtkWidget *icon_fb;
    GtkWidget *icons_chk;
    GtkTooltips *tooltip;  /* needed? */
} DMPlugin;


#if GTK_CHECK_VERSION(2, 6, 0)
/* util */
GtkWidget *
xfutil_custom_button_new(const gchar *text, const gchar *icon)
{
    GtkWidget *btn, *hbox, *img, *lbl;
    
    hbox = gtk_hbox_new(FALSE, 4);
    gtk_widget_show(hbox);
    
    img = gtk_image_new_from_stock(icon, GTK_ICON_SIZE_BUTTON);
    if(img) {
        if(gtk_image_get_storage_type(GTK_IMAGE(img)) != GTK_IMAGE_EMPTY) {
            gtk_widget_show(img);
            gtk_box_pack_start(GTK_BOX(hbox), img, FALSE, FALSE, 0);
        } else
            gtk_widget_destroy(img);
    }
    
    lbl = gtk_label_new_with_mnemonic(text);
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
    
    btn = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(btn), hbox);
    gtk_label_set_mnemonic_widget(GTK_LABEL(lbl), btn);
    
    return btn;
}
#endif

static gchar *
dmp_get_real_path(const gchar *raw_path)
{
    if(!raw_path)
        return NULL;
    
    if(strstr(raw_path, "$XDG_CONFIG_DIRS/") == raw_path)
        return xfce_resource_lookup(XFCE_RESOURCE_CONFIG, raw_path+17);
    else if(strstr(raw_path, "$XDG_CONFIG_HOME/") == raw_path)
        return xfce_resource_save_location(XFCE_RESOURCE_CONFIG, raw_path+17, FALSE);
    else if(strstr(raw_path, "$XDG_DATA_DIRS/") == raw_path)
        return xfce_resource_lookup(XFCE_RESOURCE_DATA, raw_path+15);
    else if(strstr(raw_path, "$XDG_DATA_HOME/") == raw_path)
        return xfce_resource_save_location(XFCE_RESOURCE_DATA, raw_path+15, FALSE);
    else if(strstr(raw_path, "$XDG_CACHE_HOME/") == raw_path)
        return xfce_resource_save_location(XFCE_RESOURCE_CACHE, raw_path+16, FALSE);
    
    return xfce_expand_variables(raw_path, NULL);
}

static GdkPixbuf *
dmp_get_icon(const gchar *icon_name, gint size, GtkOrientation orientation)
{
    GdkPixbuf *pix = NULL;
    gchar *filename;
    gint w, h;
    
    filename = xfce_themed_icon_lookup(icon_name, size);
    if(!filename)
        return NULL;
    
#if GTK_CHECK_VERSION(2, 8, 0)
    w = orientation == GTK_ORIENTATION_HORIZONTAL ? -1 : size;
    h = orientation == GTK_ORIENTATION_VERTICAL ? -1 : size;
    pix = gdk_pixbuf_new_from_file_at_scale(filename, w, h, TRUE, NULL);
#else
    pix = gdk_pixbuf_new_from_file(filename, NULL);
    if(pix) {
        GdkPixbuf *tmp;
        gdouble aspect;
        
        w = gdk_pixbuf_get_width(pix);
        h = gdk_pixbuf_get_height(pix);
        aspect = (gdouble)w / h;
        
        w = orientation == GTK_ORIENTATION_HORIZONTAL ? size*aspect : size;
        h = orientation == GTK_ORIENTATION_VERTICAL ? size*aspect : size;
        
        tmp = gdk_pixbuf_scale_simple(pix, w, h, GDK_INTERP_BILINEAR);
        g_object_unref(G_OBJECT(pix));
        pix = tmp;
    }
#endif
    
    g_free(filename);
    
    return pix;
}

static void
dmp_set_size(XfcePanelPlugin *plugin, gint wsize, DMPlugin *dmp)
{
    gint width, height, size, pix_w = 0, pix_h = 0;
    GtkOrientation orientation = xfce_panel_plugin_get_orientation(plugin);
    
    size = wsize - MAX(GTK_WIDGET(dmp->button)->style->xthickness,
                       GTK_WIDGET(dmp->button)->style->ythickness) - 1;
    
    DBG("wsize: %d, size: %d", wsize, size);
    
    if(dmp->icon_file) {
        GdkPixbuf *pix = dmp_get_icon(dmp->icon_file, size, orientation);
        if(pix) {
            pix_w = gdk_pixbuf_get_width(pix);
            pix_h = gdk_pixbuf_get_height(pix);
            gtk_image_set_from_pixbuf(GTK_IMAGE(dmp->image), pix);
            g_object_unref(G_OBJECT(pix));
        }
    }
    
    width = pix_w + (wsize - size);
    height = pix_h + (wsize - size);
    
    if(dmp->show_button_title) {
        GtkRequisition req;
        
        gtk_widget_size_request(dmp->label, &req);
        if(orientation == GTK_ORIENTATION_HORIZONTAL)
            width += req.width + BORDER/2;
        else {
            width = (width > req.width ? width : req.width
                     + GTK_WIDGET(dmp->label)->style->xthickness);
            height += req.height + BORDER/2;
        }
    }
    
    DBG("width: %d, height: %d", width, height);
    
    gtk_widget_set_size_request(dmp->button, width, height);
}

static void
dmp_set_orientation(XfcePanelPlugin *plugin,
                    GtkOrientation orientation,
                    DMPlugin *dmp)
{
    if(!dmp->show_button_title)
        return;
    
    gtk_widget_set_size_request(dmp->button, -1, -1);
    
    gtk_container_remove(GTK_CONTAINER(dmp->button),
            gtk_bin_get_child(GTK_BIN(dmp->button)));
    
    if(xfce_panel_plugin_get_orientation(plugin) == GTK_ORIENTATION_HORIZONTAL)
        dmp->box = gtk_hbox_new(FALSE, BORDER/2);
    else
        dmp->box = gtk_vbox_new(FALSE, BORDER/2);
    gtk_container_set_border_width(GTK_CONTAINER(dmp->box), 0);
    gtk_widget_show(dmp->box);
    gtk_container_add(GTK_CONTAINER(dmp->button), dmp->box);
    
    gtk_widget_show(dmp->image);
    gtk_box_pack_start(GTK_BOX(dmp->box), dmp->image, TRUE, TRUE, 0);
    gtk_widget_show(dmp->label);
    gtk_box_pack_start(GTK_BOX(dmp->box), dmp->label, TRUE, TRUE, 0);
    
    dmp_set_size(plugin, xfce_panel_plugin_get_size(plugin), dmp);
}

static void
show_title_toggled_cb(GtkToggleButton *tb, gpointer user_data)
{
    DMPlugin *dmp = user_data;
    
    dmp->show_button_title = gtk_toggle_button_get_active(tb);
    
    if(dmp->show_button_title)
        dmp_set_orientation(dmp->plugin, xfce_panel_plugin_get_orientation(dmp->plugin), dmp);
    else {
        gtk_widget_hide(dmp->label);
        dmp_set_size(dmp->plugin, xfce_panel_plugin_get_size(dmp->plugin), dmp);
    }
}

static void
dmp_free(XfcePanelPlugin *plugin, DMPlugin *dmp)
{
    if(dmp->desktop_menu)
        xfce_desktop_menu_destroy(dmp->desktop_menu);
    if(dmp->tooltip)
        gtk_object_sink(GTK_OBJECT(dmp->tooltip));
    
    if(dmp->menu_file)
        g_free(dmp->menu_file);
    if(dmp->icon_file)
        g_free(dmp->icon_file);
    if(dmp->button_title)
        g_free(dmp->button_title);
    
    g_free(dmp);
}

static void
dmp_position_menu (GtkMenu *menu, int *x, int *y, gboolean *push_in, 
                   DMPlugin *dmp)
{
    XfceScreenPosition pos;
    GtkRequisition req;

    gtk_widget_size_request(GTK_WIDGET(menu), &req);

    gdk_window_get_origin (GTK_WIDGET (dmp->plugin)->window, x, y);

    pos = xfce_panel_plugin_get_screen_position(dmp->plugin);

    switch(pos) {
        case XFCE_SCREEN_POSITION_NW_V:
        case XFCE_SCREEN_POSITION_W:
        case XFCE_SCREEN_POSITION_SW_V:
            *x += dmp->button->allocation.width;
            *y += dmp->button->allocation.height - req.height;
            break;
        
        case XFCE_SCREEN_POSITION_NE_V:
        case XFCE_SCREEN_POSITION_E:
        case XFCE_SCREEN_POSITION_SE_V:
            *x -= req.width;
            *y += dmp->button->allocation.height - req.height;
            break;
        
        case XFCE_SCREEN_POSITION_NW_H:
        case XFCE_SCREEN_POSITION_N:
        case XFCE_SCREEN_POSITION_NE_H:
            *y += dmp->button->allocation.height;
            break;
        
        case XFCE_SCREEN_POSITION_SW_H:
        case XFCE_SCREEN_POSITION_S:
        case XFCE_SCREEN_POSITION_SE_H:
            *y -= req.height;
            break;
        
        default:  /* floating */
            gdk_display_get_pointer(gtk_widget_get_display(GTK_WIDGET(dmp->plugin)),
                                                           NULL, x, y, NULL);
    }

    if (*x < 0)
        *x = 0;

    if (*y < 0)
        *y = 0;

    /* TODO: wtf is this ? */
    *push_in = FALSE;
}

static void
menu_deactivated(GtkWidget *menu, gpointer user_data)
{
    int id;
    DMPlugin *dmp = user_data;

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dmp->button), FALSE);

    id = GPOINTER_TO_INT(g_object_get_data (G_OBJECT(menu), "sig_id"));

    g_signal_handler_disconnect(menu, id);
}

static gboolean
dmp_popup(GtkWidget *w,
          GdkEventButton *evt,
          gpointer user_data)
{
    GtkWidget *menu;
    DMPlugin *dmp = user_data;
    
    if(evt->button != 1 || ((evt->state & GDK_CONTROL_MASK)
                            && !(evt->state & (GDK_MOD1_MASK|GDK_SHIFT_MASK
                                               |GDK_MOD4_MASK))))
    {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), FALSE);
        return FALSE;
    }

    if(!dmp->desktop_menu) {
        g_critical("dmp->desktop_menu is NULL - module load failed?");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), FALSE);
        return TRUE;
    }
    
    if(xfce_desktop_menu_need_update(dmp->desktop_menu))
        xfce_desktop_menu_force_regen(dmp->desktop_menu);

    menu = xfce_desktop_menu_get_widget(dmp->desktop_menu);
    if(menu) {
        guint id;
        
        id = g_signal_connect(menu, "deactivate", 
                G_CALLBACK(menu_deactivated), dmp);
        g_object_set_data(G_OBJECT(menu), "sig_id", GUINT_TO_POINTER(id));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), TRUE);
        gtk_menu_popup(GTK_MENU(menu), NULL, NULL,
                       (GtkMenuPositionFunc)dmp_position_menu, dmp,
                       1, gtk_get_current_event_time());
    } else
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), FALSE);

    return TRUE;
}

static void
dmp_set_defaults(DMPlugin *dmp)
{
    dmp->use_default_menu = TRUE;
    dmp->menu_file = NULL;
    dmp->icon_file = g_strdup(DEFAULT_BUTTON_ICON);
    dmp->show_menu_icons = TRUE;
    dmp->button_title = g_strdup(_(DEFAULT_BUTTON_TITLE));
    dmp->show_button_title = TRUE;
}

static void
dmp_read_config(XfcePanelPlugin *plugin, DMPlugin *dmp)
{
    gchar *cfgfile;
    XfceRc *rcfile;
    const gchar *value;
    
    if(!(cfgfile = xfce_panel_plugin_lookup_rc_file(plugin))) {
        dmp_set_defaults(dmp);
        return;
    }
    
    rcfile = xfce_rc_simple_open(cfgfile, TRUE);
    g_free(cfgfile);
    
    if(!rcfile) {
        dmp_set_defaults(dmp);
        return;
    }
    
    dmp->use_default_menu = xfce_rc_read_bool_entry(rcfile, "use_default_menu",
                                                    TRUE);
    
    value = xfce_rc_read_entry(rcfile, "menu_file", NULL);
    if(value) {
        g_free(dmp->menu_file);
        dmp->menu_file = g_strdup(value);
    } else
        dmp->use_default_menu = TRUE;
    
    value = xfce_rc_read_entry(rcfile, "icon_file", NULL);
    if(value) {
        g_free(dmp->icon_file);
        dmp->icon_file = g_strdup(value);
    } else
        dmp->icon_file = g_strdup(DEFAULT_BUTTON_ICON);
    
    dmp->show_menu_icons = xfce_rc_read_bool_entry(rcfile, "show_menu_icons",
                                                   TRUE);
    
    value = xfce_rc_read_entry(rcfile, "button_title", NULL);
    if(value) {
        g_free(dmp->button_title);
        dmp->button_title = g_strdup(value);
    } else
        dmp->button_title = g_strdup(_(DEFAULT_BUTTON_TITLE));
    
    dmp->show_button_title = xfce_rc_read_bool_entry(rcfile,
                                                     "show_button_title",
                                                     TRUE);
}

static void
dmp_write_config(XfcePanelPlugin *plugin, DMPlugin *dmp)
{
    gchar *cfgfile;
    XfceRc *rcfile;
    
    if(!(cfgfile = xfce_panel_plugin_save_location(plugin, TRUE)))
        return;
    
    rcfile = xfce_rc_simple_open(cfgfile, FALSE);
    g_free(cfgfile);
    
    xfce_rc_write_bool_entry(rcfile, "use_default_menu", dmp->use_default_menu);
    xfce_rc_write_entry(rcfile, "menu_file", dmp->menu_file ? dmp->menu_file : "");
    xfce_rc_write_entry(rcfile, "icon_file", dmp->icon_file ? dmp->icon_file : "");
    xfce_rc_write_bool_entry(rcfile, "show_menu_icons", dmp->show_menu_icons);
    xfce_rc_write_entry(rcfile, "button_title", dmp->button_title ? dmp->button_title : "");
    xfce_rc_write_bool_entry(rcfile, "show_button_title", dmp->show_button_title);
    
    xfce_rc_close(rcfile);
}

static gboolean
entry_focus_out_cb(GtkWidget *w, GdkEventFocus *evt, gpointer user_data)
{
    DMPlugin *dmp = user_data;
    const gchar *cur_file;
    
    if(w == dmp->icon_entry) {
        g_free(dmp->icon_file);
        dmp->icon_file = gtk_editable_get_chars(GTK_EDITABLE(w), 0, -1);
        if(dmp->icon_file && *dmp->icon_file)
            dmp_set_size(dmp->plugin, xfce_panel_plugin_get_size(dmp->plugin), dmp);
        else
            gtk_image_set_from_pixbuf(GTK_IMAGE(dmp->image), NULL);            
    } else if(w == dmp->file_entry) {
        if(dmp->menu_file)
            g_free(dmp->menu_file);
        
        dmp->menu_file = gtk_editable_get_chars(GTK_EDITABLE(w), 0, -1);
        if(dmp->desktop_menu) {
            cur_file = xfce_desktop_menu_get_menu_file(dmp->desktop_menu);
            if(strcmp(dmp->menu_file, cur_file)) {
                gchar *path;
                xfce_desktop_menu_destroy(dmp->desktop_menu);
                path = dmp_get_real_path(dmp->menu_file);
                dmp->desktop_menu = xfce_desktop_menu_new(path, TRUE);
                g_free(path);
                if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dmp->icons_chk)))
                    xfce_desktop_menu_set_show_icons(dmp->desktop_menu, FALSE);
            }
        }
    }
    
    return FALSE;
}

static void
filebutton_update_preview_cb(GtkFileChooser *chooser, gpointer user_data)
{
    GtkImage *preview;
    gchar *filename;
    GdkPixbuf *pix = NULL;
    
    preview = GTK_IMAGE(user_data);
    filename = gtk_file_chooser_get_filename(chooser);
    
    if(g_file_test(filename, G_FILE_TEST_IS_REGULAR))
        pix = gdk_pixbuf_new_from_file_at_size(filename, 250, 250, NULL);
    g_free(filename);
    
    if(pix) {
        gtk_image_set_from_pixbuf(preview, pix);
        g_object_unref(G_OBJECT(pix));
    }
    gtk_file_chooser_set_preview_widget_active(chooser, (pix != NULL));
}

static void
filebutton_click_cb(GtkWidget *w, gpointer user_data)
{
    DMPlugin *dmp = user_data;
    GtkWidget *chooser, *image;
    gchar *filename;
    GtkFileFilter *filter;
    const gchar *title;
    gboolean is_icon = FALSE;
    
    if(w == dmp->icon_fb)
        is_icon = TRUE;
    
    if(is_icon)
        title = _("Select Icon");
    else
        title = _("Select Menu File");
    
    chooser = gtk_file_chooser_dialog_new(title,
            GTK_WINDOW(gtk_widget_get_toplevel(w)),
            GTK_FILE_CHOOSER_ACTION_OPEN, GTK_STOCK_CANCEL,
            GTK_RESPONSE_CANCEL, GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
    if(is_icon)
        gtk_file_chooser_add_shortcut_folder(GTK_FILE_CHOOSER(chooser),
                DATADIR "/pixmaps", NULL);
    else
        gtk_file_chooser_add_shortcut_folder(GTK_FILE_CHOOSER(chooser),
                xfce_get_userdir(), NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(chooser), GTK_RESPONSE_ACCEPT);
    
    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("All Files"));
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filter);
    gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(chooser), filter);
    filter = gtk_file_filter_new();
    if(is_icon) {
        gtk_file_filter_set_name(filter, _("Image Files"));
        gtk_file_filter_add_pattern(filter, "*.png");
        gtk_file_filter_add_pattern(filter, "*.jpg");
        gtk_file_filter_add_pattern(filter, "*.bmp");
        gtk_file_filter_add_pattern(filter, "*.svg");
        gtk_file_filter_add_pattern(filter, "*.xpm");
        gtk_file_filter_add_pattern(filter, "*.gif");
    } else {
        gtk_file_filter_set_name(filter, _("Menu Files"));
        gtk_file_filter_add_pattern(filter, "*.xml");
    }
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filter);
    
    if(is_icon) {
        image = gtk_image_new();
        gtk_widget_show(image);
        gtk_file_chooser_set_preview_widget(GTK_FILE_CHOOSER(chooser), image);
        g_signal_connect(G_OBJECT(chooser), "update-preview",
                         G_CALLBACK(filebutton_update_preview_cb), image);
        gtk_file_chooser_set_preview_widget_active(GTK_FILE_CHOOSER(chooser), FALSE);
    }

    gtk_widget_show(chooser);
    if(gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        if(filename) {
            if(is_icon) {
                gtk_entry_set_text(GTK_ENTRY(dmp->icon_entry), filename);
                entry_focus_out_cb(dmp->icon_entry, NULL, dmp);
            } else {
                gtk_entry_set_text(GTK_ENTRY(dmp->file_entry), filename);
                entry_focus_out_cb(dmp->file_entry, NULL, dmp);
            }
            g_free(filename);
        }
    }
    gtk_widget_destroy(chooser);
}

static void
icon_chk_cb(GtkToggleButton *w, gpointer user_data)
{
    DMPlugin *dmp = user_data;
    
    dmp->show_menu_icons = gtk_toggle_button_get_active(w);
    if(dmp->desktop_menu)
        xfce_desktop_menu_set_show_icons(dmp->desktop_menu, dmp->show_menu_icons);
}

static void
dmp_use_desktop_menu_toggled_cb(GtkToggleButton *tb, gpointer user_data)
{
    DMPlugin *dmp = user_data;
    
    if(gtk_toggle_button_get_active(tb)) {
        GtkWidget *hbox;
        
        dmp->use_default_menu = TRUE;
        
        hbox = g_object_get_data(G_OBJECT(tb), "dmp-child-hbox");
        gtk_widget_set_sensitive(hbox, FALSE);
        
        if(dmp->desktop_menu)
            xfce_desktop_menu_destroy(dmp->desktop_menu);
        dmp->desktop_menu = xfce_desktop_menu_new(NULL, TRUE);
    }
}

static void
dmp_use_custom_menu_toggled_cb(GtkToggleButton *tb, gpointer user_data)
{
    DMPlugin *dmp = user_data;
    
    if(gtk_toggle_button_get_active(tb)) {
        GtkWidget *hbox;
        
        dmp->use_default_menu = FALSE;
        
        hbox = g_object_get_data(G_OBJECT(tb), "dmp-child-hbox");
        gtk_widget_set_sensitive(hbox, TRUE);
        
        if(dmp->menu_file) {
            if(dmp->desktop_menu)
                xfce_desktop_menu_destroy(dmp->desktop_menu);
            dmp->desktop_menu = xfce_desktop_menu_new(dmp->menu_file, TRUE);
        }
    }
}

static gboolean
dmp_button_title_focus_out_cb(GtkWidget *w, GdkEventFocus *evt,
        gpointer user_data)
{
    DMPlugin *dmp = user_data;
    
    if(dmp->button_title)
        g_free(dmp->button_title);
    dmp->button_title = gtk_editable_get_chars(GTK_EDITABLE(w), 0, -1);
    
    gtk_tooltips_set_tip(dmp->tooltip, dmp->button, dmp->button_title, NULL);
    gtk_label_set_text(GTK_LABEL(dmp->label), dmp->button_title);
    
    return FALSE;
}

static void
dmp_edit_menu_clicked_cb(GtkWidget *w, gpointer user_data)
{
    DMPlugin *dmp = user_data;
    GError *err = NULL;
    const gchar *menu_file;
    gchar cmd[PATH_MAX];
    
    g_return_if_fail(dmp && dmp->desktop_menu);
    
    menu_file = xfce_desktop_menu_get_menu_file(dmp->desktop_menu);
    if(!menu_file)
        return;
    
    g_snprintf(cmd, PATH_MAX, "%s/xfce4-menueditor \"%s\"", BINDIR, menu_file);
    if(xfce_exec(cmd, FALSE, FALSE, NULL))
        return;
    
    g_snprintf(cmd, PATH_MAX, "xfce4-menueditor \"%s\"", menu_file);
    if(!xfce_exec(cmd, FALSE, FALSE, &err)) {
        xfce_warn(_("Unable to launch xfce4-menueditor: %s"), err->message);
        g_error_free(err);
    }
}

static void
dmp_options_dlg_response_cb(GtkDialog *dialog, gint response, DMPlugin *dmp)
{
    gtk_widget_destroy(GTK_WIDGET(dialog));
    xfce_panel_plugin_unblock_menu(dmp->plugin);
    dmp_write_config(dmp->plugin, dmp);
}

static void
dmp_create_options(XfcePanelPlugin *plugin, DMPlugin *dmp)
{
    GtkWidget *dlg, *header,*topvbox, *vbox, *hbox, *frame, *frame_bin, *spacer;
    GtkWidget *label, *image, *filebutton, *chk, *radio, *entry, *btn;
    
    xfce_textdomain (GETTEXT_PACKAGE, LOCALEDIR, "UTF-8");
    
    xfce_panel_plugin_block_menu(plugin);
    
    dlg = gtk_dialog_new_with_buttons(_("Edit Properties"),
                        GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(plugin))),
                        GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_NO_SEPARATOR,
                        GTK_STOCK_CLOSE, GTK_RESPONSE_ACCEPT, NULL);
    gtk_container_set_border_width(GTK_CONTAINER(dlg), BORDER);
    g_signal_connect(G_OBJECT(dlg), "response",
                     G_CALLBACK(dmp_options_dlg_response_cb), dmp);
    
    header = xfce_create_header(NULL, _("Xfce Menu"));
    gtk_widget_set_size_request(GTK_BIN(header)->child, -1, 32);  /* iconless size hack */
    gtk_container_set_border_width(GTK_CONTAINER(header), BORDER/2);
    gtk_widget_show(header);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), header, FALSE, TRUE, 0);
    
    topvbox = gtk_vbox_new(FALSE, BORDER/2);
    gtk_widget_show(topvbox);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), topvbox, TRUE, TRUE, 0);
    
    frame = xfce_create_framebox(_("Button"), &frame_bin);
    gtk_widget_show(frame);
    gtk_box_pack_start(GTK_BOX(topvbox), frame, FALSE, FALSE, 0);
    
    vbox = gtk_vbox_new(FALSE, BORDER/2);
    gtk_widget_show(vbox);
    gtk_container_add(GTK_CONTAINER(frame_bin), vbox);
    
    hbox = gtk_hbox_new(FALSE, BORDER/2);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), BORDER/2);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
    label = gtk_label_new_with_mnemonic(_("Button _title:"));
    gtk_widget_show(label);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    
    entry = gtk_entry_new();
    if(dmp->button_title)
        gtk_entry_set_text(GTK_ENTRY(entry), dmp->button_title);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), entry);
    gtk_widget_show(entry);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(entry), "focus-out-event",
            G_CALLBACK(dmp_button_title_focus_out_cb), dmp);
    
    chk = gtk_check_button_new_with_mnemonic(_("_Show title in button"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk), dmp->show_button_title);
    gtk_widget_show(chk);
    gtk_box_pack_start(GTK_BOX(vbox), chk, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(chk), "toggled",
            G_CALLBACK(show_title_toggled_cb), dmp);
    
    frame = xfce_create_framebox(_("Menu File"), &frame_bin);
    gtk_widget_show(frame);
    gtk_box_pack_start(GTK_BOX(topvbox), frame, FALSE, FALSE, 0);
    
    vbox = gtk_vbox_new(FALSE, BORDER/2);
    gtk_widget_show(vbox);
    gtk_container_add(GTK_CONTAINER(frame_bin), vbox);
    
    /* 2nd radio button's child hbox */
    hbox = gtk_hbox_new(FALSE, BORDER/2);
    gtk_widget_show(hbox);
    
    radio = gtk_radio_button_new_with_mnemonic(NULL, _("Use default _desktop menu file"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio), dmp->use_default_menu);
    gtk_widget_show(radio);
    gtk_box_pack_start(GTK_BOX(vbox), radio, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(radio), "toggled",
            G_CALLBACK(dmp_use_desktop_menu_toggled_cb), dmp);
    g_object_set_data(G_OBJECT(radio), "dmp-child-hbox", hbox);
    
    radio = gtk_radio_button_new_with_mnemonic_from_widget(GTK_RADIO_BUTTON(radio),
            _("Use _custom menu file:"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio), !dmp->use_default_menu);
    gtk_widget_show(radio);
    gtk_box_pack_start(GTK_BOX(vbox), radio, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(radio), "toggled",
            G_CALLBACK(dmp_use_custom_menu_toggled_cb), dmp);
    g_object_set_data(G_OBJECT(radio), "dmp-child-hbox", hbox);
    
    /* now pack in the child hbox */
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
    spacer = gtk_alignment_new(0.5, 0.5, 1, 1);
    gtk_widget_show(spacer);
    gtk_box_pack_start(GTK_BOX(hbox), spacer, FALSE, FALSE, 0);
    gtk_widget_set_size_request(spacer, 16, -1);
    
    dmp->file_entry = gtk_entry_new();
    if(dmp->menu_file)
        gtk_entry_set_text(GTK_ENTRY(dmp->file_entry), dmp->menu_file);
    else if(dmp->desktop_menu) {
        dmp->menu_file = g_strdup(xfce_desktop_menu_get_menu_file(dmp->desktop_menu));
        gtk_entry_set_text(GTK_ENTRY(dmp->file_entry), dmp->menu_file);
    }
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), dmp->file_entry);
    gtk_widget_set_size_request(dmp->file_entry, 325, -1);  /* FIXME */
    gtk_widget_show(dmp->file_entry);
    gtk_box_pack_start(GTK_BOX(hbox), dmp->file_entry, TRUE, TRUE, 3);
    g_signal_connect(G_OBJECT(dmp->file_entry), "focus-out-event",
            G_CALLBACK(entry_focus_out_cb), dmp);
    
    image = gtk_image_new_from_stock(GTK_STOCK_OPEN, GTK_ICON_SIZE_BUTTON);
    gtk_widget_show(image);
    
    dmp->file_fb = filebutton = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(filebutton), image);
    gtk_widget_show(filebutton);
    gtk_box_pack_end(GTK_BOX(hbox), filebutton, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(filebutton), "clicked",
            G_CALLBACK(filebutton_click_cb), dmp);
    
    gtk_widget_set_sensitive(hbox, !dmp->use_default_menu);
    
    spacer = gtk_alignment_new(0.5, 0.5, 1, 1);
    gtk_widget_show(spacer);
    gtk_box_pack_start(GTK_BOX(vbox), spacer, FALSE, FALSE, 0);
    gtk_widget_set_size_request(spacer, -1, 4);
    
    hbox = gtk_hbox_new(FALSE, BORDER/2);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
#if GTK_CHECK_VERSION(2, 6, 0)
    btn = xfutil_custom_button_new(_("_Edit Menu"), GTK_STOCK_EDIT);
#else
    btn = gtk_button_new_with_mnemonic(_("_Edit Menu"));
#endif
    gtk_widget_show(btn);
    gtk_box_pack_end(GTK_BOX(hbox), btn, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(btn), "clicked",
            G_CALLBACK(dmp_edit_menu_clicked_cb), dmp);
    
    frame = xfce_create_framebox(_("Icons"), &frame_bin);
    gtk_widget_show(frame);
    gtk_box_pack_start(GTK_BOX(topvbox), frame, FALSE, FALSE, 0);
    
    vbox = gtk_vbox_new(FALSE, BORDER/2);
    gtk_widget_show(vbox);
    gtk_container_add(GTK_CONTAINER(frame_bin), vbox);
    
    hbox = gtk_hbox_new(FALSE, BORDER/2);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
    label = gtk_label_new_with_mnemonic(_("_Button icon:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_widget_show(label);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
    
    dmp->icon_entry = gtk_entry_new();
    if(dmp->icon_file)
        gtk_entry_set_text(GTK_ENTRY(dmp->icon_entry), dmp->icon_file);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), dmp->icon_entry);
    gtk_widget_show(dmp->icon_entry);
    gtk_box_pack_start(GTK_BOX(hbox), dmp->icon_entry, TRUE, TRUE, 3);
    g_signal_connect(G_OBJECT(dmp->icon_entry), "focus-out-event",
            G_CALLBACK(entry_focus_out_cb), dmp);
    
    image = gtk_image_new_from_stock(GTK_STOCK_OPEN, GTK_ICON_SIZE_BUTTON);
    gtk_widget_show(image);
    
    dmp->icon_fb = filebutton = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(filebutton), image);
    gtk_widget_show(filebutton);
    gtk_box_pack_end(GTK_BOX(hbox), filebutton, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(filebutton), "clicked",
            G_CALLBACK(filebutton_click_cb), dmp);
    
    dmp->icons_chk = chk = gtk_check_button_new_with_mnemonic(_("Show _icons in menu"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk), dmp->show_menu_icons);
    gtk_widget_show(chk);
    gtk_box_pack_start(GTK_BOX(vbox), chk, FALSE, FALSE, BORDER/2);
    g_signal_connect(G_OBJECT(chk), "toggled", G_CALLBACK(icon_chk_cb), dmp);
    
    gtk_widget_show(dlg);
}

static DMPlugin *
dmp_new(XfcePanelPlugin *plugin)
{
    DMPlugin *dmp = g_new0(DMPlugin, 1);
    dmp->plugin = plugin;
    dmp_read_config(plugin, dmp);
    
    dmp->tooltip = gtk_tooltips_new();
    
    dmp->button = xfce_create_panel_toggle_button();
    gtk_widget_set_name(dmp->button, "xfce-menu-button");
    gtk_widget_show(dmp->button);
    gtk_tooltips_set_tip(dmp->tooltip, dmp->button, dmp->button_title, NULL);
    
    if(xfce_panel_plugin_get_orientation(plugin) == GTK_ORIENTATION_HORIZONTAL)
        dmp->box = gtk_hbox_new(FALSE, BORDER/2);
    else
        dmp->box = gtk_vbox_new(FALSE, BORDER/2);
    gtk_container_set_border_width(GTK_CONTAINER(dmp->box), 0);
    gtk_widget_show(dmp->box);
    gtk_container_add(GTK_CONTAINER(dmp->button), dmp->box);
    
    dmp->image = gtk_image_new();
    g_object_ref(G_OBJECT(dmp->image));
    gtk_widget_show(dmp->image);
    gtk_box_pack_start(GTK_BOX(dmp->box), dmp->image, TRUE, TRUE, 0);
    
    dmp->label = gtk_label_new(dmp->button_title);
    g_object_ref(G_OBJECT(dmp->label));
    if(dmp->show_button_title)
        gtk_widget_show(dmp->label);
    gtk_box_pack_start(GTK_BOX(dmp->box), dmp->label, TRUE, TRUE, 0);
    
    dmp->desktop_menu = xfce_desktop_menu_new(!dmp->use_default_menu
                                                ? dmp->menu_file : NULL,
                                              TRUE);
    if(dmp->desktop_menu) {
        xfce_desktop_menu_set_show_icons(dmp->desktop_menu,
                                         dmp->show_menu_icons);
        xfce_desktop_menu_start_autoregen(dmp->desktop_menu, 10);
    }
    g_signal_connect(G_OBJECT(dmp->button), "button-press-event",
            G_CALLBACK(dmp_popup), dmp);
    
    xfce_panel_plugin_add_action_widget(plugin, dmp->button);
    gtk_container_add(GTK_CONTAINER(plugin), dmp->button);
    
    return dmp;
}

static void
desktop_menu_plugin_construct(XfcePanelPlugin *plugin)
{
    DMPlugin *dmp;
    
    xfce_textdomain(GETTEXT_PACKAGE, LOCALEDIR, "UTF-8");
    
    if(!(dmp = dmp_new(plugin)))
        exit(1);
    
    g_signal_connect(plugin, "free-data",
                     G_CALLBACK(dmp_free), dmp);
    g_signal_connect(plugin, "save",
                     G_CALLBACK(dmp_write_config), dmp);
    g_signal_connect(plugin, "configure-plugin",
                     G_CALLBACK(dmp_create_options), dmp);
    g_signal_connect(plugin, "size-changed",
                     G_CALLBACK(dmp_set_size), dmp);
    g_signal_connect(plugin, "orientation-changed",
                     G_CALLBACK(dmp_set_orientation), dmp);
    
    xfce_panel_plugin_menu_show_configure(plugin);
    
    dmp_set_size(plugin, xfce_panel_plugin_get_size(plugin), dmp);
}

XFCE_PANEL_PLUGIN_REGISTER_EXTERNAL(desktop_menu_plugin_construct)
