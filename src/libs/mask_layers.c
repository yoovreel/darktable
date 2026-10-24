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

// masks: every masked adjustment of this photo in one list, like layers.
// A row is a module instance with a drawn, parametric or raster mask; click
// it to go to it with its mask panel open, use its switch to turn it on or
// off. "new mask" starts one more (a masked exposure instance).

#include "common/darktable.h"
#include "control/conf.h"
#include "control/control.h"
#include "control/signal.h"
#include "develop/blend.h"
#include "develop/develop.h"
#include "develop/imageop.h"
#include "dtgtk/button.h"
#include "dtgtk/paint.h"
#include "dtgtk/togglebutton.h"
#include "gui/gtk.h"
#include "libs/lib.h"
#include "libs/lib_api.h"

DT_MODULE(1)

typedef struct dt_lib_mask_layers_t
{
  GtkWidget *list;
  GtkWidget *empty;
  guint refresh;
  guint poll;
  gchar *signature;
  gboolean subtract; // the parts drawn next are subtracted
} dt_lib_mask_layers_t;

const char *name(dt_lib_module_t *self)
{
  return _("masks");
}

const char *description(dt_lib_module_t *self)
{
  return _("the masked adjustments of this photo, like layers:\n"
           "click one to edit it, or start a new mask");
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
  // under snapshots (1000) and history (900)
  return 880;
}

static gboolean _has_mask(const dt_iop_module_t *m)
{
  return m->blend_params
         && (m->blend_params->mask_mode & (DEVELOP_MASK_MASK | DEVELOP_MASK_FLEXI
                                           | DEVELOP_MASK_CONDITIONAL | DEVELOP_MASK_RASTER));
}

static void _row_clicked(GtkButton *button, dt_lib_module_t *self)
{
  dt_iop_module_t *m = g_object_get_data(G_OBJECT(button), "module");
  dt_develop_t *dev = darktable.develop;
  if(!m || !dev || !g_list_find(dev->iop, m)) return;
  dt_dev_modulegroups_switch(dev, m);
  dt_iop_request_focus(m);
  dt_iop_gui_set_expanded(m, TRUE, TRUE);
  dt_iop_gui_blend_masks_panel_open();
  dt_iop_gui_scroll_into_view_later(m->expander);
}

static void _switch_toggled(GtkToggleButton *button, dt_lib_module_t *self)
{
  dt_iop_module_t *m = g_object_get_data(G_OBJECT(button), "module");
  dt_develop_t *dev = darktable.develop;
  if(!m || !dev || !g_list_find(dev->iop, m) || !m->off) return;
  const gboolean on = gtk_toggle_button_get_active(button);
  if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->off)) != on)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->off), on);
}

static void _mask_switch_toggled(GtkToggleButton *button, dt_lib_module_t *self)
{
  GList *members = g_object_get_data(G_OBJECT(button), "members");
  dt_develop_t *dev = darktable.develop;
  if(!dev) return;
  const gboolean on = gtk_toggle_button_get_active(button);
  for(const GList *l = members; l; l = g_list_next(l))
  {
    dt_iop_module_t *m = l->data;
    if(!g_list_find(dev->iop, m) || !m->off) continue;
    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->off)) != on)
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->off), on);
  }
}

static void _new_clicked(GtkButton *button, dt_lib_module_t *self)
{
  if(dt_iop_gui_blend_new_mask_adjustment())
    dt_iop_gui_blend_masks_panel_open();
  else
    dt_control_log(_("could not start a new mask"));
}

// what the open mask is made of, and the buttons that change it
static const struct { const char *label; int shape; } _part_buttons[] = {
  { N_("brush"), 1 },  { N_("linear"), 4 }, { N_("radial"), 2 },
  { N_("ellipse"), 3 }, { N_("path"), 0 },
#ifdef HAVE_AI
  { N_("AI object"), 5 },
#endif
};

static void _part_clicked(GtkButton *button, dt_lib_module_t *self)
{
  dt_lib_mask_layers_t *d = self->data;
  dt_iop_module_t *m = g_object_get_data(G_OBJECT(button), "module");
  if(!m || !g_list_find(darktable.develop->iop, m)) return;
  const int shape = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "shape"));
  if(dt_iop_gui_blend_mask_add_part(m, shape, d->subtract))
    dt_control_log(d->subtract ? _("draw what to take out of the mask")
                               : _("draw what to add to the mask"));
}

static void _mode_clicked(GtkButton *button, dt_lib_module_t *self)
{
  dt_lib_mask_layers_t *d = self->data;
  d->subtract = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "subtract"));
  g_free(d->signature);
  d->signature = NULL; // rebuild, to show the mode
}

static void _invert_clicked(GtkButton *button, dt_lib_module_t *self)
{
  dt_iop_module_t *m = g_object_get_data(G_OBJECT(button), "module");
  if(m && g_list_find(darktable.develop->iop, m)) dt_iop_gui_blend_mask_invert(m);
}

static GtkWidget *_parts_box(dt_lib_module_t *self, dt_iop_module_t *m)
{
  dt_lib_mask_layers_t *d = self->data;
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  dt_gui_add_class(box, "dt_mask_parts");

  GList *parts = dt_iop_gui_blend_mask_parts(m);
  for(const GList *l = parts; l; l = g_list_next(l))
  {
    const char *p = l->data;
    const gboolean minus = g_str_has_prefix(p, "−");
    const char *name = p + (minus ? strlen("−") : 1);
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *op = gtk_label_new(minus ? _("subtract") : _("add"));
    dt_gui_add_class(op, minus ? "dt_mask_part_minus" : "dt_mask_part_plus");
    GtkWidget *nm = gtk_label_new(name);
    gtk_label_set_xalign(GTK_LABEL(nm), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(nm), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(row), op, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), nm, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
  }
  if(!parts)
  {
    GtkWidget *e = gtk_label_new(_("empty: add a part below"));
    gtk_label_set_xalign(GTK_LABEL(e), 0.0);
    dt_gui_add_class(e, "dt_presets_empty");
    gtk_box_pack_start(GTK_BOX(box), e, FALSE, FALSE, 0);
  }
  g_list_free_full(parts, g_free);

  // add | subtract | invert
  GtkWidget *modes = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  for(int i = 0; i < 2; i++)
  {
    GtkWidget *b = gtk_button_new_with_label(i ? _("subtract") : _("add"));
    dt_gui_add_class(b, "dt_mask_mode");
    if(d->subtract == i) dt_gui_add_class(b, "dt_mask_mode_active");
    gtk_widget_set_tooltip_text(b, i ? _("the next part drawn is taken out of the mask")
                                     : _("the next part drawn is added to the mask"));
    g_object_set_data(G_OBJECT(b), "subtract", GINT_TO_POINTER(i));
    g_signal_connect(b, "clicked", G_CALLBACK(_mode_clicked), self);
    gtk_box_pack_start(GTK_BOX(modes), b, FALSE, FALSE, 0);
  }
  GtkWidget *inv = gtk_button_new_with_label(_("invert"));
  dt_gui_add_class(inv, "dt_mask_mode");
  gtk_widget_set_tooltip_text(inv, _("swap what the mask selects and what it leaves out"));
  g_object_set_data(G_OBJECT(inv), "module", m);
  g_signal_connect(inv, "clicked", G_CALLBACK(_invert_clicked), self);
  gtk_box_pack_start(GTK_BOX(modes), inv, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), modes, FALSE, FALSE, 4);

  // the parts to draw
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 4);
  gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
  for(int i = 0; i < (int)G_N_ELEMENTS(_part_buttons); i++)
  {
    GtkWidget *b = gtk_button_new_with_label(_(_part_buttons[i].label));
    dt_gui_add_class(b, "dt_mask_part_btn");
    g_object_set_data(G_OBJECT(b), "module", m);
    g_object_set_data(G_OBJECT(b), "shape", GINT_TO_POINTER(_part_buttons[i].shape));
    g_signal_connect(b, "clicked", G_CALLBACK(_part_clicked), self);
    gtk_grid_attach(GTK_GRID(grid), b, i % 3, i / 3, 1, 1);
  }
  gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);
  return box;
}

// a mask is the name its adjustments share: an adjustment created for a mask
// is named after it (see blend_gui.c). A masked module with no name of its
// own is a mask by itself
static gchar *_mask_key(const dt_iop_module_t *m)
{
  if(m->multi_name_hand_edited && m->multi_name[0])
    return dt_util_localize_segmented_name(m->multi_name, FALSE);
  return NULL;
}

// the adjustments of the mask that holds the module `m`, top of the pipe first
static GList *_mask_members(const dt_iop_module_t *m)
{
  GList *res = NULL;
  gchar *key = _mask_key(m);
  dt_develop_t *dev = darktable.develop;
  for(const GList *l = dev ? g_list_last(dev->iop) : NULL; l; l = g_list_previous(l))
  {
    dt_iop_module_t *o = l->data;
    if(!o->expander || !_has_mask(o) || dt_iop_is_hidden(o)) continue;
    gchar *k = _mask_key(o);
    if(o == m || (key && !g_strcmp0(k, key))) res = g_list_append(res, o);
    g_free(k);
  }
  g_free(key);
  return res;
}

static void _delete_clicked(GtkButton *button, dt_lib_module_t *self)
{
  dt_iop_module_t *m = g_object_get_data(G_OBJECT(button), "module");
  dt_develop_t *dev = darktable.develop;
  if(!m || !dev || !g_list_find(dev->iop, m)) return;
  GList *members = _mask_members(m);
  const int n = g_list_length(members);
  gchar *key = _mask_key(m);
  const gboolean yes = dt_gui_show_yes_no_dialog(
      _("delete mask"), "mask_delete",
      n > 1 ? _("delete the mask \"%s\" and its %d adjustments?")
            : _("delete the mask \"%s\" and its adjustment?"),
      key ? key : m->name(), n);
  g_free(key);
  if(yes)
    for(GList *l = members; l; l = g_list_next(l))
    {
      dt_iop_module_t *o = l->data;
      if(g_list_find(dev->iop, o)) dt_iop_gui_delete(o);
    }
  g_list_free(members);
}

static void _fill(dt_lib_module_t *self)
{
  dt_lib_mask_layers_t *d = self->data;
  dt_gui_container_destroy_children(GTK_CONTAINER(d->list));
  dt_develop_t *dev = darktable.develop;
  int n = 0;
  const dt_iop_module_t *focus = dt_dev_gui_module();
  GList *done = NULL; // the modules already shown as part of a mask

  // top of the pipe first, as layers are listed
  for(const GList *l = dev ? g_list_last(dev->iop) : NULL; l; l = g_list_previous(l))
  {
    dt_iop_module_t *m = l->data;
    if(!m->expander || !_has_mask(m) || dt_iop_is_hidden(m)) continue;
    if(g_list_find(done, m)) continue;

    GList *members = _mask_members(m);
    done = g_list_concat(done, g_list_copy(members));
    gboolean active = FALSE, any_on = FALSE;
    dt_iop_module_t *anchor = m;
    for(const GList *k = members; k; k = g_list_next(k))
    {
      dt_iop_module_t *o = k->data;
      if(o == focus) { active = TRUE; anchor = o; }
      if(o->enabled) any_on = TRUE;
    }

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    dt_gui_add_class(row, "dt_mask_layer");

    GtkWidget *sw = dtgtk_togglebutton_new(dtgtk_cairo_paint_eye, 0, NULL);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sw), any_on);
    gtk_widget_set_tooltip_text(sw, _("show or hide this mask's adjustments"));
    dt_gui_add_class(sw, "dt_transparent_background");

    gchar *key = _mask_key(m);
    GtkWidget *btn = gtk_button_new();
    GtkWidget *lb = gtk_label_new(NULL);
    gchar *text = key ? g_markup_printf_escaped("%s", key)
                      : g_markup_printf_escaped("%s", m->name());
    gtk_label_set_markup(GTK_LABEL(lb), text);
    g_free(text);
    g_free(key);
    gtk_label_set_xalign(GTK_LABEL(lb), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(lb), PANGO_ELLIPSIZE_END);
    gtk_container_add(GTK_CONTAINER(btn), lb);
    gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(btn, _("select this mask: its adjustments, parts and sliders"));
    g_object_set_data(G_OBJECT(btn), "module", anchor);
    g_signal_connect(btn, "clicked", G_CALLBACK(_row_clicked), self);
    if(active) dt_gui_add_class(row, "dt_mask_layer_active");

    // a swatch for the mask, in the overlay's color
    GtkWidget *swatch = gtk_label_new(NULL);
    dt_gui_add_class(swatch, "dt_mask_layer_swatch");
    gtk_widget_set_valign(swatch, GTK_ALIGN_CENTER);

    gtk_box_pack_start(GTK_BOX(row), swatch, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), btn, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(row), sw, FALSE, FALSE, 0);
    // one switch for every adjustment of the mask
    g_object_set_data_full(G_OBJECT(sw), "members", g_list_copy(members), (GDestroyNotify)g_list_free);
    g_signal_connect(sw, "toggled", G_CALLBACK(_mask_switch_toggled), self);

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    dt_gui_add_class(card, "dt_mask_layer_card");
    gtk_box_pack_start(GTK_BOX(card), row, FALSE, FALSE, 0);
    if(active)
    {
      dt_gui_add_class(card, "dt_mask_layer_card_active");
      gtk_box_pack_start(GTK_BOX(card), _parts_box(self, anchor), FALSE, FALSE, 0);

      // the module instances that belong to this mask: each stays a module
      // of its own, to open, switch off or move in the pipe
      GtkWidget *inst = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
      dt_gui_add_class(inst, "dt_mask_instances");
      GtkWidget *head = gtk_label_new(_("adjustments"));
      gtk_label_set_xalign(GTK_LABEL(head), 0.0);
      dt_gui_add_class(head, "dt_presets_empty");
      gtk_box_pack_start(GTK_BOX(inst), head, FALSE, FALSE, 0);
      for(const GList *k = members; k; k = g_list_next(k))
      {
        dt_iop_module_t *o = k->data;
        GtkWidget *ir = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        GtkWidget *ib = gtk_button_new_with_label(o->name());
        gtk_button_set_relief(GTK_BUTTON(ib), GTK_RELIEF_NONE);
        gtk_widget_set_tooltip_text(ib, _("open this module"));
        g_object_set_data(G_OBJECT(ib), "module", o);
        g_signal_connect(ib, "clicked", G_CALLBACK(_row_clicked), self);
        if(o == focus) dt_gui_add_class(ir, "dt_mask_layer_active");
        GtkWidget *isw = dtgtk_togglebutton_new(dtgtk_cairo_paint_eye, 0, NULL);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(isw), o->enabled);
        dt_gui_add_class(isw, "dt_transparent_background");
        g_object_set_data(G_OBJECT(isw), "module", o);
        g_signal_connect(isw, "toggled", G_CALLBACK(_switch_toggled), self);
        gtk_box_pack_start(GTK_BOX(ir), ib, TRUE, TRUE, 0);
        gtk_box_pack_end(GTK_BOX(ir), isw, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(inst), ir, FALSE, FALSE, 0);
      }
      gtk_box_pack_start(GTK_BOX(card), inst, FALSE, FALSE, 4);

      GtkWidget *del = gtk_button_new_with_label(_("delete mask"));
      dt_gui_add_class(del, "dt_mask_mode");
      gtk_widget_set_tooltip_text(del, _("delete this mask and, if you agree, its adjustments"));
      g_object_set_data(G_OBJECT(del), "module", anchor);
      g_signal_connect(del, "clicked", G_CALLBACK(_delete_clicked), self);
      gtk_box_pack_start(GTK_BOX(card), del, FALSE, FALSE, 4);
    }
    gtk_box_pack_start(GTK_BOX(d->list), card, FALSE, FALSE, 0);
    g_list_free(members);
    n++;
  }
  g_list_free(done);

  gtk_widget_set_visible(d->empty, n == 0);
  gtk_widget_show_all(d->list);
}

// what the list shows, to rebuild it only when that changes. Mask edits do
// not all raise a signal, so this is also checked on a timer
static gchar *_signature(void)
{
  GString *sig = g_string_new(NULL);
  dt_develop_t *dev = darktable.develop;
  for(const GList *l = dev ? dev->iop : NULL; l; l = g_list_next(l))
  {
    const dt_iop_module_t *m = l->data;
    if(!m->expander || !_has_mask(m)) continue;
    g_string_append_printf(sig, "%p:%d:%s:%d|", (void *)m, m->enabled, m->multi_name,
                           m == dt_dev_gui_module());
    if(m == dt_dev_gui_module())
    {
      GList *parts = dt_iop_gui_blend_mask_parts((dt_iop_module_t *)m);
      for(const GList *p = parts; p; p = g_list_next(p))
        g_string_append_printf(sig, "%s;", (const char *)p->data);
      g_list_free_full(parts, g_free);
    }
  }
  return g_string_free(sig, FALSE);
}

// rebuild the list only when what it shows has changed: every slider drag
// raises a history change, and rebuilding the list each time made the whole
// panel stutter
static void _fill_if_changed(dt_lib_module_t *self)
{
  dt_lib_mask_layers_t *d = self->data;
  gchar *sig = _signature();
  if(g_strcmp0(sig, d->signature))
  {
    g_free(d->signature);
    d->signature = sig;
    _fill(self);
  }
  else
    g_free(sig);
}

static gboolean _poll(gpointer user_data)
{
  dt_lib_module_t *self = user_data;
  if(!self->widget || !gtk_widget_get_mapped(self->widget)) return G_SOURCE_CONTINUE;
  _fill_if_changed(self);
  return G_SOURCE_CONTINUE;
}

static gboolean _refresh_idle(gpointer user_data)
{
  dt_lib_module_t *self = user_data;
  dt_lib_mask_layers_t *d = self->data;
  d->refresh = 0;
  _fill_if_changed(self);
  return G_SOURCE_REMOVE;
}

static void _queue_refresh(gpointer instance, dt_lib_module_t *self)
{
  dt_lib_mask_layers_t *d = self->data;
  if(!d->refresh) d->refresh = g_timeout_add(150, _refresh_idle, self);
}

// another image, or modules gone: the rows may point at modules that are no
// more, even where the list would read the same, so it is always rebuilt
static void _queue_rebuild(gpointer instance, dt_lib_module_t *self)
{
  dt_lib_mask_layers_t *d = self->data;
  g_free(d->signature);
  d->signature = NULL;
  _queue_refresh(instance, self);
}

static void _queue_rebuild_module(gpointer instance, dt_iop_module_t *module, dt_lib_module_t *self)
{
  _queue_rebuild(instance, self);
}

void gui_init(dt_lib_module_t *self)
{
  dt_lib_mask_layers_t *d = g_malloc0(sizeof(dt_lib_mask_layers_t));
  self->data = d;

  d->list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_name(d->list, "mask-layers");

  d->empty = gtk_label_new(_("no masks on this photo yet"));
  dt_gui_add_class(d->empty, "dt_presets_empty");
  gtk_label_set_xalign(GTK_LABEL(d->empty), 0.0);
  gtk_widget_set_no_show_all(d->empty, TRUE);

  GtkWidget *add = gtk_button_new_with_label(_("+ new mask"));
  dt_gui_add_class(add, "dt_mask_layers_new");
  gtk_widget_set_tooltip_text(add, _("start a new masked adjustment"));
  g_signal_connect(add, "clicked", G_CALLBACK(_new_clicked), self);

  self->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), add, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), d->list, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), d->empty, FALSE, FALSE, 0);

  if(!dt_conf_key_exists("plugins/darkroom/mask_layers/expanded"))
    dt_conf_set_bool("plugins/darkroom/mask_layers/expanded", TRUE);

  DT_CONTROL_SIGNAL_HANDLE(DT_SIGNAL_DEVELOP_HISTORY_CHANGE, _queue_refresh);
  DT_CONTROL_SIGNAL_HANDLE(DT_SIGNAL_DEVELOP_IMAGE_CHANGED, _queue_rebuild);
  DT_CONTROL_SIGNAL_HANDLE(DT_SIGNAL_DEVELOP_INITIALIZE, _queue_rebuild);
  DT_CONTROL_SIGNAL_HANDLE(DT_SIGNAL_DEVELOP_MODULE_REMOVE, _queue_rebuild_module);
  d->poll = g_timeout_add(400, _poll, self);
}

void gui_cleanup(dt_lib_module_t *self)
{
  dt_lib_mask_layers_t *d = self->data;
  if(d && d->refresh) g_source_remove(d->refresh);
  if(d && d->poll) g_source_remove(d->poll);
  if(d) g_free(d->signature);
  g_free(self->data);
  self->data = NULL;
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode c; remove-trailing-spaces modified;
// clang-format on
