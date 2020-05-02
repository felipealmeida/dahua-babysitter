///////////////////////////////////////////////////////////////////////////////
//
// Copyright 2018 Felipe Magno de Almeida.
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
// See http://www.boost.org/libs/foreach for documentation
//

#ifndef RTVC_SRC_PIPELINE_HPP
#define RTVC_SRC_PIPELINE_HPP

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#include <string>
#include <stdexcept>
#include <iostream>

#include <boost/signals2.hpp>

namespace rtvc { namespace pipeline {

struct source
{
  GstElement *dmsssrc;
  GstElement *dmssdemux;
  GstElement *audio_decodebin;
  GstElement *audioconvert;
  GstElement *filter;
  GstElement *audioresample;
  GstElement *appsink;
  GstElement *audio_queue1, *audio_queue2;
  GstElement *rganalysis;
  GstElement *video_queue;
  GstElement *video_appsink;
  GstElement *pipeline;
  double current_level;
  gulong bus_connection;
  boost::signals2::signal <void (GstSample*)> sample_signal;  
  boost::signals2::signal <void (GstSample*)> sample_video_signal;  

  source() : dmsssrc (nullptr), dmssdemux(nullptr), audio_decodebin(nullptr)
           , audioconvert(nullptr), filter(nullptr), audioresample(nullptr)
           , appsink(nullptr), video_appsink(nullptr), pipeline(nullptr)
           , rganalysis(nullptr)
           , current_level(0.)
           , sample_signal{}, sample_video_signal{}
  {
    std::cout << "default constructor " << this << std::endl;
  }
  source (std::string const& host, unsigned short port, std::string username
          , std::string const& password
          , unsigned int channel, unsigned int subchannel)
    : dmsssrc (gst_element_factory_make ("dmsssrc", "dmsssrc"))
    , dmssdemux (gst_element_factory_make ("dmssdemux", "dmssdemux"))
    , audio_decodebin (gst_element_factory_make ("decodebin", "decodebin"))
    , audioconvert (gst_element_factory_make ("audioconvert", "audioconvert"))
    , filter (gst_element_factory_make ("audiocheblimit", "audiocheblimit"))
    , audioresample (gst_element_factory_make ("audioresample", "audioresample"))
    , appsink (gst_element_factory_make ("appsink", "audio_appsink"))
    , video_appsink (gst_element_factory_make ("appsink", "video_appsink"))
    , video_queue (gst_element_factory_make ("queue", "video_queue"))
    , audio_queue1 (gst_element_factory_make ("queue", "audio_queue1"))
    , audio_queue2 (gst_element_factory_make ("queue", "audio_queue2"))
    , rganalysis (gst_element_factory_make ("rganalysis", "rganalysis"))
    , pipeline (gst_pipeline_new ("source_pipeline"))
  {
    std::cout << "normal constructor " << this << std::endl;
    if (!dmsssrc)
      throw std::runtime_error ("Couldn't create dmsssrc gstreamer plugin");
    if (!dmssdemux)
      throw std::runtime_error ("Couldn't create dmssdemux gstreamer plugin");
    if(!audio_decodebin)
      throw std::runtime_error ("Couldn't create decodebin gstreamer plugin");
    if (!audioconvert)
      throw std::runtime_error ("Couldn't create audioconvert gstreamer plugin");
    if (!filter)
      throw std::runtime_error ("Couldn't create audiocheblimit gstreamer plugin");
    if (!audioresample)
      throw std::runtime_error ("Couldn't create audioresample gstreamer plugin");
    if (!appsink)
      throw std::runtime_error ("Couldn't create appsink gstreamer plugin");
    if (!video_appsink)
      throw std::runtime_error ("Couldn't create video appsink gstreamer plugin");
    if (!audio_queue1)
      throw std::runtime_error ("Couldn't create queue1 gstreamer plugin");
    if (!audio_queue2)
      throw std::runtime_error ("Couldn't create queue2 gstreamer plugin");
    if (!rganalysis)
      throw std::runtime_error ("Couldn't create rganalysis gstreamer plugin");
    if (!pipeline)
      throw std::runtime_error ("Couldn't create pipeline for source");

    g_object_set (G_OBJECT (dmsssrc), "host", host.c_str(), "port", port, "user", username.c_str(), "password", password.c_str()
                  , "channel", channel, "subchannel", subchannel, NULL);
    g_object_set (G_OBJECT (dmsssrc), "timeout", 15, NULL);
    g_object_set (G_OBJECT (rganalysis), "message", TRUE, NULL);

    GstPad* appsink_sinkpad = gst_element_get_static_pad (audio_queue2, "sink");
    g_signal_connect (audio_decodebin, "pad-added", G_CALLBACK (decodebin_newpad), appsink_sinkpad);

    {
      GstPad* decodebin_sinkpad = gst_element_get_static_pad (audio_queue1, "sink");
      g_signal_connect (dmssdemux, "pad-added", G_CALLBACK (dmssdemux_newpad), decodebin_sinkpad);
    }

    std::cout << "this is " << this << std::endl;
    
    GstAppSinkCallbacks callbacks1
      = {
         &appsink_eos
         , &appsink_preroll
         , &appsink_sample
        };
    gst_app_sink_set_callbacks ( GST_APP_SINK(appsink), &callbacks1, this, appsink_notify_destroy);
    GstAppSinkCallbacks callbacks2
      = {
         &appsink_eos
         , &appsink_preroll
         , &appsink_video_sample
        };
    gst_app_sink_set_callbacks ( GST_APP_SINK(video_appsink), &callbacks2, this, appsink_notify_destroy);

    gst_bin_add_many (GST_BIN (pipeline), dmsssrc, dmssdemux, audio_decodebin, audioconvert, filter, audioresample, appsink
                      , audio_queue1, audio_queue2, rganalysis, video_queue, video_appsink, NULL);
    if (gst_element_link_many (dmsssrc, dmssdemux, video_queue, video_appsink, NULL) != TRUE
        || gst_element_link_many (audio_queue2, rganalysis, audioconvert, audioresample, appsink, NULL) != TRUE
        || gst_element_link_many (audio_queue1, audio_decodebin, NULL) != TRUE
        )
    {
      gst_object_unref (pipeline);
      throw std::runtime_error ("Elements could not be linked");
    }

    GstBus* bus = gst_element_get_bus (pipeline);
    gst_bus_add_signal_watch (bus);
    bus_connection = g_signal_connect (G_OBJECT (bus), "message", G_CALLBACK (&source::message_cb), this);
    std::cout << "registered " << bus_connection << std::endl;
    gst_object_unref (GST_OBJECT (bus));
  }

  ~source ()
  {
    if (dmsssrc)
    {
      std::cout << "should free elements" << std::endl;
      GstBus* bus = gst_element_get_bus (pipeline);
      std::cout << "clearing " << bus_connection << std::endl;
      g_clear_signal_handler(&bus_connection, bus);
      gst_object_unref (GST_OBJECT (bus));
    }
    else
    {
      std::cout << "already moved object" << std::endl;
    }
  }
  
  source (source const&) = delete;

  void swap (source& other)
  {
    using std::swap;
    std::swap(dmsssrc, other.dmsssrc);
    std::swap(dmssdemux, other.dmssdemux);
    std::swap(audio_decodebin, other.audio_decodebin);
    std::swap(audioconvert, other.audioconvert);
    std::swap(filter, other.filter);
    std::swap(audioresample, other.audioresample);
    std::swap(appsink, other.appsink);
    std::swap(video_appsink, other.video_appsink);
    std::swap(pipeline, other.pipeline);
    std::swap(bus_connection, other.bus_connection);
    std::swap(current_level, other.current_level);
    swap(sample_signal, other.sample_signal);
    swap(sample_video_signal, other.sample_video_signal);
    if (appsink)
    {
      GstAppSinkCallbacks callbacks1
        = {
           &appsink_eos
           , &appsink_preroll
           , &appsink_sample
          };

      gst_app_sink_set_callbacks ( GST_APP_SINK(appsink), &callbacks1, this, appsink_notify_destroy);
      GstAppSinkCallbacks callbacks2
        = {
           &appsink_eos
           , &appsink_preroll
           , &appsink_video_sample
          };
      gst_app_sink_set_callbacks ( GST_APP_SINK(video_appsink), &callbacks2, this, appsink_notify_destroy);
    }
    if (other.appsink)
    {
      GstAppSinkCallbacks callbacks1
        = {
           &appsink_eos
           , &appsink_preroll
           , &appsink_sample
          };

      gst_app_sink_set_callbacks ( GST_APP_SINK(other.appsink), &callbacks1, &other, appsink_notify_destroy);
      GstAppSinkCallbacks callbacks2
        = {
           &appsink_eos
           , &appsink_preroll
           , &appsink_video_sample
          };
      gst_app_sink_set_callbacks ( GST_APP_SINK(other.video_appsink), &callbacks2, &other, appsink_notify_destroy);
    }

    if (pipeline)
    {
      GstBus* bus = gst_element_get_bus (pipeline);
      std::cout << "clearing " << bus_connection << std::endl;
      g_clear_signal_handler(&bus_connection, bus);
    
      gst_bus_add_signal_watch (bus);
      bus_connection = g_signal_connect (G_OBJECT (bus), "message", G_CALLBACK (&source::message_cb), this);
      std::cout << "registered " << bus_connection << std::endl;
      gst_object_unref (GST_OBJECT (bus));
    }

    if (other.pipeline)
    {
      GstBus* bus = gst_element_get_bus (other.pipeline);
      std::cout << "clearing " << other.bus_connection << std::endl;
      g_clear_signal_handler(&other.bus_connection, bus);
    
      gst_bus_add_signal_watch (bus);
      other.bus_connection = g_signal_connect (G_OBJECT (bus), "message", G_CALLBACK (&source::message_cb), &other);
      std::cout << "registered " << other.bus_connection << std::endl;
      gst_object_unref (GST_OBJECT (bus));
    }
  }
  
  source& operator=(source other)
  {
    std::cout << "move operator assignment " << this << " from " << &other << std::endl;
    other.swap(*this);
    return *this;
  }
  
  source (source && other)
    : dmsssrc(other.dmsssrc), dmssdemux(other.dmssdemux), audio_decodebin(other.audio_decodebin)
    , audioconvert(other.audioconvert), filter(other.filter)
    , audioresample(other.audioresample), appsink(other.appsink), video_appsink(other.video_appsink)
    , pipeline(other.pipeline), sample_signal(std::move(other.sample_signal))
    , sample_video_signal(std::move(other.sample_video_signal))
    , bus_connection(other.bus_connection), current_level(other.current_level)
  {
    std::cout << "move constructor " << this << " from " << &other << std::endl;
    other.dmsssrc = nullptr;
    other.dmssdemux = nullptr;
    other.audio_decodebin = nullptr;
    other.audioconvert = nullptr;
    other.filter = nullptr;
    other.audioresample = nullptr;
    other.appsink = nullptr;
    other.video_appsink = nullptr;
    std::cout << "MOVED this is " << this << std::endl;
    GstAppSinkCallbacks callbacks1
      = {
         &appsink_eos
         , &appsink_preroll
         , &appsink_sample
        };

    gst_app_sink_set_callbacks ( GST_APP_SINK(appsink), &callbacks1, this, appsink_notify_destroy);
    GstAppSinkCallbacks callbacks2
      = {
         &appsink_eos
         , &appsink_preroll
         , &appsink_video_sample
        };
    gst_app_sink_set_callbacks ( GST_APP_SINK(video_appsink), &callbacks2, this, appsink_notify_destroy);

    GstBus* bus = gst_element_get_bus (pipeline);
    std::cout << "clearing " << bus_connection << std::endl;
    g_clear_signal_handler(&bus_connection, bus);
    
    bus_connection = g_signal_connect (G_OBJECT (bus), "message", G_CALLBACK (&source::message_cb), this);
    std::cout << "registered " << bus_connection << std::endl;
    gst_object_unref (GST_OBJECT (bus));
  }
  
private:
  static void decodebin_newpad (GstElement *decodebin, GstPad *pad, gpointer data)
  {
    std::cout << "decodebin_newpad" << std::endl;
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

  static void dmssdemux_newpad (GstElement *decodebin, GstPad *pad, gpointer data)
  {
    std::cout << "dmssdemux_newpad " << gst_pad_get_name (pad) << std::endl;

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
  
  static void appsink_eos (GstAppSink *appsink, gpointer user_data)
  {
    std::cout << "eos" << std::endl;
  }
  static GstFlowReturn appsink_preroll (GstAppSink *appsink, gpointer user_data)
  {
    std::cout << "preroll" << std::endl;
    return GST_FLOW_OK;
  }
  static void appsink_notify_destroy (gpointer user_data)
  {
  }
  static GstFlowReturn appsink_sample (GstAppSink *appsink, gpointer user_data)
  {
    //std::cout << "appsink sample " << user_data << std::endl;
    source* self = static_cast<source*>(user_data);

    assert (!!self->appsink);
    assert (self->appsink == GST_ELEMENT(appsink));
    
    GstSample* sample = gst_app_sink_pull_sample (GST_APP_SINK (appsink));
    self->sample_signal (sample);
    gst_sample_unref (sample);

    return GST_FLOW_OK;
  }
  static GstFlowReturn appsink_video_sample (GstAppSink *appsink, gpointer user_data)
  {
    //std::cout << "appsink sample " << user_data << std::endl;
    source* self = static_cast<source*>(user_data);

    assert (!!self->video_appsink);
    assert (self->video_appsink == GST_ELEMENT(appsink));
    
    GstSample* sample = gst_app_sink_pull_sample (GST_APP_SINK (appsink));
    self->sample_video_signal (sample);
    gst_sample_unref (sample);

    return GST_FLOW_OK;
  }

  static gboolean
  message_cb (GstBus * bus, GstMessage * message, gpointer user_data)
  {
    //std::cout << "message_cb " << GST_MESSAGE_TYPE (message) << std::endl;
    if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ELEMENT)
    {
      ///std::cout << "which kind of element maybe? " << gst_structure_get_name (gst_message_get_structure(message)) << std::endl;
      source* self = static_cast<source*>(user_data);
      const GstStructure *s = gst_message_get_structure (message);
      if (gst_structure_has_name (s, "rganalysis"))
      {
        if (gst_structure_has_field (s, "rglevel"))
        {
          double level = 0;
          if (gst_structure_get_double (s, "rglevel", &level))
          {
            if (level > -10.)
              std::cout << "level for current window is " << level << std::endl;
            self->current_level = level;
          }
        }
      }        
    }
    else if(GST_MESSAGE_TYPE (message) == GST_MESSAGE_ELEMENT)
    {
      std::cout << "error" << std::endl; 
    }
  }

};
  
} }

#endif
