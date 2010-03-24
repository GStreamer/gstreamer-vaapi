/*
 *  test-windows.c - Test GstVaapiWindow
 *
 *  gstreamer-vaapi (C) 2010 Splitted-Desktop Systems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <gst/vaapi/gstvaapidisplay_x11.h>
#include <gst/vaapi/gstvaapiwindow_x11.h>
#include <gst/vaapi/gstvaapisurface.h>
#include <gst/vaapi/gstvaapiimage.h>

static inline void pause(void)
{
    g_print("Press any key to continue...\n");
    getchar();
}

typedef void (*DrawRectFunc)(
    guchar *pixels[3],
    guint   stride[3],
    gint    x,
    gint    y,
    guint   width,
    guint   height,
    guint32 color
);

static void draw_rect_ARGB(
    guchar *pixels[3],
    guint   stride[3],
    gint    x,
    gint    y,
    guint   width,
    guint   height,
    guint32 color
)
{
    guint i, j;

    color = GUINT32_TO_BE(color);

    for (j = 0; j < height; j++) {
        guint32 *p = (guint32 *)(pixels[0] + (y + j) * stride[0] + x * 4);
        for (i = 0; i < width; i++)
            p[i] = color;
    }
}

static void draw_rect_BGRA(
    guchar *pixels[3],
    guint   stride[3],
    gint    x,
    gint    y,
    guint   width,
    guint   height,
    guint32 color
)
{
    // Converts ARGB color to BGRA
    color = GUINT32_SWAP_LE_BE(color);

    draw_rect_ARGB(pixels, stride, x, y, width, height, color);
}

static void draw_rect_RGBA(
    guchar *pixels[3],
    guint   stride[3],
    gint    x,
    gint    y,
    guint   width,
    guint   height,
    guint32 color
)
{
    // Converts ARGB color to RGBA
    color = ((color >> 24) & 0xff) | ((color & 0xffffff) << 8);

    draw_rect_ARGB(pixels, stride, x, y, width, height, color);
}

static void draw_rect_ABGR(
    guchar *pixels[3],
    guint   stride[3],
    gint    x,
    gint    y,
    guint   width,
    guint   height,
    guint32 color
)
{
    // Converts ARGB color to ABGR
    color = ((color & 0xff00ff00)   |
             ((color >> 16) & 0xff) |
             ((color & 0xff) << 16));

    draw_rect_ARGB(pixels, stride, x, y, width, height, color);
}

static void draw_rect_NV12( // Y, UV planes
    guchar *pixels[3],
    guint   stride[3],
    gint    x,
    gint    y,
    guint   width,
    guint   height,
    guint32 color
)
{
    const guchar Y  = color >> 16;
    const guchar Cb = color >> 8;
    const guchar Cr = color;
    guchar *dst;
    guint i, j;

    dst = pixels[0] + y * stride[0] + x;
    for (j = 0; j < height; j++, dst += stride[0])
        for (i = 0; i < width; i++)
            dst[i] = Y;

    x      /= 2;
    y      /= 2;
    width  /= 2;
    height /= 2;

    dst = pixels[1] + y * stride[1] + x * 2;
    for (j = 0; j < height; j++, dst += stride[1])
        for (i = 0; i < width; i++) {
            dst[2*i + 0] = Cb;
            dst[2*i + 1] = Cr;
        }
}

static void draw_rect_YV12( // Y, U, V planes
    guchar *pixels[3],
    guint   stride[3],
    gint    x,
    gint    y,
    guint   width,
    guint   height,
    guint32 color
)
{
    const guchar Y  = color >> 16;
    const guchar Cb = color >> 8;
    const guchar Cr = color;
    guchar *pY, *pU, *pV;
    guint i, j;

    pY = pixels[0] + y * stride[0] + x;
    for (j = 0; j < height; j++, pY += stride[0])
        for (i = 0; i < width; i++)
            pY[i] = Y;

    x      /= 2;
    y      /= 2;
    width  /= 2;
    height /= 2;

    pU = pixels[1] + y * stride[1] + x;
    pV = pixels[2] + y * stride[2] + x;
    for (j = 0; j < height; j++, pU += stride[1], pV += stride[2])
        for (i = 0; i < width; i++) {
            pU[i] = Cb;
            pV[i] = Cr;
        }
}

static gboolean draw_rgb_rects(GstVaapiImage *image)
{
    GstVaapiImageFormat format = GST_VAAPI_IMAGE_FORMAT(image);
    guint               w      = GST_VAAPI_IMAGE_WIDTH(image);
    guint               h      = GST_VAAPI_IMAGE_HEIGHT(image);
    guchar             *pixels[3];
    guint               stride[3];
    guint32             red_color, green_color, blue_color, black_color;
    DrawRectFunc        draw_rect;

    if (!gst_vaapi_image_map(image))
        return FALSE;

    switch (format) {
    case GST_VAAPI_IMAGE_ARGB:
        draw_rect   = draw_rect_ARGB;
        goto RGB_colors;
    case GST_VAAPI_IMAGE_BGRA:
        draw_rect   = draw_rect_BGRA;
        goto RGB_colors;
    case GST_VAAPI_IMAGE_RGBA:
        draw_rect   = draw_rect_RGBA;
        goto RGB_colors;
    case GST_VAAPI_IMAGE_ABGR:
        draw_rect   = draw_rect_ABGR;
    RGB_colors:
        pixels[0]   = gst_vaapi_image_get_plane(image, 0);
        stride[0]   = gst_vaapi_image_get_pitch(image, 0);
        red_color   = 0xffff0000;
        green_color = 0xff00ff00;
        blue_color  = 0xff0000ff;
        black_color = 0xff000000;
        break;
    case GST_VAAPI_IMAGE_NV12:
        draw_rect   = draw_rect_NV12;
        pixels[0]   = gst_vaapi_image_get_plane(image, 0);
        stride[0]   = gst_vaapi_image_get_pitch(image, 0);
        pixels[1]   = gst_vaapi_image_get_plane(image, 1);
        stride[1]   = gst_vaapi_image_get_pitch(image, 1);
        goto YUV_colors;
    case GST_VAAPI_IMAGE_YV12:
        draw_rect   = draw_rect_YV12;
        pixels[0]   = gst_vaapi_image_get_plane(image, 0);
        stride[0]   = gst_vaapi_image_get_pitch(image, 0);
        pixels[1]   = gst_vaapi_image_get_plane(image, 2);
        stride[1]   = gst_vaapi_image_get_pitch(image, 2);
        pixels[2]   = gst_vaapi_image_get_plane(image, 1);
        stride[2]   = gst_vaapi_image_get_pitch(image, 1);
        goto YUV_colors;
    case GST_VAAPI_IMAGE_I420:
        draw_rect   = draw_rect_YV12;
        pixels[0]   = gst_vaapi_image_get_plane(image, 0);
        stride[0]   = gst_vaapi_image_get_pitch(image, 0);
        pixels[1]   = gst_vaapi_image_get_plane(image, 1);
        stride[1]   = gst_vaapi_image_get_pitch(image, 1);
        pixels[2]   = gst_vaapi_image_get_plane(image, 2);
        stride[2]   = gst_vaapi_image_get_pitch(image, 2);
    YUV_colors:
        red_color   = 0x515af0;
        green_color = 0x913622;
        blue_color  = 0x29f06e;
        black_color = 0x108080;
        break;
    default:
        gst_vaapi_image_unmap(image);
        return FALSE;
    }

    draw_rect(pixels, stride, 0,   0,   w/2, h/2, red_color);
    draw_rect(pixels, stride, w/2, 0,   w/2, h/2, green_color);
    draw_rect(pixels, stride, 0,   h/2, w/2, h/2, blue_color);
    draw_rect(pixels, stride, w/2, h/2, w/2, h/2, black_color);

    if (!gst_vaapi_image_unmap(image))
        return FALSE;

    return TRUE;
}

static gboolean
upload_image(GstVaapiSurface *surface, GstVaapiImage *image)
{
    GstVaapiDisplay *display;
    GstVaapiImageFormat format;
    GstVaapiSubpicture *subpicture;

    display = gst_vaapi_object_get_display(GST_VAAPI_OBJECT(surface));
    if (!display)
        return FALSE;

    format = gst_vaapi_image_get_format(image);
    if (!format)
        return FALSE;

    if (gst_vaapi_surface_put_image(surface, image))
        return TRUE;

    g_print("could not upload %" GST_FOURCC_FORMAT" image to surface\n",
            GST_FOURCC_ARGS(format));

    if (!gst_vaapi_display_has_subpicture_format(display, format))
        return FALSE;

    g_print("trying as a subpicture\n");

    subpicture = gst_vaapi_subpicture_new(image);
    if (!subpicture)
        g_error("could not create Gst/VA subpicture");

    if (!gst_vaapi_surface_associate_subpicture(surface, subpicture,
                                                NULL, NULL))
        g_error("could not associate subpicture to surface");

    /* The surface holds a reference to the subpicture. This is safe */
    g_object_unref(subpicture);
    return TRUE;
}

int
main(int argc, char *argv[])
{
    GstVaapiDisplay    *display;
    GstVaapiWindow     *window;
    GstVaapiSurface    *surface;
    GstVaapiImage      *image   = NULL;
    guint flags = GST_VAAPI_PICTURE_STRUCTURE_FRAME;
    guint i;

    static const GstVaapiImageFormat image_formats[] = {
        GST_VAAPI_IMAGE_NV12,
        GST_VAAPI_IMAGE_YV12,
        GST_VAAPI_IMAGE_I420,
        GST_VAAPI_IMAGE_ARGB,
        GST_VAAPI_IMAGE_BGRA,
        GST_VAAPI_IMAGE_RGBA,
        GST_VAAPI_IMAGE_ABGR,
        0
    };

    static const GstVaapiChromaType chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420;
    static const guint              width       = 320;
    static const guint              height      = 240;
    static const guint              win_width   = 640;
    static const guint              win_height  = 480;

    gst_init(&argc, &argv);

    display = gst_vaapi_display_x11_new(NULL);
    if (!display)
        g_error("could not create Gst/VA display");

    surface = gst_vaapi_surface_new(display, chroma_type, width, height);
    if (!surface)
        g_error("could not create Gst/VA surface");

    for (i = 0; image_formats[i]; i++) {
        const GstVaapiImageFormat format = image_formats[i];

        image = gst_vaapi_image_new(display, format, width, height);
        if (!image)
            continue;

        if (!draw_rgb_rects(image))
            g_error("could not draw RGB rectangles");

        if (upload_image(surface, image))
            break;
    }
    if (!image)
        g_error("could not create Gst/VA image");

    if (!gst_vaapi_surface_sync(surface))
        g_error("could not complete image upload");

    g_print("#\n");
    g_print("# Create window with gst_vaapi_window_x11_new()\n");
    g_print("#\n");
    {
        window = gst_vaapi_window_x11_new(display, win_width, win_height);
        if (!window)
            g_error("could not create window");

        gst_vaapi_window_show(window);

        if (!gst_vaapi_window_put_surface(window, surface, NULL, NULL, flags))
            g_error("could not render surface");

        pause();
        g_object_unref(window);
    }

    g_print("#\n");
    g_print("# Create window with gst_vaapi_window_x11_new_with_xid()\n");
    g_print("#\n");
    {
        Display * const dpy = GST_VAAPI_DISPLAY_XDISPLAY(display);
        Window rootwin, win;
        int screen;
        unsigned long white_pixel, black_pixel;

        screen      = DefaultScreen(dpy);
        rootwin     = RootWindow(dpy, screen);
        white_pixel = WhitePixel(dpy, screen);
        black_pixel = BlackPixel(dpy, screen);

        win = XCreateSimpleWindow(
            dpy,
            rootwin,
            0, 0, win_width, win_height,
            0, black_pixel,
            white_pixel
        );
        if (!win)
            g_error("could not create X window");

        window = gst_vaapi_window_x11_new_with_xid(display, win);
        if (!window)
            g_error("could not create window");

        gst_vaapi_window_show(window);

        if (!gst_vaapi_window_put_surface(window, surface, NULL, NULL, flags))
            g_error("could not render surface");

        pause();
        g_object_unref(window);
        XUnmapWindow(dpy, win);
        XDestroyWindow(dpy, win);
    }

    g_object_unref(image);
    g_object_unref(surface);
    g_object_unref(display);
    gst_deinit();
    return 0;
}
