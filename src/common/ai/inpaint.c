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

#include "common/ai/inpaint.h"

#include "ai/backend.h"
#include "common/ai_models.h"
#include "common/darktable.h"

#include <math.h>
#include <string.h>

// the LaMa export takes "image" [1,3,512,512] in [0,1] and "mask"
// [1,1,512,512] (1 = hole) and returns "output" [1,3,512,512] in [0,1],
// already blended with the known pixels
#define INPAINT_SIZE 512

static GMutex _lock;
static dt_ai_environment_t *_env = NULL;
static dt_ai_context_t *_ctx = NULL;
static gboolean _load_failed = FALSE;

static char *_model_id(void)
{
  char *id = dt_ai_models_get_active_for_task(DT_INPAINT_TASK);
  if(id && id[0]) return id;
  g_free(id);
  return g_strdup(DT_INPAINT_DEFAULT_MODEL);
}

gboolean dt_inpaint_available(void)
{
  if(!dt_ai_registry_is_enabled()) return FALSE;
  char *id = _model_id();
  char *path = dt_ai_models_get_path(id);
  gboolean ok = FALSE;
  if(path)
  {
    gchar *onnx = g_build_filename(path, "model.onnx", NULL);
    ok = g_file_test(onnx, G_FILE_TEST_EXISTS);
    g_free(onnx);
  }
  // a model installed by hand, not through the registry
  if(!ok)
  {
    gchar *dir = g_build_filename(g_get_home_dir(), ".local", "share", "darktable",
                                  "models", id, "model.onnx", NULL);
    ok = g_file_test(dir, G_FILE_TEST_EXISTS);
    g_free(dir);
  }
  g_free(path);
  g_free(id);
  return ok;
}

// call with _lock held
static gboolean _ensure_loaded(void)
{
  if(_ctx) return TRUE;
  if(_load_failed || !dt_ai_registry_is_enabled()) return FALSE;

  if(!_env) _env = dt_ai_env_init(NULL);
  if(!_env)
  {
    _load_failed = TRUE;
    return FALSE;
  }
  char *id = _model_id();
  // the DFT layers do not map to every accelerator: let the user's choice
  // be tried, the backend falls back to the CPU when a provider refuses
  _ctx = dt_ai_load_model(_env, id, "model.onnx", DT_AI_PROVIDER_CONFIGURED);
  if(!_ctx)
    dt_print(DT_DEBUG_AI, "[inpaint] could not load model '%s'", id);
  g_free(id);
  if(!_ctx) _load_failed = TRUE;
  return _ctx != NULL;
}

// bilinear sample of channel c from an interleaved image
static inline float _sample(const float *img, const int stride, const int c,
                            const int w, const int h, float x, float y)
{
  x = CLAMP(x, 0.0f, (float)(w - 1));
  y = CLAMP(y, 0.0f, (float)(h - 1));
  const int x0 = (int)x, y0 = (int)y;
  const int x1 = MIN(x0 + 1, w - 1), y1 = MIN(y0 + 1, h - 1);
  const float fx = x - x0, fy = y - y0;
  const float a = img[((size_t)y0 * w + x0) * stride + c];
  const float b = img[((size_t)y0 * w + x1) * stride + c];
  const float cc = img[((size_t)y1 * w + x0) * stride + c];
  const float d = img[((size_t)y1 * w + x1) * stride + c];
  return (a * (1.0f - fx) + b * fx) * (1.0f - fy) + (cc * (1.0f - fx) + d * fx) * fy;
}

int dt_inpaint_run(const float *rgb,
                   const int stride,
                   const float *hole,
                   const int width,
                   const int height,
                   float *out)
{
  if(!rgb || !hole || !out || width < 2 || height < 2 || stride < 3) return -1;

  const size_t plane = (size_t)INPAINT_SIZE * INPAINT_SIZE;
  float *in_img = g_try_malloc(sizeof(float) * 3 * plane);
  float *in_mask = g_try_malloc(sizeof(float) * plane);
  float *res = g_try_malloc(sizeof(float) * 3 * plane);
  if(!in_img || !in_mask || !res)
  {
    g_free(in_img);
    g_free(in_mask);
    g_free(res);
    return -1;
  }

  // resample to the model's size, pixel centers aligned
  const float sx = (float)width / INPAINT_SIZE, sy = (float)height / INPAINT_SIZE;
  for(int y = 0; y < INPAINT_SIZE; y++)
    for(int x = 0; x < INPAINT_SIZE; x++)
    {
      const float fx = (x + 0.5f) * sx - 0.5f, fy = (y + 0.5f) * sy - 0.5f;
      const size_t k = (size_t)y * INPAINT_SIZE + x;
      for(int c = 0; c < 3; c++)
        in_img[c * plane + k] = CLAMP(_sample(rgb, stride, c, width, height, fx, fy), 0.0f, 1.0f);
      in_mask[k] = _sample(hole, 1, 0, width, height, fx, fy) > 0.1f ? 1.0f : 0.0f;
    }

  // grow the hole a little: the model blends at the hole's edge, and an
  // object's rim left outside it would come back as a ghost
  {
    float *grown = g_try_malloc(sizeof(float) * plane);
    if(grown)
    {
      const int r = 3;
      for(int y = 0; y < INPAINT_SIZE; y++)
        for(int x = 0; x < INPAINT_SIZE; x++)
        {
          float m = 0.0f;
          for(int dy = -r; dy <= r && m == 0.0f; dy++)
            for(int dx = -r; dx <= r; dx++)
            {
              const int yy = y + dy, xx = x + dx;
              if(yy < 0 || yy >= INPAINT_SIZE || xx < 0 || xx >= INPAINT_SIZE) continue;
              if(in_mask[(size_t)yy * INPAINT_SIZE + xx] > 0.5f)
              {
                m = 1.0f;
                break;
              }
            }
          grown[(size_t)y * INPAINT_SIZE + x] = m;
        }
      memcpy(in_mask, grown, sizeof(float) * plane);
      g_free(grown);
    }
  }

  int64_t shape_img[4] = { 1, 3, INPAINT_SIZE, INPAINT_SIZE };
  int64_t shape_mask[4] = { 1, 1, INPAINT_SIZE, INPAINT_SIZE };
  int64_t shape_out[4] = { 1, 3, INPAINT_SIZE, INPAINT_SIZE };
  dt_ai_tensor_t inputs[2] = {
    { .data = in_img, .type = DT_AI_FLOAT, .shape = shape_img, .ndim = 4 },
    { .data = in_mask, .type = DT_AI_FLOAT, .shape = shape_mask, .ndim = 4 },
  };
  dt_ai_tensor_t outputs[1] = {
    { .data = res, .type = DT_AI_FLOAT, .shape = shape_out, .ndim = 4 },
  };

  int err = -1;
  g_mutex_lock(&_lock);
  if(_ensure_loaded())
    err = dt_ai_run(_ctx, inputs, 2, outputs, 1);
  g_mutex_unlock(&_lock);

  if(err == 0)
  {
    // back to the caller's size; the known pixels are left to the caller
    const float rx = (float)INPAINT_SIZE / width, ry = (float)INPAINT_SIZE / height;
    for(int y = 0; y < height; y++)
      for(int x = 0; x < width; x++)
      {
        const size_t k = (size_t)y * width + x;
        if(hole[k] <= 0.0f) continue;
        const float fx = (x + 0.5f) * rx - 0.5f, fy = (y + 0.5f) * ry - 0.5f;
        for(int c = 0; c < 3; c++)
          out[k * stride + c] = CLAMP(_sample(res + c * plane, 1, 0, INPAINT_SIZE,
                                              INPAINT_SIZE, fx, fy), 0.0f, 1.0f);
      }
  }
  else
    dt_print(DT_DEBUG_AI, "[inpaint] inference failed (%d)", err);

  g_free(in_img);
  g_free(in_mask);
  g_free(res);
  return err == 0 ? 0 : -1;
}

void dt_inpaint_cleanup(void)
{
  g_mutex_lock(&_lock);
  if(_ctx) dt_ai_unload_model(_ctx);
  _ctx = NULL;
  if(_env) dt_ai_env_destroy(_env);
  _env = NULL;
  _load_failed = FALSE;
  g_mutex_unlock(&_lock);
}
