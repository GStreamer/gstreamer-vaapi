/*
 *  gstvaapidisplay_x11_priv.h - Internal VA/X11 interface
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2012 Intel Corporation
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#ifndef GST_VAAPI_DISPLAY_X11_PRIV_H
#define GST_VAAPI_DISPLAY_X11_PRIV_H

#include <gst/vaapi/gstvaapiutils_x11.h>
#include <gst/vaapi/gstvaapidisplay_x11.h>
#include "gstvaapidisplay_priv.h"

G_BEGIN_DECLS

#define GST_VAAPI_DISPLAY_X11_CAST(display) \
    ((GstVaapiDisplayX11 *)(display))

#define GST_VAAPI_DISPLAY_X11_PRIVATE(display) \
    (&GST_VAAPI_DISPLAY_X11_CAST(display)->priv)

typedef struct _GstVaapiDisplayX11Private       GstVaapiDisplayX11Private;
typedef struct _GstVaapiDisplayX11Class         GstVaapiDisplayX11Class;

/**
 * GST_VAAPI_DISPLAY_XDISPLAY:
 * @display: a #GstVaapiDisplay
 *
 * Macro that evaluates to the underlying X11 #Display of @display
 */
#undef  GST_VAAPI_DISPLAY_XDISPLAY
#define GST_VAAPI_DISPLAY_XDISPLAY(display) \
    GST_VAAPI_DISPLAY_X11_PRIVATE(display)->x11_display

/**
 * GST_VAAPI_DISPLAY_XSCREEN:
 * @display: a #GstVaapiDisplay
 *
 * Macro that evaluates to the underlying X11 screen of @display
 */
#undef  GST_VAAPI_DISPLAY_XSCREEN
#define GST_VAAPI_DISPLAY_XSCREEN(display) \
    GST_VAAPI_DISPLAY_X11_PRIVATE(display)->x11_screen

struct _GstVaapiDisplayX11Private {
    gchar              *display_name;
    Display            *x11_display;
    int                 x11_screen;
    guint               use_foreign_display     : 1; // Foreign native_display?
    guint               use_xrandr              : 1;
    guint               synchronous             : 1;
};

/**
 * GstVaapiDisplayX11:
 *
 * VA/X11 display wrapper.
 */
struct _GstVaapiDisplayX11 {
    /*< private >*/
    GstVaapiDisplay parent_instance;

    GstVaapiDisplayX11Private priv;
};

/**
 * GstVaapiDisplayX11Class:
 *
 * VA/X11 display wrapper clas.
 */
struct _GstVaapiDisplayX11Class {
    /*< private >*/
    GstVaapiDisplayClass parent_class;
};

void
gst_vaapi_display_x11_class_init(GstVaapiDisplayX11Class *klass);

G_END_DECLS

#endif /* GST_VAAPI_DISPLAY_X11_PRIV_H */
