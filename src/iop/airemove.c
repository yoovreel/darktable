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

// AI remove: retouch's AI remove tool as a module of its own, with a brush
// and nothing else. It is retouch built a second time with
// DT_RETOUCH_AI_ONLY, so the two share their parameters, processing and
// mask handling, and a fix to one is a fix to both

#define DT_RETOUCH_AI_ONLY
#include "iop/retouch.c"
