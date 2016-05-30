/*
 *  gstvaapiencoder_vp9.c - VP9 encoder
 *
 *  Copyright (C) 2016 Intel Corporation
 *    Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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

#include "sysdeps.h"
#include <va/va.h>
#include <va/va_enc_vp9.h>
#include <gst/base/gstbitwriter.h>
#include <gst/codecparsers/gstvp9parser.h>
#include "gstvaapicompat.h"
#include "gstvaapiencoder_priv.h"
#include "gstvaapiencoder_vp9.h"
#include "gstvaapicodedbufferproxy_priv.h"
#include "gstvaapisurface.h"

#define DEBUG 1
#include "gstvaapidebug.h"

/* Define default rate control mode ("constant-qp") */
#define DEFAULT_RATECONTROL GST_VAAPI_RATECONTROL_CQP

/* Supported set of VA rate controls, within this implementation */
#define SUPPORTED_RATECONTROLS                  \
  (GST_VAAPI_RATECONTROL_MASK (CQP))

/* Supported set of tuning options, within this implementation */
#define SUPPORTED_TUNE_OPTIONS \
  (GST_VAAPI_ENCODER_TUNE_MASK (NONE))

/* Supported set of VA packed headers, within this implementation */
#define SUPPORTED_PACKED_HEADERS                \
  (VA_ENC_PACKED_HEADER_NONE)

#define DEFAULT_LOOP_FILTER_LEVEL 0
#define DEFAULT_SHARPNESS_LEVEL 0
#define DEFAULT_YAC_QI 40

/* ------------------------------------------------------------------------- */
/* --- VP9 Encoder                                                      --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_ENCODER_VP9_CAST(encoder) \
    ((GstVaapiEncoderVP9 *)(encoder))

struct _GstVaapiEncoderVP9
{
  GstVaapiEncoder parent_instance;
  GstVaapiProfile profile;
  guint frame_num;
  /* reference list */
  GstVaapiSurfaceProxy *ref_list[GST_VP9_REF_FRAMES];
};

/* Derives the profile that suits best to the configuration */
static GstVaapiEncoderStatus
ensure_profile (GstVaapiEncoderVP9 * encoder)
{
  /* Always start from "simple" profile for maximum compatibility */
  encoder->profile = GST_VAAPI_PROFILE_VP9_0;

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

/* Derives the profile supported by the underlying hardware */
static gboolean
ensure_hw_profile (GstVaapiEncoderVP9 * encoder)
{
  GstVaapiDisplay *const display = GST_VAAPI_ENCODER_DISPLAY (encoder);
  GstVaapiEntrypoint entrypoint = GST_VAAPI_ENTRYPOINT_SLICE_ENCODE;
  GstVaapiProfile profile, profiles[2];
  guint i, num_profiles = 0;

  profiles[num_profiles++] = encoder->profile;

  profile = GST_VAAPI_PROFILE_UNKNOWN;
  for (i = 0; i < num_profiles; i++) {
    if (gst_vaapi_display_has_encoder (display, profiles[i], entrypoint)) {
      profile = profiles[i];
      break;
    }
  }
  if (profile == GST_VAAPI_PROFILE_UNKNOWN)
    goto error_unsupported_profile;

  GST_VAAPI_ENCODER_CAST (encoder)->profile = profile;
  return TRUE;

  /* ERRORS */
error_unsupported_profile:
  {
    GST_ERROR ("unsupported HW profile (0x%08x)", encoder->profile);
    return FALSE;
  }
}

static GstVaapiEncoderStatus
set_context_info (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderVP9 *encoder = GST_VAAPI_ENCODER_VP9_CAST (base_encoder);
  GstVideoInfo *const vip = GST_VAAPI_ENCODER_VIDEO_INFO (encoder);

  /*Fixme:  Maximum sizes for common headers (in bytes) */

  if (!ensure_hw_profile (encoder))
    return GST_VAAPI_ENCODER_STATUS_ERROR_UNSUPPORTED_PROFILE;

  base_encoder->num_ref_frames = 3;

  /* Only YUV 4:2:0 formats are supported for now. */
  base_encoder->codedbuf_size = GST_ROUND_UP_16 (vip->width) *
      GST_ROUND_UP_16 (vip->height) * 3 / 2;

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static gboolean
fill_sequence (GstVaapiEncoderVP9 * encoder, GstVaapiEncSequence * sequence)
{
  GstVaapiEncoder *const base_encoder = GST_VAAPI_ENCODER_CAST (encoder);
  VAEncSequenceParameterBufferVP9 *const seq_param = sequence->param;

  memset (seq_param, 0, sizeof (VAEncSequenceParameterBufferVP9));

  /* Fixme: check the requirement of 64 byte alignment ? */
  seq_param->max_frame_width = 8192;
  seq_param->max_frame_height = 8192;

  /* keyframe minimum interval */
  seq_param->kf_min_dist = 1;
  /* keyframe maximum interval */
  seq_param->kf_max_dist = base_encoder->keyframe_period;
  seq_param->intra_period = base_encoder->keyframe_period;

  return TRUE;
}

static gboolean
ensure_sequence (GstVaapiEncoderVP9 * encoder, GstVaapiEncPicture * picture)
{
  GstVaapiEncSequence *sequence;

  g_assert (picture);

  if (picture->type != GST_VAAPI_PICTURE_TYPE_I)
    return TRUE;

  sequence = GST_VAAPI_ENC_SEQUENCE_NEW (VP9, encoder);
  if (!sequence)
    goto error;

  if (!fill_sequence (encoder, sequence))
    goto error;

  gst_vaapi_enc_picture_set_sequence (picture, sequence);
  gst_vaapi_codec_object_replace (&sequence, NULL);
  return TRUE;

error:
  gst_vaapi_codec_object_replace (&sequence, NULL);
  return FALSE;
}

static gboolean
fill_picture (GstVaapiEncoderVP9 * encoder,
    GstVaapiEncPicture * picture,
    GstVaapiCodedBuffer * codedbuf, GstVaapiSurfaceProxy * surface)
{
  VAEncPictureParameterBufferVP9 *const pic_param = picture->param;
  guint i;

  memset (pic_param, 0, sizeof (VAEncPictureParameterBufferVP9));

  pic_param->reconstructed_frame = GST_VAAPI_SURFACE_PROXY_SURFACE_ID (surface);
  pic_param->coded_buf = GST_VAAPI_OBJECT_ID (codedbuf);

  /* Update Reference Frame list */
  if (picture->type == GST_VAAPI_PICTURE_TYPE_I)
    memset (pic_param->reference_frames, 0xFF,
        sizeof (pic_param->reference_frames));
  else {
    for (i = 0; i < G_N_ELEMENTS (pic_param->reference_frames); i++) {
      pic_param->reference_frames[i] =
          GST_VAAPI_SURFACE_PROXY_SURFACE_ID (encoder->ref_list[i]);
    }
  }

  /* It is possible to have dynamic scaling with gpu by providing
   * src and destination resoltuion. For now we are just using
   * default encoder width and height */
  pic_param->frame_width_src = GST_VAAPI_ENCODER_WIDTH (encoder);
  pic_param->frame_height_src = GST_VAAPI_ENCODER_HEIGHT (encoder);
  pic_param->frame_width_dst = GST_VAAPI_ENCODER_WIDTH (encoder);
  pic_param->frame_height_dst = GST_VAAPI_ENCODER_HEIGHT (encoder);

  pic_param->pic_flags.bits.show_frame = 1;

  if (picture->type == GST_VAAPI_PICTURE_TYPE_P) {
    pic_param->pic_flags.bits.frame_type = GST_VP9_INTER_FRAME;

    /* use three of the reference frames (last, golden and altref)
     * for prediction */
    pic_param->ref_flags.bits.ref_frame_ctrl_l0 = 0x7;

    pic_param->ref_flags.bits.ref_last_idx = 0;
    pic_param->ref_flags.bits.ref_gf_idx = 1;
    pic_param->ref_flags.bits.ref_arf_idx = 2;

    /* updating the ref_slot[0] with current frame */
    pic_param->refresh_frame_flags = 0x01;
  }

  /* Fixme: Add tuning options for ac/dc qindex */
  /* Assiging some default values for now */
  pic_param->luma_ac_qindex = 60;
  pic_param->luma_dc_qindex_delta = 1;
  pic_param->chroma_ac_qindex_delta = 1;
  pic_param->chroma_dc_qindex_delta = 1;
  /* Fixme: Add properties to control sharness and loopfilter level */
  pic_param->filter_level = 0;
  pic_param->sharpness_level = 0;

  return TRUE;
}

static gboolean
ensure_picture (GstVaapiEncoderVP9 * encoder, GstVaapiEncPicture * picture,
    GstVaapiCodedBufferProxy * codedbuf_proxy, GstVaapiSurfaceProxy * surface)
{
  GstVaapiCodedBuffer *const codedbuf =
      GST_VAAPI_CODED_BUFFER_PROXY_BUFFER (codedbuf_proxy);

  if (!fill_picture (encoder, picture, codedbuf, surface))
    return FALSE;

  return TRUE;
}

static void
update_ref_list (GstVaapiEncoderVP9 * encoder, GstVaapiEncPicture * picture,
    GstVaapiSurfaceProxy * ref)
{
  guint i;

  if (picture->type == GST_VAAPI_PICTURE_TYPE_I) {
    for (i = 0; i < G_N_ELEMENTS (encoder->ref_list); i++) {
      if (encoder->ref_list[i])
        gst_vaapi_surface_proxy_unref (encoder->ref_list[i]);
      encoder->ref_list[i] = gst_vaapi_surface_proxy_ref (ref);
    }
    gst_vaapi_surface_proxy_unref (ref);
    return;
  }

  gst_vaapi_surface_proxy_unref (encoder->ref_list[0]);
  encoder->ref_list[0] = ref;
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_vp9_encode (GstVaapiEncoder * base_encoder,
    GstVaapiEncPicture * picture, GstVaapiCodedBufferProxy * codedbuf)
{
  GstVaapiEncoderVP9 *const encoder = GST_VAAPI_ENCODER_VP9_CAST (base_encoder);
  GstVaapiEncoderStatus ret = GST_VAAPI_ENCODER_STATUS_ERROR_UNKNOWN;
  GstVaapiSurfaceProxy *reconstruct = NULL;

  reconstruct = gst_vaapi_encoder_create_surface (base_encoder);

  g_assert (GST_VAAPI_SURFACE_PROXY_SURFACE (reconstruct));

  if (!ensure_sequence (encoder, picture))
    goto error;
  if (!ensure_picture (encoder, picture, codedbuf, reconstruct))
    goto error;
  if (!gst_vaapi_enc_picture_encode (picture))
    goto error;

  update_ref_list (encoder, picture, reconstruct);

  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
error:
  if (reconstruct)
    gst_vaapi_encoder_release_surface (GST_VAAPI_ENCODER (encoder),
        reconstruct);
  return ret;
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_vp9_flush (GstVaapiEncoder * base_encoder)
{
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_vp9_reordering (GstVaapiEncoder * base_encoder,
    GstVideoCodecFrame * frame, GstVaapiEncPicture ** output)
{
  GstVaapiEncoderVP9 *const encoder = GST_VAAPI_ENCODER_VP9_CAST (base_encoder);
  GstVaapiEncPicture *picture = NULL;
  GstVaapiEncoderStatus status = GST_VAAPI_ENCODER_STATUS_SUCCESS;

  if (!frame)
    return GST_VAAPI_ENCODER_STATUS_NO_SURFACE;

  picture = GST_VAAPI_ENC_PICTURE_NEW (VP9, encoder, frame);
  if (!picture) {
    GST_WARNING ("create VP9 picture failed, frame timestamp:%"
        GST_TIME_FORMAT, GST_TIME_ARGS (frame->pts));
    return GST_VAAPI_ENCODER_STATUS_ERROR_ALLOCATION_FAILED;
  }

  if (encoder->frame_num >= base_encoder->keyframe_period) {
    encoder->frame_num = 0;
  }
  if (encoder->frame_num == 0) {
    picture->type = GST_VAAPI_PICTURE_TYPE_I;
    GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
  } else {
    picture->type = GST_VAAPI_PICTURE_TYPE_P;
  }

  encoder->frame_num++;
  *output = picture;
  return status;
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_vp9_reconfigure (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderVP9 *const encoder = GST_VAAPI_ENCODER_VP9_CAST (base_encoder);
  GstVaapiEncoderStatus status;

  status = ensure_profile (encoder);
  if (status != GST_VAAPI_ENCODER_STATUS_SUCCESS)
    return status;

  return set_context_info (base_encoder);
}

static gboolean
gst_vaapi_encoder_vp9_init (GstVaapiEncoder * base_encoder)
{
  GstVaapiEncoderVP9 *const encoder = GST_VAAPI_ENCODER_VP9_CAST (base_encoder);

  encoder->frame_num = 0;
  memset (encoder->ref_list, 0, G_N_ELEMENTS (encoder->ref_list));
  return TRUE;
}

static void
gst_vaapi_encoder_vp9_finalize (GstVaapiEncoder * base_encoder)
{
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_vp9_set_property (GstVaapiEncoder * base_encoder,
    gint prop_id, const GValue * value)
{
  switch (prop_id) {
    default:
      return GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_PARAMETER;
  }
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;
}

GST_VAAPI_ENCODER_DEFINE_CLASS_DATA (VP9);

static inline const GstVaapiEncoderClass *
gst_vaapi_encoder_vp9_class (void)
{
  static const GstVaapiEncoderClass GstVaapiEncoderVP9Class = {
    GST_VAAPI_ENCODER_CLASS_INIT (VP9, vp9),
    .set_property = gst_vaapi_encoder_vp9_set_property,
  };
  return &GstVaapiEncoderVP9Class;
}

/**
 * gst_vaapi_encoder_vp9_new:
 * @display: a #GstVaapiDisplay
 *
 * Creates a new #GstVaapiEncoder for VP9 encoding.
 *
 * Return value: the newly allocated #GstVaapiEncoder object
 */
GstVaapiEncoder *
gst_vaapi_encoder_vp9_new (GstVaapiDisplay * display)
{
  return gst_vaapi_encoder_new (gst_vaapi_encoder_vp9_class (), display);
}

/**
 * gst_vaapi_encoder_vp9_get_default_properties:
 *
 * Determines the set of common and vp9 specific encoder properties.
 * The caller owns an extra reference to the resulting array of
 * #GstVaapiEncoderPropInfo elements, so it shall be released with
 * g_ptr_array_unref() after usage.
 *
 * Return value: the set of encoder properties for #GstVaapiEncoderVP9,
 *   or %NULL if an error occurred.
 */
GPtrArray *
gst_vaapi_encoder_vp9_get_default_properties (void)
{
  const GstVaapiEncoderClass *const klass = gst_vaapi_encoder_vp9_class ();
  GPtrArray *props;

  props = gst_vaapi_encoder_properties_get_default (klass);
  if (!props)
    return NULL;

  return props;
}