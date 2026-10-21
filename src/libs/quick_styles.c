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
// editors show their presets. Hover a style for a before/after preview of
// it on this photo, click to apply it. Styles are grouped by the first part
// of their name ("group|name"), ungrouped ones under "your styles".

#include "common/darktable.h"
#include <math.h>
#include "common/styles.h"
#include "common/utility.h"
#include "control/conf.h"
#include "control/control.h"
#include "control/signal.h"
#include "develop/develop.h"
#include "gui/gtk.h"
#include "gui/styles.h"
#include "libs/lib.h"
#include "libs/lib_api.h"

DT_MODULE(1)

typedef struct dt_lib_quick_styles_t
{
  GtkWidget *entry;
  GtkWidget *list;
  GtkWidget *empty;
  // the swatches: each style rendered small on this photo, in the background
  GHashTable *swatches;   // style name -> cairo_surface_t
  dt_imgid_t swatch_imgid;
  GAsyncQueue *todo;      // _swatch_job_t, for the worker
  GThread *worker;
  gint generation;        // a result from an older photo is dropped
} dt_lib_quick_styles_t;

typedef struct _swatch_job_t
{
  dt_lib_module_t *self;
  gchar *style;
  dt_imgid_t imgid;
  gint generation;
  cairo_surface_t *surface;
} _swatch_job_t;

#define SWATCH_RENDER 96

static gboolean _swatch_done(gpointer data)
{
  _swatch_job_t *j = data;
  dt_lib_quick_styles_t *d = j->self->data;
  if(d && j->surface && j->generation == g_atomic_int_get(&d->generation))
  {
    g_hash_table_insert(d->swatches, g_strdup(j->style), j->surface);
    j->surface = NULL;
    gtk_widget_queue_draw(d->list);
  }
  if(j->surface) cairo_surface_destroy(j->surface);
  g_free(j->style);
  g_free(j);
  return G_SOURCE_REMOVE;
}

static gpointer _swatch_worker(gpointer data)
{
  GAsyncQueue *todo = data;
  for(;;)
  {
    _swatch_job_t *j = g_async_queue_pop(todo);
    if(!j->self) // the stop signal
    {
      g_free(j);
      break;
    }
    dt_lib_quick_styles_t *d = j->self->data;
    if(j->generation == g_atomic_int_get(&d->generation))
      j->surface = dt_gui_get_style_preview(j->imgid, j->style, SWATCH_RENDER);
    g_idle_add(_swatch_done, j);
  }
  return NULL;
}

static gboolean _swatch_draw(GtkWidget *w, cairo_t *cr, dt_lib_module_t *self)
{
  dt_lib_quick_styles_t *d = self->data;
  const char *style = g_object_get_data(G_OBJECT(w), "style");
  const int W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w);
  const double r = 4.0;
  cairo_new_sub_path(cr);
  cairo_arc(cr, W - r, r, r, -M_PI / 2, 0);
  cairo_arc(cr, W - r, H - r, r, 0, M_PI / 2);
  cairo_arc(cr, r, H - r, r, M_PI / 2, M_PI);
  cairo_arc(cr, r, r, r, M_PI, 1.5 * M_PI);
  cairo_close_path(cr);
  cairo_clip(cr);

  cairo_surface_t *s = style ? g_hash_table_lookup(d->swatches, style) : NULL;
  if(!s)
  {
    // rendering every style through the pixelpipe made the editor sluggish,
    // so each preset gets a gradient derived from its name instead
    const guint h = style ? g_str_hash(style) : 0;
    const double h1 = (h % 360) / 360.0, h2 = ((h / 360) % 360) / 360.0;
    const double c1[3] = { 0.66 + 0.18 * sin(6.283 * h1), 0.56 + 0.14 * sin(6.283 * h1 + 2.1),
                           0.46 + 0.16 * sin(6.283 * h1 + 4.2) };
    const double c2[3] = { 0.46 + 0.14 * sin(6.283 * h2 + 1.0), 0.40 + 0.12 * sin(6.283 * h2 + 3.0),
                           0.36 + 0.14 * sin(6.283 * h2 + 5.0) };
    cairo_pattern_t *g = cairo_pattern_create_linear(0, 0, W, H);
    cairo_pattern_add_color_stop_rgb(g, 0, CLAMP(c1[0], 0, 1), CLAMP(c1[1], 0, 1), CLAMP(c1[2], 0, 1));
    cairo_pattern_add_color_stop_rgb(g, 1, CLAMP(c2[0], 0, 1), CLAMP(c2[1], 0, 1), CLAMP(c2[2], 0, 1));
    cairo_set_source(cr, g);
    cairo_paint(cr);
    cairo_pattern_destroy(g);
    return TRUE;
  }
  // fill the swatch with the middle of the photo
  const int sw = cairo_image_surface_get_width(s), sh = cairo_image_surface_get_height(s);
  const double scale = MAX((double)W / sw, (double)H / sh);
  cairo_translate(cr, 0.5 * W, 0.5 * H);
  cairo_scale(cr, scale, scale);
  cairo_set_source_surface(cr, s, -0.5 * sw, -0.5 * sh);
  cairo_paint(cr);
  return TRUE;
}

// queue the swatches this photo does not have yet
static void _swatches_request(dt_lib_module_t *self, GList *names)
{
  dt_lib_quick_styles_t *d = self->data;
  // swatches are gradients now; see _swatch_draw
  if(TRUE) return;
  const dt_imgid_t imgid = darktable.develop ? darktable.develop->image_storage.id : NO_IMGID;
  if(!dt_is_valid_imgid(imgid)) return;
  if(imgid != d->swatch_imgid)
  {
    g_atomic_int_inc(&d->generation);
    g_hash_table_remove_all(d->swatches);
    d->swatch_imgid = imgid;
  }
  if(!d->worker) d->worker = g_thread_new("dt-swatches", _swatch_worker, d->todo);
  for(const GList *l = names; l; l = g_list_next(l))
  {
    if(g_hash_table_contains(d->swatches, l->data)) continue;
    _swatch_job_t *j = g_new0(_swatch_job_t, 1);
    j->self = self;
    j->style = g_strdup(l->data);
    j->imgid = imgid;
    j->generation = g_atomic_int_get(&d->generation);
    g_async_queue_push(d->todo, j);
  }
}

const char *name(dt_lib_module_t *self)
{
  return _("presets");
}

const char *description(dt_lib_module_t *self)
{
  return _("your styles, ready to apply to this photo:\n"
           "hover one to preview it, click to apply it");
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

static gboolean _row_tooltip(GtkWidget *widget, gint x, gint y, gboolean keyboard_mode,
                             GtkTooltip *tooltip, gpointer user_data)
{
  const char *style = g_object_get_data(G_OBJECT(widget), "style");
  dt_develop_t *dev = darktable.develop;
  if(!style || !dev || !dt_is_valid_imgid(dev->image_storage.id)) return FALSE;

  // the preview is made from the stored history: store the current one first
  dt_dev_write_history(dev);
  GtkWidget *preview = dt_gui_style_content_dialog((char *)style, dev->image_storage.id);
  if(!preview) return FALSE;
  gtk_widget_show_all(preview);
  gtk_tooltip_set_custom(tooltip, preview);
  return TRUE;
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

  GList *wanted = NULL;

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
      GtkWidget *rowbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
      GtkWidget *sw = gtk_drawing_area_new();
      gtk_widget_set_size_request(sw, DT_PIXEL_APPLY_DPI(34), DT_PIXEL_APPLY_DPI(24));
      gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
      g_object_set_data_full(G_OBJECT(sw), "style", g_strdup(st->name), g_free);
      g_signal_connect(sw, "draw", G_CALLBACK(_swatch_draw), self);
      GtkWidget *lb = gtk_label_new(dt_util_localize_string(leaf));
      gtk_label_set_xalign(GTK_LABEL(lb), 0.0);
      gtk_label_set_ellipsize(GTK_LABEL(lb), PANGO_ELLIPSIZE_END);
      gtk_box_pack_start(GTK_BOX(rowbox), sw, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(rowbox), lb, TRUE, TRUE, 0);
      gtk_container_add(GTK_CONTAINER(btn), rowbox);
      wanted = g_list_prepend(wanted, g_strdup(st->name));
      gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
      dt_gui_add_class(btn, "dt_presets_row");
      g_object_set_data_full(G_OBJECT(btn), "style", g_strdup(st->name), g_free);
      gtk_widget_set_has_tooltip(btn, TRUE);
      g_signal_connect(btn, "query-tooltip", G_CALLBACK(_row_tooltip), NULL);
      g_signal_connect(btn, "clicked", G_CALLBACK(_row_clicked), self);
      gtk_box_pack_start(GTK_BOX(d->list), btn, FALSE, FALSE, 0);
    }
  }

  gtk_widget_set_visible(d->empty, order == NULL);
  gtk_widget_show_all(d->list);

  wanted = g_list_reverse(wanted);
  _swatches_request(self, wanted);
  g_list_free_full(wanted, g_free);

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
  d->swatches = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                      (GDestroyNotify)cairo_surface_destroy);
  d->swatch_imgid = NO_IMGID;
  d->todo = g_async_queue_new();

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
  dt_lib_quick_styles_t *d = self->data;
  if(d && d->worker)
  {
    g_atomic_int_inc(&d->generation);
    g_async_queue_push_front(d->todo, g_new0(_swatch_job_t, 1)); // stop
    g_thread_join(d->worker);
  }
  if(d)
  {
    g_hash_table_destroy(d->swatches);
    g_async_queue_unref(d->todo);
  }
  g_free(self->data);
  self->data = NULL;
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode c; remove-trailing-spaces modified;
// clang-format on
