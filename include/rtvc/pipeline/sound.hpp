///////////////////////////////////////////////////////////////////////////////
//
// Copyright 2018 Felipe Magno de Almeida.
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
// See http://www.boost.org/libs/foreach for documentation
//

#ifndef RTVC_PIPELINE_SOUND_HPP
#define RTVC_PIPELINE_SOUND_HPP

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

#include <string>
#include <stdexcept>

namespace rtvc { namespace pipeline {

struct sound_sink
{
  std::vector<GstElement*> appsrc;
  std::vector<GstElement*> audioconvert;
  GstElement *audiomixer;
  GstElement *sink;
  GstElement *pipeline;

  sound_sink (int sources)
    : audiomixer (gst_element_factory_make ("audiomixer", "audiomixer"))
    , sink (gst_element_factory_make ("autoaudiosink", "autoaudiosink"))
    , pipeline (gst_pipeline_new ("sink_pipeline"))
  {
    appsrc.resize(sources);
    audioconvert.resize(sources);
    if (!audiomixer)
      throw std::runtime_error ("Couldn't create audiomixer plugin");
    if (!sink)
      throw std::runtime_error ("Couldn't create autoaudiosink plugin");
    if (!pipeline)
      throw std::runtime_error ("Couldn't create pipeline");
    gst_bin_add_many (GST_BIN (pipeline), audiomixer, sink, NULL);

    {
      unsigned int i = 0;
      for (auto&& src : appsrc)
      {
        std::string name = "appsrc";
        name += std::to_string(++i);
        src = gst_element_factory_make ("appsrc", name.c_str());
        if (!src)
          throw std::runtime_error ("Not all elements could be created in visualization.");
        g_object_set (G_OBJECT (src), "format", GST_FORMAT_TIME, NULL);
        g_object_set (G_OBJECT (src), "is-live", TRUE, NULL);
        gst_app_src_set_stream_type(GST_APP_SRC(src), GST_APP_STREAM_TYPE_STREAM);
        gst_bin_add (GST_BIN (pipeline), src);
      }

      i = 0;
      for (auto&& source : appsrc)
      {
        std::cout << "creating audioconvert elements" << std::endl;
        std::string name = "audioconvert";
        name += std::to_string(i);
        GstElement* convert = gst_element_factory_make ("audioconvert", name.c_str());
        if (!convert)
          throw std::runtime_error ("Not all elements could be created in visualization.");
        std::cout << "created" << std::endl;
        gst_bin_add (GST_BIN (pipeline), convert);
        gst_element_link (source, convert);
        auto src_pad = gst_element_get_static_pad (convert, "src");
        assert (!!src_pad);
        auto sink_pad = gst_element_get_request_pad (audiomixer, "sink_%u");
        assert (!!sink_pad);

        std::cout << "pad name: " << gst_pad_get_name (sink_pad) << std::endl;
        gst_pad_link (src_pad, sink_pad);

        ++i;
      }
    }
    
    if (gst_element_link_many (audiomixer, sink, NULL) != TRUE)
    {
      throw std::runtime_error ("Elements could not be linked.\n");
    }    
    
  }
};
    
} }

#endif
