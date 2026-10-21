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

// edit header: the bar above the image in the darkroom says which photo
// this is (film roll · file name) and has the one action most edits end
// with, export, using the export module's current settings. It takes the
// place of the collection filters, which belong to the library and are
// hidden here by default (they can be shown again from the panel menu).

#include "common/darktable.h"
#include "common/image.h"
#include "common/image_cache.h"
#include "control/conf.h"
#include "control/control.h"
#include "control/signal.h"
#include "develop/develop.h"
#include "gui/accelerators.h"
#include "gui/gtk.h"
#include "libs/lib.h"
#include "libs/lib_api.h"
#include "views/view.h"

DT_MODULE(1)

typedef struct dt_lib_edit_header_t
{
  GtkWidget *roll;
  GtkWidget *file;
} dt_lib_edit_header_t;

const char *name(dt_lib_module_t *self)
{
  return _("edit header");
}

dt_view_type_flags_t views(dt_lib_module_t *self)
{
  return DT_VIEW_DARKROOM;
}

uint32_t container(dt_lib_module_t *self)
{
  return DT_UI_CONTAINER_PANEL_CENTER_TOP_LEFT;
}

gboolean expandable(dt_lib_module_t *self)
{
  return FALSE;
}

int position(const dt_lib_module_t *self)
{
  return 2000;
}

static void _update(dt_lib_module_t *self)
{
  dt_lib_edit_header_t *d = self->data;
  const dt_imgid_t imgid = darktable.develop ? darktable.develop->image_storage.id : NO_IMGID;
  if(!dt_is_valid_imgid(imgid))
  {
    gtk_label_set_text(GTK_LABEL(d->roll), "");
    gtk_label_set_text(GTK_LABEL(d->file), "");
    return;
  }
  const dt_image_t *img = dt_image_cache_get(imgid, 'r');
  if(!img) return;
  char roll[PATH_MAX] = { 0 };
  dt_image_film_roll(img, roll, sizeof(roll));
  gchar *file = g_strdup(img->filename);
  dt_image_cache_read_release(img);

  gtk_label_set_text(GTK_LABEL(d->roll), roll);
  gtk_label_set_text(GTK_LABEL(d->file), file);
  g_free(file);
}

static void _image_changed(gpointer instance, dt_lib_module_t *self)
{
  _update(self);
}

static void _export_clicked(GtkButton *button, dt_lib_module_t *self)
{
  if(dt_action_process("lib/export/start export", 0, NULL, NULL, 1.0f) == DT_ACTION_NOT_VALID)
    dt_control_log(_("open the export module in the left panel to export"));
}

// the view switch decides the toolbars' visibility before the darkroom is the
// current view, so it reads the library's setting for the filters: apply the
// darkroom's once the switch is done
static gboolean _apply_filter_visibility(gpointer data)
{
  dt_lib_module_t *filter = dt_lib_get_module("filter");
  const dt_view_t *cv = dt_view_manager_get_current_view(darktable.view_manager);
  if(filter && filter->widget && cv && !strcmp(cv->module_name, "darkroom"))
    gtk_widget_set_visible(filter->widget, dt_lib_is_visible_in_view(filter, cv));
  return G_SOURCE_REMOVE;
}

void view_enter(dt_lib_module_t *self, dt_view_t *old_view, dt_view_t *new_view)
{
  g_idle_add(_apply_filter_visibility, NULL);
}

void gui_init(dt_lib_module_t *self)
{
  dt_lib_edit_header_t *d = g_malloc0(sizeof(dt_lib_edit_header_t));
  self->data = d;

  d->roll = gtk_label_new("");
  gtk_label_set_ellipsize(GTK_LABEL(d->roll), PANGO_ELLIPSIZE_START);
  dt_gui_add_class(d->roll, "dt_edit_header_roll");
  GtkWidget *dot = gtk_label_new("·");
  dt_gui_add_class(dot, "dt_edit_header_roll");
  d->file = gtk_label_new("");
  gtk_label_set_ellipsize(GTK_LABEL(d->file), PANGO_ELLIPSIZE_MIDDLE);
  dt_gui_add_class(d->file, "dt_edit_header_file");

  GtkWidget *export = gtk_button_new_with_label(_("export"));
  dt_gui_add_class(export, "dt_edit_header_export");
  gtk_widget_set_tooltip_text(export, _("export this photo with the export module's settings (ctrl+e)"));
  g_signal_connect(export, "clicked", G_CALLBACK(_export_clicked), self);

  self->widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_name(self->widget, "edit-header");
  gtk_box_pack_start(GTK_BOX(self->widget), d->roll, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), dot, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), d->file, FALSE, FALSE, 0);
  gtk_box_pack_end(GTK_BOX(self->widget), export, FALSE, FALSE, 0);
  gtk_widget_set_hexpand(self->widget, TRUE);
  gtk_widget_show_all(self->widget);

  // the library's collection filters make room for this bar in the darkroom
  // (the visibility is stored per darkroom layout, see _get_lib_view_path)
  for(int layout = 0; layout < 3; layout++)
  {
    gchar *key = g_strdup_printf("plugins/darkroom/%d/filter_visible", layout);
    if(!dt_conf_key_exists(key)) dt_conf_set_bool(key, FALSE);
    g_free(key);
  }

  DT_CONTROL_SIGNAL_HANDLE(DT_SIGNAL_DEVELOP_IMAGE_CHANGED, _image_changed);
  DT_CONTROL_SIGNAL_HANDLE(DT_SIGNAL_DEVELOP_INITIALIZE, _image_changed);
  _update(self);
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
