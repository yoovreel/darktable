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

// tool strip: five big, labelled entry points at the top of the darkroom's
// right panel (edit, crop, remove, masks, effects), the way Lightroom and
// most modern editors start an edit. Each one is a shortcut to what is
// already there: a module group, a module, a tool of a module or a new
// masked adjustment. Nothing is hidden: the module groups and the full
// module list stay right below.

#include "common/darktable.h"
#include "control/conf.h"
#include "control/control.h"
#include "develop/blend.h"
#include "develop/develop.h"
#include "develop/imageop.h"
#include "dtgtk/icon.h"
#include "dtgtk/paint.h"
#include "gui/accelerators.h"
#include "gui/gtk.h"
#include "libs/lib.h"
#include "libs/lib_api.h"
#include "libs/modulegroups.h"

#include <math.h>

DT_MODULE(1)

typedef enum dt_tool_strip_tool_t
{
  TOOL_EDIT = 0,
  TOOL_CROP,
  TOOL_REMOVE,
  TOOL_MASKS,
  TOOL_EFFECTS,
  TOOL_N
} dt_tool_strip_tool_t;

typedef struct dt_lib_tool_strip_t
{
  GtkWidget *buttons[TOOL_N];
  guint sync_source;
  int active;
} dt_lib_tool_strip_t;

// the modules that open the effects group: the first one in the user's
// pipe that belongs to a group decides which group that is
static const char *_effects_ops[] = { "vignette", "grain", "bloom", "soften", "lowlight", NULL };

const char *name(dt_lib_module_t *self)
{
  return _("tool strip");
}

const char *description(dt_lib_module_t *self)
{
  return _("quick entry points for the common tasks:\n"
           "edit, crop, remove, masks and effects");
}

dt_view_type_flags_t views(dt_lib_module_t *self)
{
  return DT_VIEW_DARKROOM;
}

uint32_t container(dt_lib_module_t *self)
{
  return DT_UI_CONTAINER_PANEL_RIGHT_TOP;
}

gboolean expandable(dt_lib_module_t *self)
{
  return FALSE;
}

int position(const dt_lib_module_t *self)
{
  // above the histogram (1000), as in other editors' develop panel
  return 1001;
}

/* ---- icons ---------------------------------------------------------------- */

#define ICON_BEGIN                                                                  \
  cairo_save(cr);                                                                   \
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);                                     \
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);                                   \
  const double s = MIN(w, h);                                                       \
  cairo_translate(cr, x + (w - s) / 2.0, y + (h - s) / 2.0);                        \
  cairo_scale(cr, s, s);                                                            \
  cairo_set_line_width(cr, 0.085);

#define ICON_END cairo_restore(cr);

// three sliders
static void _paint_edit(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  ICON_BEGIN
  const double ys[3] = { 0.22, 0.5, 0.78 }, ks[3] = { 0.65, 0.3, 0.55 };
  for(int i = 0; i < 3; i++)
  {
    cairo_move_to(cr, 0.08, ys[i]);
    cairo_line_to(cr, 0.92, ys[i]);
    cairo_stroke(cr);
    cairo_arc(cr, ks[i], ys[i], 0.1, 0, 2 * M_PI);
    cairo_fill(cr);
  }
  ICON_END
}

// two crop corners
static void _paint_crop(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  ICON_BEGIN
  cairo_move_to(cr, 0.26, 0.04);
  cairo_line_to(cr, 0.26, 0.74);
  cairo_line_to(cr, 0.96, 0.74);
  cairo_move_to(cr, 0.04, 0.26);
  cairo_line_to(cr, 0.74, 0.26);
  cairo_line_to(cr, 0.74, 0.96);
  cairo_stroke(cr);
  ICON_END
}

// an eraser
static void _paint_remove(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  ICON_BEGIN
  cairo_move_to(cr, 0.58, 0.1);
  cairo_line_to(cr, 0.92, 0.44);
  cairo_line_to(cr, 0.48, 0.88);
  cairo_line_to(cr, 0.3, 0.88);
  cairo_line_to(cr, 0.12, 0.7);
  cairo_close_path(cr);
  cairo_move_to(cr, 0.36, 0.32);
  cairo_line_to(cr, 0.7, 0.66);
  cairo_move_to(cr, 0.3, 0.88);
  cairo_line_to(cr, 0.92, 0.88);
  cairo_stroke(cr);
  ICON_END
}

// a circle, half filled
static void _paint_masks(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  ICON_BEGIN
  cairo_arc(cr, 0.5, 0.5, 0.4, 0, 2 * M_PI);
  cairo_stroke(cr);
  cairo_arc(cr, 0.5, 0.5, 0.4, M_PI / 2, 3 * M_PI / 2);
  cairo_close_path(cr);
  cairo_fill(cr);
  ICON_END
}

// a sparkle
static void _paint_effects(cairo_t *cr, gint x, gint y, gint w, gint h, gint flags, void *data)
{
  ICON_BEGIN
  const double cx = 0.42, cy = 0.56, r = 0.36, k = 0.09;
  cairo_move_to(cr, cx, cy - r);
  cairo_curve_to(cr, cx + k, cy - k, cx + k, cy - k, cx + r, cy);
  cairo_curve_to(cr, cx + k, cy + k, cx + k, cy + k, cx, cy + r);
  cairo_curve_to(cr, cx - k, cy + k, cx - k, cy + k, cx - r, cy);
  cairo_curve_to(cr, cx - k, cy - k, cx - k, cy - k, cx, cy - r);
  cairo_close_path(cr);
  cairo_fill(cr);
  cairo_arc(cr, 0.82, 0.18, 0.08, 0, 2 * M_PI);
  cairo_fill(cr);
  ICON_END
}

/* ---- actions -------------------------------------------------------------- */

static dt_iop_module_t *_module(const char *op)
{
  // the instance the user is looking at when there are several
  dt_iop_module_t *focused = dt_dev_gui_module();
  if(focused && !strcmp(focused->op, op)) return focused;
  return darktable.develop ? dt_iop_get_module_from_list(darktable.develop->iop, op) : NULL;
}

static gboolean _open_module(const char *op)
{
  dt_iop_module_t *m = _module(op);
  if(!m)
  {
    dt_control_log(_("%s is not available for this image"), dt_iop_get_localized_name(op));
    return FALSE;
  }
  dt_dev_modulegroups_switch(darktable.develop, m);
  dt_iop_request_focus(m);
  dt_iop_gui_set_expanded(m, TRUE, TRUE);
  dt_iop_gui_scroll_into_view_later(m->expander);
  return TRUE;
}

static gboolean _remove_tool_on(gpointer data)
{
  const float r = dt_action_process("iop/retouch/tools/activate AI remove tool", 0, NULL, "on", 1.0f);
  if(r < 0.5f) dt_print(DT_DEBUG_ALWAYS, "[tool strip] could not select the AI remove tool");
  return G_SOURCE_REMOVE;
}

static gboolean _masks_panel_open(gpointer data)
{
  dt_iop_gui_blend_masks_panel_open();
  return G_SOURCE_REMOVE;
}

static void _tool_clicked(GtkButton *button, dt_lib_module_t *self)
{
  dt_develop_t *dev = darktable.develop;
  if(!dev) return;
  const int tool = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "tool"));

  switch(tool)
  {
    case TOOL_EDIT:
      dt_iop_request_focus(NULL);
      dt_dev_modulegroups_set(dev, DT_MODULEGROUP_BASICS);
      break;

    case TOOL_CROP:
      _open_module("crop");
      break;

    case TOOL_REMOVE:
      if(_open_module("retouch"))
      {
        // after the module has focused and laid itself out: focusing it
        // restores its last tool
        g_timeout_add(120, _remove_tool_on, NULL);
        dt_control_log(_("paint over what should go: a person, a sign, a stain"));
      }
      break;

    case TOOL_MASKS:
    {
      // a module that already has a mask: show it; otherwise start a new
      // masked adjustment
      dt_iop_module_t *m = dt_dev_gui_module();
      if(m && m->blend_params
         && (m->blend_params->mask_mode & (DEVELOP_MASK_MASK | DEVELOP_MASK_FLEXI)))
        g_timeout_add(120, _masks_panel_open, NULL);
      else if(!dt_iop_gui_blend_new_mask_adjustment())
        dt_control_log(_("could not start a new mask"));
      else
      {
        g_timeout_add(120, _masks_panel_open, NULL);
        dt_control_log(_("new mask: draw a shape or pick an AI object, then adjust"));
      }
      break;
    }

    case TOOL_EFFECTS:
    {
      for(const char **op = _effects_ops; *op; op++)
      {
        dt_iop_module_t *m = _module(*op);
        if(m)
        {
          dt_iop_request_focus(NULL);
          dt_dev_modulegroups_switch(dev, m);
          return;
        }
      }
      _open_module("vignette");
      break;
    }
  }
}

/* ---- state ---------------------------------------------------------------- */

static int _current_tool(void)
{
  dt_develop_t *dev = darktable.develop;
  if(!dev) return -1;
  const dt_iop_module_t *m = dt_dev_gui_module();
  if(m)
  {
    if(!strcmp(m->op, "crop")) return TOOL_CROP;
    if(!strcmp(m->op, "retouch")) return TOOL_REMOVE;
    // the edit panel shows the basic sliders, whatever stays focused behind it
    if(dt_dev_modulegroups_get(dev) == DT_MODULEGROUP_BASICS) return TOOL_EDIT;
    if(m->blend_params
       && (m->blend_params->mask_mode & (DEVELOP_MASK_MASK | DEVELOP_MASK_FLEXI)))
      return TOOL_MASKS;
    for(const char **op = _effects_ops; *op; op++)
      if(!strcmp(m->op, *op)) return TOOL_EFFECTS;
  }
  if(dt_dev_modulegroups_get(dev) == DT_MODULEGROUP_BASICS) return TOOL_EDIT;
  return -1;
}

static gboolean _sync(gpointer user_data)
{
  dt_lib_module_t *self = user_data;
  dt_lib_tool_strip_t *d = self->data;
  if(!gtk_widget_get_mapped(self->widget)) return G_SOURCE_CONTINUE;
  const int now = _current_tool();
  if(now == d->active) return G_SOURCE_CONTINUE;
  d->active = now;
  for(int i = 0; i < TOOL_N; i++)
  {
    if(i == now)
      dt_gui_add_class(d->buttons[i], "dt_tool_active");
    else
      dt_gui_remove_class(d->buttons[i], "dt_tool_active");
  }
  return G_SOURCE_CONTINUE;
}

/* ---- gui ------------------------------------------------------------------ */

void gui_init(dt_lib_module_t *self)
{
  dt_lib_tool_strip_t *d = g_malloc0(sizeof(dt_lib_tool_strip_t));
  self->data = d;
  d->active = -2;

  struct
  {
    const char *label, *tip;
    DTGTKCairoPaintIconFunc paint;
  } tools[TOOL_N] = {
    { N_("edit"), N_("the basic adjustments: light, color, presence"), _paint_edit },
    { N_("crop"), N_("crop and straighten"), _paint_crop },
    { N_("remove"), N_("remove people, signs or spots with AI\n"
                       "opens the retouch module's AI remove tool"), _paint_remove },
    { N_("masks"), N_("adjust only part of the image\n"
                      "starts a new masked adjustment, or shows the mask of the focused one"),
      _paint_masks },
    { N_("effects"), N_("vignette, grain and other finishing touches"), _paint_effects },
  };

  self->widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_name(self->widget, "tool-strip");
  gtk_box_set_homogeneous(GTK_BOX(self->widget), TRUE);

  for(int i = 0; i < TOOL_N; i++)
  {
    GtkWidget *btn = gtk_button_new();
    dt_gui_add_class(btn, "dt_tool_btn");
    gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *icon = dtgtk_icon_new(tools[i].paint, 0, NULL);
    gtk_widget_set_size_request(icon, DT_PIXEL_APPLY_DPI(20), DT_PIXEL_APPLY_DPI(20));
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    GtkWidget *label = gtk_label_new(_(tools[i].label));
    gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(btn), box);
    gtk_widget_set_tooltip_text(btn, _(tools[i].tip));
    g_object_set_data(G_OBJECT(btn), "tool", GINT_TO_POINTER(i));
    g_signal_connect(btn, "clicked", G_CALLBACK(_tool_clicked), self);
    gtk_box_pack_start(GTK_BOX(self->widget), btn, TRUE, TRUE, 0);
    d->buttons[i] = btn;

    gchar *action = g_strdup_printf("%s tool", tools[i].label);
    dt_action_define(DT_ACTION(self), NULL, action, btn, &dt_action_def_button);
    g_free(action);
  }

  gtk_widget_show_all(self->widget);
  d->sync_source = g_timeout_add(250, _sync, self);
}

void gui_cleanup(dt_lib_module_t *self)
{
  dt_lib_tool_strip_t *d = self->data;
  if(d && d->sync_source) g_source_remove(d->sync_source);
  g_free(self->data);
  self->data = NULL;
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode c; remove-trailing-spaces modified;
// clang-format on
