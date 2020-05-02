///////////////////////////////////////////////////////////////////////////////
//
// Copyright 2018 Felipe Magno de Almeida.
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
// See http://www.boost.org/libs/foreach for documentation
//

#ifndef RTVC_PIPELINE_VISUALIZATION_HPP
#define RTVC_PIPELINE_VISUALIZATION_HPP

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

#include <string>
#include <stdexcept>

namespace rtvc { namespace pipeline {

struct visualization
{
  GstElement* appsrc;
  GstElement* decodebin;
  GstElement* queue;
  GstElement* videoscale;
  GstElement* scale_capsfilter;
  GstElement* videoconvert;
  GstElement *sink;
  GstElement *pipeline;

  visualization (int width, int height)
    : appsrc (gst_element_factory_make ("appsrc", "video_appsrc"))
    , decodebin (gst_element_factory_make ("decodebin", "video_decodebin"))
    , queue (gst_element_factory_make ("queue", "video_queue"))
    , videoscale (gst_element_factory_make ("videoscale", "videoscale"))
    , scale_capsfilter (gst_element_factory_make ("capsfilter", "scale_capsfilter"))
    , videoconvert (gst_element_factory_make ("videoconvert", "videoconvert"))
    , sink (gst_element_factory_make ("autovideosink", "autovideoxsink"))
    , pipeline (gst_pipeline_new ("video_pipeline"))
  {
    if (!appsrc)
      throw std::runtime_error ("Couldn't create appsrc plugin");
    if (!decodebin)
      throw std::runtime_error ("Couldn't create video decodebin plugin");
    if (!videoscale)
      throw std::runtime_error ("Couldn't create videoscale plugin");
    if (!scale_capsfilter)
      throw std::runtime_error ("Couldn't create capsfilter plugin");
    if (!videoconvert)
      throw std::runtime_error ("Couldn't create videoconvert plugin");
    if (!sink)
      throw std::runtime_error ("Couldn't create autovideosink plugin");
    if (!pipeline)
      throw std::runtime_error ("Couldn't create video pipeline");

    g_object_set (G_OBJECT (appsrc), "format", GST_FORMAT_TIME, NULL);
    g_object_set (G_OBJECT (appsrc), "is-live", TRUE, NULL);
    gst_app_src_set_stream_type(GST_APP_SRC(appsrc), GST_APP_STREAM_TYPE_STREAM);

    GstCaps* scale_caps = gst_caps_new_simple("video/x-raw", "width", G_TYPE_INT, width, "height", G_TYPE_INT, height, NULL);
    g_object_set(G_OBJECT (scale_capsfilter), "caps", scale_caps, NULL);
    gst_caps_unref (scale_caps);
    
    GstPad* queue_sinkpad = gst_element_get_static_pad (queue, "sink");
    g_signal_connect (decodebin, "pad-added", G_CALLBACK (decodebin_newpad), queue_sinkpad);

    gst_bin_add_many (GST_BIN (pipeline), appsrc, decodebin, queue, videoscale, scale_capsfilter, videoconvert, sink, NULL);
    if (gst_element_link_many (appsrc, decodebin, NULL) != TRUE
        || gst_element_link_many (queue, videoscale, scale_capsfilter, videoconvert, sink, NULL) != TRUE)
    {
      throw std::runtime_error ("Elements could not be linked.\n");
    }    
    
  }
  ~visualization()
  {
    gst_object_unref (pipeline);
  }

  static void decodebin_newpad (GstElement *decodebin, GstPad *pad, gpointer data)
  {
    std::cout << "video decodebin_newpad " << gst_pad_get_name (pad) << std::endl;
    GstPad* sinkpad = static_cast<GstPad*>(data);

    if (!GST_PAD_IS_LINKED (sinkpad))
    {
      gst_pad_link (pad, sinkpad);
    }
    else
    {
      // 
    }
  }
};
    
} }

#endif
