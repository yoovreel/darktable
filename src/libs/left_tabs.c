/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

// left tabs: presets, history and snapshots as three tabs at the top of the
// darkroom's left panel. One of them is shown, open and without its own
// header; the other two are hidden until their tab is picked.

#include "common/darktable.h"
#include "control/conf.h"
#include "dtgtk/expander.h"
#include "gui/gtk.h"
#include "libs/lib.h"
#include "libs/lib_api.h"
#include "views/view.h"

DT_MODULE(1)

#define N_TABS 3
static const char *_libs[N_TABS] = { "quick_styles", "history", "snapshots" };
static const char *_labels[N_TABS] = { N_("presets"), N_("history"), N_("snapshots") };

typedef struct dt_lib_left_tabs_t
{
  GtkWidget *tabs[N_TABS];
} dt_lib_left_tabs_t;

const char *name(dt_lib_module_t *self)
{
  return _("panel tabs");
}

dt_view_type_flags_t views(dt_lib_module_t *self)
{
  return DT_VIEW_DARKROOM;
}

uint32_t container(dt_lib_module_t *self)
{
  return DT_UI_CONTAINER_PANEL_LEFT_CENTER;
}

gboolean expandable(dt_lib_module_t *self)
{
  return FALSE;
}

int position(const dt_lib_module_t *self)
{
  // first in the left panel
  return 1200;
}

static void _apply(dt_lib_module_t *self)
{
  dt_lib_left_tabs_t *d = self->data;
  const int cur = CLAMP(dt_conf_get_int("plugins/darkroom/left_tabs/current"), 0, N_TABS - 1);
  for(int i = 0; i < N_TABS; i++)
  {
    if(i == cur)
      dt_gui_add_class(d->tabs[i], "dt_tab_active");
    else
      dt_gui_remove_class(d->tabs[i], "dt_tab_active");

    dt_lib_module_t *m = dt_lib_get_module(_libs[i]);
    if(!m || !m->expander) continue;
    GtkWidget *header = dtgtk_expander_get_header(DTGTK_EXPANDER(m->expander));
    if(i == cur)
    {
      dt_lib_gui_set_expanded(m, TRUE);
      gtk_widget_show(m->expander);
      // the tab is its header
      if(header) gtk_widget_hide(header);
    }
    else
      gtk_widget_hide(m->expander);
  }
}

static gboolean _apply_idle(gpointer user_data)
{
  const dt_view_t *cv = dt_view_manager_get_current_view(darktable.view_manager);
  if(cv && !strcmp(cv->module_name, "darkroom")) _apply(user_data);
  return G_SOURCE_REMOVE;
}

static void _tab_clicked(GtkButton *button, dt_lib_module_t *self)
{
  dt_conf_set_int("plugins/darkroom/left_tabs/current",
                  GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "tab")));
  _apply(self);
}

void view_enter(dt_lib_module_t *self, dt_view_t *old_view, dt_view_t *new_view)
{
  // after the view has shown the panel's libs
  g_idle_add(_apply_idle, self);
}

void view_leave(dt_lib_module_t *self, dt_view_t *old_view, dt_view_t *new_view)
{
  // give the libs their headers back for the other views and layouts
  for(int i = 0; i < N_TABS; i++)
  {
    dt_lib_module_t *m = dt_lib_get_module(_libs[i]);
    if(!m || !m->expander) continue;
    GtkWidget *header = dtgtk_expander_get_header(DTGTK_EXPANDER(m->expander));
    if(header) gtk_widget_show(header);
  }
}

void gui_init(dt_lib_module_t *self)
{
  dt_lib_left_tabs_t *d = g_malloc0(sizeof(dt_lib_left_tabs_t));
  self->data = d;

  self->widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_set_name(self->widget, "left-tabs");
  gtk_box_set_homogeneous(GTK_BOX(self->widget), TRUE);
  for(int i = 0; i < N_TABS; i++)
  {
    GtkWidget *b = gtk_button_new_with_label(_(_labels[i]));
    dt_gui_add_class(b, "dt_tab");
    g_object_set_data(G_OBJECT(b), "tab", GINT_TO_POINTER(i));
    g_signal_connect(b, "clicked", G_CALLBACK(_tab_clicked), self);
    gtk_box_pack_start(GTK_BOX(self->widget), b, TRUE, TRUE, 0);
    d->tabs[i] = b;
  }
  gtk_widget_show_all(self->widget);
}

void gui_cleanup(dt_lib_module_t *self)
{
  g_free(self->data);
  self->data = NULL;
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode c; remove-trailing-spaces modified;
// clang-format on
