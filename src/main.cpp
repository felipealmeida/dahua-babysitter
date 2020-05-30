/*
[felipe@felipe-desktop gst-plugins]$ GST_DEBUG=dmssdemux:3 GST_PLUGIN_PATH=. gst-launch-1.0 dmsssrc host=nvr.localdomain port=57777 user=admin password=
awvapq1 channel=5 ! dmssdemux  latency=2000 name=d d.audio ! queue ! decodebin ! audioconvert ! audiocheblimit mode=low-pass cutoff=4000 ripple=0.4 ! audior
esample ! audiomixer name=m ! autoaudiosink  dmsssrc host=nvr.localdomain user=admin password= port=57777 channel=13 ! dmssdemux name=d2 d2.audio
 ! queue ! decodebin ! queue ! audioconvert ! audiocheblimit mode=high-pass cutoff=400 ripple=0.2 ! audioresample ! m.
 */
#include <rtvc/pipeline/source.hpp>
#include <rtvc/pipeline/sound.hpp>
#include <rtvc/pipeline/visualization.hpp>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>

#include <stdio.h>
#include <iostream>

#include <boost/program_options.hpp>
#include <boost/dynamic_bitset.hpp>

#include <sys/wait.h>

/* This function is called when an error message is posted on the bus */
template <typename F>
static void error_cb (GstBus *bus, GstMessage *msg, void *data)
{
  (*static_cast<F*>(data)) (bus, msg);
}

void turn_monitor_on ()
{
  char* argv[] = {"/usr/bin/xset", "dpms", "force", "on", NULL};
  int status;
  auto pid = fork();
  if (!pid)
    execve ("/usr/bin/xset",  argv, environ);
  else
    waitpid (pid, &status, 0);
}

void turn_monitor_off ()
{
  char* argv[] = {"/usr/bin/xset", "dpms", "force", "off", NULL};
  int status;
  auto pid = fork();
  if (!pid)
    execve ("/usr/bin/xset",  argv, environ);
  else
    waitpid (pid, &status, 0);
}

int
main (int   argc,
      char *argv[])
{
  guint major, minor, micro, nano;

  std::vector<std::string> hosts;
  std::string host2, user, password;
  std::vector<int> ports;
  std::vector<int> channels;
  int port2;
  unsigned int width = 1280, height = 720;
  bool flip = false;
  
  {
    namespace po = boost::program_options;
    po::options_description desc("Allowed options");
    desc.add_options()
      ("help", "produce help message")
      ("host", po::value<std::vector<std::string>>()->multitoken(), "Connection to NVR")
      ("port", po::value<std::vector<int>>()->multitoken(), "Port for connection to NVR")
      ("failover-host", po::value<std::string>(), "Connection failover to NVR")
      ("failover-port", po::value<int>(), "Port for connection failover to NVR")
      ("user", po::value<std::string>(), "User for connection to NVR")
      ("pass", po::value<std::string>(), "Password for connection to NVR")
      ("channel", po::value<std::vector<int>>(), "Channel to show")
      ("width", po::value<unsigned int>(), "Width of the Window")
      ("height", po::value<unsigned int>(), "Height of the Window")
      ("flip", "Flip image 90 degrees clockwise")
      ;

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);    

    if (vm.count("help")
        || !vm.count("host")
        || !vm.count("port")
        || !vm.count("user")
        || !vm.count("pass")
        || !vm.count("channel"))
    {
      std::cout << desc << "\n";
      return 1;
    }

    hosts = vm["host"].as<std::vector<std::string>>();
    user = vm["user"].as<std::string>();
    password = vm["pass"].as<std::string>();
    ports = vm["port"].as<std::vector<int>>();
    channels = vm["channel"].as<std::vector<int>>();

    if (vm.count("compression")) {
      std::cout << "Compression level was set to " 
           << vm["compression"].as<int>() << ".\n";
    } else {
      std::cout << "Compression level was not set.\n";
    }

    if (vm.count("width")) width = vm["width"].as<unsigned int>();
    if (vm.count("height")) height = vm["height"].as<unsigned int>();
    if (vm.count("flip")) flip = true;
  }
  
  gst_init (&argc, &argv);

  gst_version (&major, &minor, &micro, &nano);

  printf ("This program is linked against GStreamer %d.%d.%d\n",
          major, minor, micro);

  std::cout << "window size " << width << "x" << height << std::endl;
  
  std::vector<rtvc::pipeline::source> sources(hosts.size());
  rtvc::pipeline::sound_sink sound_sink(hosts.size());
  boost::dynamic_bitset<> sources_loaded(hosts.size());
  boost::dynamic_bitset<> reset_caps(hosts.size());
  std::vector<unsigned int> threshold_remaining(hosts.size());
  std::unique_ptr<rtvc::pipeline::visualization> visualization;
  {
    unsigned int index = 0;
    for (auto&& host : hosts)
    {
      std::cout << "initializing source" << std::endl;
      sources[index] = std::move(rtvc::pipeline::source{host, ports[index], user, password, channels[index], 1});
      sources[index].sample_signal.connect
        (
         [&,index] (GstSample* sample)
         {
           //std::cout << "appsink " << index << std::endl;
           static GstClockTime timestamp_offset;

           GstBuffer* buffer = gst_sample_get_buffer (sample);
           assert (!!buffer);
           assert (GST_IS_BUFFER (buffer));
           if(!reset_caps[index])
           {
             timestamp_offset = GST_BUFFER_TIMESTAMP (buffer);
             GstCaps* caps = gst_sample_get_caps (sample);

             std::cout << "appsrc caps will be " << gst_caps_to_string (caps) << std::endl;
         
             gst_app_src_set_caps (GST_APP_SRC (sound_sink.appsrc[index]), caps);
             //   gst_app_src_set_caps (GST_APP_SRC (motion_pipeline.appsrc), caps);
             gst_caps_unref (caps);
             reset_caps[index] = true;

             GstBuffer* tmp = gst_buffer_copy (buffer);
             assert (!!tmp);
             assert (GST_IS_BUFFER (tmp));
             //GstBuffer* tmp1 = gst_buffer_copy (buffer);
             // gst_buffer_ref (tmp);
             // gst_buffer_ref (buffer);
             GST_BUFFER_TIMESTAMP (tmp) = 0;
             //GST_BUFFER_TIMESTAMP (tmp1) = 0;
             GstFlowReturn r;
             if ((r = gst_app_src_push_buffer (GST_APP_SRC(sound_sink.appsrc[index]), tmp)) != GST_FLOW_OK)
             {
               std::cout << "Error with gst_app_src_push_buffer for view_pipeline, return " << r << std::endl;
             }
             sources_loaded[index] = true;

             if (sources_loaded.none())
             {
               std::cout << "starting sound sink" << std::endl;
               gst_element_set_state(sound_sink.pipeline, GST_STATE_PLAYING);
             }
             else
             {
               std::cout << "starting sound sink" << std::endl;
               gst_element_set_state(sound_sink.pipeline, GST_STATE_READY);
               gst_element_set_state(sound_sink.pipeline, GST_STATE_PLAYING);
             }
           }
           else if (sources[index].current_level > -10. || threshold_remaining[index] != 0)
           {
             if (!visualization)
             {
               turn_monitor_on ();
               visualization.reset (new rtvc::pipeline::visualization (width, height, flip));
               std::shared_ptr<bool> set_caps (new bool{true});
               sources[index].sample_video_signal.connect
                 ([&, index, set_caps = std::move(set_caps)] (GstSample* sample)
                  {
                    //std::cout << "video sample" << std::endl;
                    static GstClockTime timestamp_offset;

                    GstBuffer* buffer = gst_sample_get_buffer (sample);
                    assert (!!buffer);
                    assert (GST_IS_BUFFER (buffer));
                    if (*set_caps)
                    {
                      timestamp_offset = GST_BUFFER_TIMESTAMP (buffer);
                      GstCaps* caps = gst_sample_get_caps (sample);

                      std::cout << "video appsrc caps will be " << gst_caps_to_string (caps) << std::endl;
         
                      gst_app_src_set_caps (GST_APP_SRC (visualization->appsrc), caps);
                      //   gst_app_src_set_caps (GST_APP_SRC (motion_pipeline.appsrc), caps);
                      gst_caps_unref (caps);
                      *set_caps = false;

                      GstBuffer* tmp = gst_buffer_copy (buffer);
                      assert (!!tmp);
                      assert (GST_IS_BUFFER (tmp));
                      GST_BUFFER_TIMESTAMP (tmp) = 0;
                      GstFlowReturn r;
                      if ((r = gst_app_src_push_buffer (GST_APP_SRC(visualization->appsrc), tmp)) != GST_FLOW_OK)
                      {
                        std::cout << "Error with gst_app_src_push_buffer for view_pipeline, return " << r << std::endl;
                      }

                      gst_element_set_state(visualization->pipeline, GST_STATE_READY);
                      gst_element_set_state(visualization->pipeline, GST_STATE_PLAYING);
                    }
                    else
                    {
                      GstBuffer* tmp = gst_buffer_copy (buffer);
                      assert (!!tmp);
                      assert (GST_IS_BUFFER (tmp));
                      GST_BUFFER_TIMESTAMP (tmp) -= timestamp_offset;
                      GstFlowReturn r;
                      if ((r = gst_app_src_push_buffer (GST_APP_SRC(visualization->appsrc), tmp)) != GST_FLOW_OK)
                      {
                        std::cout << "Error with gst_app_src_push_buffer for view_pipeline, return " << r << std::endl;
                      }
                    }
                  });
             }
             
             if (threshold_remaining[index] == 0)
               threshold_remaining[index] = 500;
             else
             {
               if (--threshold_remaining[index] == 0)
               {
                 std::cout << "Reached 0, stopping video" << std::endl;
                 sources[index].sample_video_signal.disconnect_all_slots ();
                 gst_element_set_state (visualization->pipeline, GST_STATE_NULL);
                 visualization.reset();
                 turn_monitor_off();
               }
             }
             //std::cout << "volume above threshold, pushing" << std::endl;
             // std::cout << "sending buffer" << std::endl;
             GstBuffer* tmp = gst_buffer_copy (buffer);
             //GstBuffer* tmp1 = gst_buffer_copy (buffer);
             // gst_buffer_ref (tmp);
             // gst_buffer_ref (buffer);
             assert (!!tmp);
             assert (GST_IS_BUFFER (tmp));
             GST_BUFFER_TIMESTAMP (tmp) -= timestamp_offset;
             //GST_BUFFER_TIMESTAMP (tmp1) -= timestamp_offset;
             GstFlowReturn r;
             if ((r = gst_app_src_push_buffer (GST_APP_SRC(sound_sink.appsrc[index]), tmp)) != GST_FLOW_OK)
             {
               std::cout << "Error with gst_app_src_push_buffer for sound_sink, return " << r << std::endl;
             }
           }
         }
         );
      ++index;
    }

  }

  gst_pipeline_set_latency(GST_PIPELINE(sound_sink.pipeline), GST_SECOND);

  for (auto&& source : sources)
  {
    gst_element_set_state(source.pipeline, GST_STATE_READY);
  }
  gst_element_set_state(sound_sink.pipeline, GST_STATE_READY);

  GMainLoop* main_loop = g_main_loop_new (NULL, FALSE);

  unsigned int index = 0;
  for (auto&& source : sources)
  {
    gst_element_set_state (source.pipeline, GST_STATE_PLAYING);
    GstBus* bus = gst_element_get_bus (source.pipeline);
    gst_bus_add_signal_watch (bus);

    GError *err;
    gchar *debug_info;

    /* Print error details on the screen */
    auto error_callback = [&,index] (GstBus *bus, GstMessage *msg)
     {
       gst_message_parse_error (msg, &err, &debug_info);
       g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
       g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
       
       if (!strcmp(GST_OBJECT_NAME(msg->src), "dmsssrc"))
       {
         std::cout << "Error happened in dmsssrc" << std::endl;

         gst_element_set_state(sound_sink.pipeline, GST_STATE_PAUSED);
         reset_caps[index] = false;
         gst_element_set_state(sources[index].pipeline, GST_STATE_READY);
         gst_element_set_state(sources[index].pipeline, GST_STATE_PLAYING);
         //gst_element_set_state(sound_sink.pipeline, GST_STATE_PLAYING);
       }
  
       g_clear_error (&err);
       g_free (debug_info);
  
       // g_main_loop_quit (data->main_loop);
     };

    typedef decltype(error_callback) error_callback_type;
    g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb<error_callback_type>, new error_callback_type(error_callback));
    gst_object_unref (GST_OBJECT (bus));
    ++index;
  }

  
  g_main_loop_run (main_loop);
 
  return 0;
}
