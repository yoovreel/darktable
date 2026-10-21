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

// inpaint: AI object removal. Fills the masked part of an image with
// plausible content from its surroundings, using an inpainting model
// (LaMa) through the ONNX backend. The model works at a fixed 512x512;
// any input size is resampled to it and back, so callers pass a crop
// around the area to remove with enough context, not the whole image.

#pragma once

#include <glib.h>

#define DT_INPAINT_TASK "inpaint"
#define DT_INPAINT_DEFAULT_MODEL "inpaint-lama"

/* TRUE when AI is enabled in preferences and an inpainting model is
   installed. Cheap; does not load the model. */
gboolean dt_inpaint_available(void);

/* fill the hole in `rgb`. `rgb` is interleaved RGB, `stride` floats per
   pixel (3 or 4, a 4th channel is ignored and left as is), values
   display-encoded in [0, 1]. `hole` is one float per pixel, > 0.5 where
   content is to be made up. `out` receives the result in the same layout
   as `rgb` (it may be `rgb` itself); only the hole is meaningful, the
   caller blends. Thread-safe: one inference at a time. Returns 0 on
   success, < 0 when no model is available or inference failed. */
int dt_inpaint_run(const float *rgb,
                   const int stride,
                   const float *hole,
                   const int width,
                   const int height,
                   float *out);

/* release the model; it is loaded again on the next run */
void dt_inpaint_cleanup(void);
