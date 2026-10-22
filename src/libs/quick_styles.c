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

// presets: the styles as a list in the darkroom's left panel, the way
// editors show their presets: click one to apply it to this photo. Styles
// are grouped by the first part of their name ("group|name"), ungrouped ones
// under "your styles".

#include "common/darktable.h"
#include "common/styles.h"
#include "common/utility.h"
#include "control/conf.h"
#include "control/control.h"
#include "control/signal.h"
#include "develop/develop.h"
#include "gui/gtk.h"
#include "libs/lib.h"
#include "libs/lib_api.h"

DT_MODULE(1)

typedef struct dt_lib_quick_styles_t
{
  GtkWidget *entry;
  GtkWidget *list;
  GtkWidget *empty;
} dt_lib_quick_styles_t;

const char *name(dt_lib_module_t *self)
{
  return _("presets");
}

const char *description(dt_lib_module_t *self)
{
  return _("your styles, ready to apply to this photo:\n"
           "click one to apply it");
}

dt_view_type_flags_t views(dt_lib_module_t *self)
{
  return DT_VIEW_DARKROOM;
}

uint32_t container(dt_lib_module_t *self)
{
  return DT_UI_CONTAINER_PANEL_LEFT_CENTER;
}

int position(const dt_lib_module_t *self)
{
  // first in the left panel, above snapshots and history
  return 1100;
}

// the camera-matching styles darktable ships (darktable|camera|…) are
// hundreds of technical starting points, not looks: they stay in the
// styles module of the library
static gboolean _hidden(const char *name)
{
  return g_str_has_prefix(name, "darktable|") || g_str_has_prefix(name, "_l10n_darktable|");
}

static void _row_clicked(GtkButton *button, dt_lib_module_t *self)
{
  const char *style = g_object_get_data(G_OBJECT(button), "style");
  dt_develop_t *dev = darktable.develop;
  if(!style || !dev || !dt_is_valid_imgid(dev->image_storage.id)) return;
  dt_styles_apply_to_dev(style, dev->image_storage.id);
}

static void _fill(dt_lib_module_t *self)
{
  dt_lib_quick_styles_t *d = self->data;
  dt_gui_container_destroy_children(GTK_CONTAINER(d->list));

  const char *filter = gtk_entry_get_text(GTK_ENTRY(d->entry));
  GList *styles = dt_styles_get_list(filter && filter[0] ? filter : "");

  // the ungrouped ones first, as "your styles", then each group
  GHashTable *groups = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  GList *order = NULL;
  for(GList *l = styles; l; l = g_list_next(l))
  {
    const dt_style_t *st = l->data;
    if(_hidden(st->name)) continue;
    const char *bar = strchr(st->name, '|');
    gchar *group = bar ? g_strndup(st->name, bar - st->name) : g_strdup("");
    if(!g_hash_table_contains(groups, group))
    {
      order = g_list_append(order, g_strdup(group));
      g_hash_table_insert(groups, group, GINT_TO_POINTER(1));
    }
    else
      g_free(group);
  }
  order = g_list_sort(order, (GCompareFunc)g_strcmp0);

  for(GList *g = order; g; g = g_list_next(g))
  {
    const char *group = g->data;
    GtkWidget *head = gtk_label_new(NULL);
    gchar *up = g_utf8_strup(group[0] ? dt_util_localize_string(group) : _("your styles"), -1);
    gtk_label_set_text(GTK_LABEL(head), up);
    g_free(up);
    gtk_label_set_xalign(GTK_LABEL(head), 0.0);
    dt_gui_add_class(head, "dt_presets_group");
    gtk_box_pack_start(GTK_BOX(d->list), head, FALSE, FALSE, 0);

    for(GList *l = styles; l; l = g_list_next(l))
    {
      const dt_style_t *st = l->data;
      if(_hidden(st->name)) continue;
      const char *bar = strchr(st->name, '|');
      const size_t glen = bar ? (size_t)(bar - st->name) : 0;
      if(strlen(group) != glen || strncmp(st->name, group, glen)) continue;

      // the last part of the name, localized
      const char *leaf = strrchr(st->name, '|');
      leaf = leaf ? leaf + 1 : st->name;

      GtkWidget *btn = gtk_button_new();
      GtkWidget *lb = gtk_label_new(dt_util_localize_string(leaf));
      gtk_label_set_xalign(GTK_LABEL(lb), 0.0);
      gtk_label_set_ellipsize(GTK_LABEL(lb), PANGO_ELLIPSIZE_END);
      gtk_container_add(GTK_CONTAINER(btn), lb);
      gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
      dt_gui_add_class(btn, "dt_presets_row");
      g_object_set_data_full(G_OBJECT(btn), "style", g_strdup(st->name), g_free);
      g_signal_connect(btn, "clicked", G_CALLBACK(_row_clicked), self);
      gtk_box_pack_start(GTK_BOX(d->list), btn, FALSE, FALSE, 0);
    }
  }

  gtk_widget_set_visible(d->empty, order == NULL);
  gtk_widget_show_all(d->list);

  g_list_free_full(order, g_free);
  g_hash_table_destroy(groups);
  g_list_free_full(styles, dt_style_free);
}

static void _filter_changed(GtkSearchEntry *entry, dt_lib_module_t *self)
{
  _fill(self);
}

static void _styles_changed(gpointer instance, dt_lib_module_t *self)
{
  _fill(self);
}

void gui_init(dt_lib_module_t *self)
{
  dt_lib_quick_styles_t *d = g_malloc0(sizeof(dt_lib_quick_styles_t));
  self->data = d;

  d->entry = gtk_search_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(d->entry), _("search presets"));
  gtk_entry_set_width_chars(GTK_ENTRY(d->entry), 0);
  g_signal_connect(d->entry, "search-changed", G_CALLBACK(_filter_changed), self);

  d->list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_name(d->list, "presets-list");

  d->empty = gtk_label_new(_("no styles yet: create one from the history\n"
                             "module, or import .dtstyle files in the library"));
  gtk_label_set_line_wrap(GTK_LABEL(d->empty), TRUE);
  dt_gui_add_class(d->empty, "dt_presets_empty");
  gtk_widget_set_no_show_all(d->empty, TRUE);

  self->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), d->entry, FALSE, FALSE, 0);
  // no scrolled window of its own: the left panel scrolls
  gtk_box_pack_start(GTK_BOX(self->widget), d->list, TRUE, TRUE, 0);

  // open the first time, as an editor's presets are
  if(!dt_conf_key_exists("plugins/darkroom/quick_styles/expanded"))
    dt_conf_set_bool("plugins/darkroom/quick_styles/expanded", TRUE);
  gtk_box_pack_start(GTK_BOX(self->widget), d->empty, FALSE, FALSE, 0);

  _fill(self);
  DT_CONTROL_SIGNAL_HANDLE(DT_SIGNAL_STYLE_CHANGED, _styles_changed);
  DT_CONTROL_SIGNAL_HANDLE(DT_SIGNAL_DEVELOP_IMAGE_CHANGED, _styles_changed);
  DT_CONTROL_SIGNAL_HANDLE(DT_SIGNAL_DEVELOP_INITIALIZE, _styles_changed);
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
