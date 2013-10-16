/*
 * (C) Copyright 2013 Kurento (http://kurento.org/)
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 */

#include <gst/check/gstcheck.h>
#include <gst/sdp/gstsdpmessage.h>
#include <gst/gst.h>
#include <glib.h>

static const gchar *pattern_offer_sdp_str = "v=0\r\n"
    "o=- 0 0 IN IP4 0.0.0.0\r\n"
    "s=TestSession\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "t=0 0\r\n"
    "m=video 0 RTP/AVP 96 97\r\n"
    "a=rtpmap:96 H263-1998/90000\r\n"
    "a=rtpmap:97 VP8/90000\r\n"
    "m=audio 0 RTP/AVP 98 99\r\n"
    "a=rtpmap:98 OPUS/48000/1\r\n" "a=rtpmap:99 AMR/8000/1\r\n";

static const gchar *pattern_answer_sdp_str = "v=0\r\n"
    "o=- 0 0 IN IP4 0.0.0.0\r\n"
    "s=TestSession\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "t=0 0\r\n"
    "m=video 0 RTP/AVP 99\r\n"
    "a=rtpmap:99 VP8/90000\r\n"
    "m=audio 0 RTP/AVP 100\r\n" "a=rtpmap:100 OPUS/48000/1\r\n";

static const gchar *pattern_sdp_str = "v=0\r\n"
    "o=- 0 0 IN IP4 0.0.0.0\r\n"
    "s=TestSession\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "t=0 0\r\n"
    "m=video 0 RTP/AVP 96\r\n"
    "a=rtpmap:96 VP8/90000\r\n"
    "m=audio 0 RTP/AVP 97\r\n" "a=rtpmap:97 OPUS/48000/1\r\n";

static gboolean
quit_main_loop (gpointer data)
{
  g_main_loop_quit (data);
  return FALSE;
}

static void
bus_msg (GstBus * bus, GstMessage * msg, gpointer pipe)
{

  switch (msg->type) {
    case GST_MESSAGE_ERROR:{
      GST_ERROR ("Error: %" GST_PTR_FORMAT, msg);
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipe),
          GST_DEBUG_GRAPH_SHOW_ALL, "error");
      fail ("Error received on bus");
      break;
    }
    case GST_MESSAGE_WARNING:{
      GST_WARNING ("Warning: %" GST_PTR_FORMAT, msg);
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipe),
          GST_DEBUG_GRAPH_SHOW_ALL, "warning");
      break;
    }
    default:
      break;
  }
}

static void
fakesink_hand_off (GstElement * fakesink, GstBuffer * buf, GstPad * pad,
    gpointer data)
{
  static int count = 0;
  GMainLoop *loop = (GMainLoop *) data;

  count++;
  GST_DEBUG ("count: %d", count);
  if (count > 40) {
    g_object_set (G_OBJECT (fakesink), "signal-handoffs", FALSE, NULL);
    g_idle_add (quit_main_loop, loop);
  }
}

static gboolean
timeout_check (gpointer pipeline)
{
  gchar *timeout_file =
      g_strdup_printf ("timeout-%s", GST_OBJECT_NAME (pipeline));

  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
      GST_DEBUG_GRAPH_SHOW_ALL, timeout_file);
  g_free (timeout_file);
  return FALSE;
}

static gboolean
connect_output_sink (gpointer data)
{
  GstElement *pipeline = data;
  GstElement *rtpendpointreceiver = gst_bin_get_by_name (GST_BIN (pipeline),
      "receiver");
  GstElement *outputfakesink = gst_element_factory_make ("fakesink", NULL);
  GMainLoop *loop = g_object_get_data (G_OBJECT (pipeline), "loop");
  GstElement *agnosticbin = gst_bin_get_by_name (GST_BIN (pipeline),
      "agnosticbin");
  GstElement *rtpendpointsender = gst_bin_get_by_name (GST_BIN (pipeline),
      "sender");

  gst_element_link_pads (agnosticbin, NULL, rtpendpointsender, "video_sink");
  g_object_unref (agnosticbin);
  g_object_unref (rtpendpointsender);

  g_object_set (G_OBJECT (outputfakesink), "signal-handoffs", TRUE, NULL);
  g_signal_connect (G_OBJECT (outputfakesink), "handoff",
      G_CALLBACK (fakesink_hand_off), loop);

  gst_bin_add (GST_BIN (pipeline), outputfakesink);
  gst_element_sync_state_with_parent (outputfakesink);

  gst_element_link_pads (rtpendpointreceiver, "video_src_%u", outputfakesink,
      "sink");

  g_object_unref (rtpendpointreceiver);

  return FALSE;
}

GST_START_TEST (loopback)
{
  GMainLoop *loop = g_main_loop_new (NULL, TRUE);
  GstSDPMessage *pattern_sdp, *offer, *answer;
  GstElement *pipeline = gst_pipeline_new (__FUNCTION__);
  GstElement *videotestsrc = gst_element_factory_make ("videotestsrc", NULL);
  GstElement *fakesink = gst_element_factory_make ("fakesink", NULL);
  GstElement *agnosticbin =
      gst_element_factory_make ("agnosticbin", "agnosticbin");
  GstElement *rtpendpointsender =
      gst_element_factory_make ("rtpendpoint", "sender");
  GstElement *rtpendpointreceiver =
      gst_element_factory_make ("rtpendpoint", "receiver");

  GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));

  g_object_set (G_OBJECT (pipeline), "async-handling", TRUE, NULL);

  g_object_set_data (G_OBJECT (pipeline), "loop", loop);

  gst_bus_add_watch (bus, gst_bus_async_signal_func, NULL);
  g_signal_connect (bus, "message", G_CALLBACK (bus_msg), pipeline);
  g_object_unref (bus);

  fail_unless (gst_sdp_message_new (&pattern_sdp) == GST_SDP_OK);
  fail_unless (gst_sdp_message_parse_buffer ((const guint8 *)
          pattern_sdp_str, -1, pattern_sdp) == GST_SDP_OK);

  g_object_set (rtpendpointsender, "pattern-sdp", pattern_sdp, NULL);
  g_object_set (rtpendpointreceiver, "pattern-sdp", pattern_sdp, NULL);
  fail_unless (gst_sdp_message_free (pattern_sdp) == GST_SDP_OK);

  g_object_set (G_OBJECT (fakesink), "sync", TRUE, NULL);

  gst_bin_add_many (GST_BIN (pipeline), videotestsrc, agnosticbin, fakesink,
      NULL);
  gst_element_link_many (videotestsrc, agnosticbin, fakesink, NULL);

  gst_bin_add_many (GST_BIN (pipeline), rtpendpointreceiver, rtpendpointsender,
      NULL);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  mark_point ();
  g_signal_emit_by_name (rtpendpointsender, "generate-offer", &offer);
  fail_unless (offer != NULL);

  mark_point ();
  g_signal_emit_by_name (rtpendpointreceiver, "process-offer", offer, &answer);
  fail_unless (answer != NULL);

  mark_point ();
  g_signal_emit_by_name (rtpendpointsender, "process-answer", answer);
  gst_sdp_message_free (offer);
  gst_sdp_message_free (answer);

  gst_element_set_state (rtpendpointsender, GST_STATE_PLAYING);
  gst_element_set_state (rtpendpointreceiver, GST_STATE_PLAYING);

  g_timeout_add (500, connect_output_sink, pipeline);
  g_timeout_add_seconds (10, timeout_check, pipeline);

  mark_point ();
  g_main_loop_run (loop);
  mark_point ();

  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
      GST_DEBUG_GRAPH_SHOW_ALL, __FUNCTION__);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_object_unref (pipeline);
}

GST_END_TEST
GST_START_TEST (negotiation_offerer)
{
  GstSDPMessage *pattern_sdp;
  GstElement *offerer = gst_element_factory_make ("rtpendpoint", NULL);
  GstElement *answerer = gst_element_factory_make ("rtpendpoint", NULL);
  GstSDPMessage *offer = NULL, *answer = NULL;
  GstSDPMessage *local_offer = NULL, *local_answer = NULL, *remote_offer =
      NULL, *remote_answer = NULL;
  gchar *local_offer_str, *local_answer_str, *remote_offer_str,
      *remote_answer_str;
  gchar *aux = NULL;

  fail_unless (gst_sdp_message_new (&pattern_sdp) == GST_SDP_OK);
  fail_unless (gst_sdp_message_parse_buffer ((const guint8 *)
          pattern_offer_sdp_str, -1, pattern_sdp) == GST_SDP_OK);

  g_object_set (offerer, "pattern-sdp", pattern_sdp, NULL);
  fail_unless (gst_sdp_message_free (pattern_sdp) == GST_SDP_OK);
  g_object_get (offerer, "pattern-sdp", &pattern_sdp, NULL);
  fail_unless (pattern_sdp != NULL);
  fail_unless (gst_sdp_message_free (pattern_sdp) == GST_SDP_OK);

  fail_unless (gst_sdp_message_new (&pattern_sdp) == GST_SDP_OK);
  fail_unless (gst_sdp_message_parse_buffer ((const guint8 *)
          pattern_answer_sdp_str, -1, pattern_sdp) == GST_SDP_OK);
  g_object_set (answerer, "pattern-sdp", pattern_sdp, NULL);
  fail_unless (gst_sdp_message_free (pattern_sdp) == GST_SDP_OK);

  g_signal_emit_by_name (offerer, "generate-offer", &offer);

  fail_unless (offer != NULL);

  GST_DEBUG ("Offer:\n%s", (aux = gst_sdp_message_as_text (offer)));
  g_free (aux);
  aux = NULL;

  g_signal_emit_by_name (answerer, "process-offer", offer, &answer);
  fail_unless (answer != NULL);
  GST_DEBUG ("Answer:\n%s", (aux = gst_sdp_message_as_text (answer)));
  g_free (aux);
  aux = NULL;

  g_signal_emit_by_name (offerer, "process-answer", answer);

  gst_sdp_message_free (offer);
  gst_sdp_message_free (answer);

  g_object_get (offerer, "local-offer-sdp", &local_offer, NULL);
  g_object_get (offerer, "remote-answer-sdp", &remote_answer, NULL);

  g_object_get (answerer, "remote-offer-sdp", &remote_offer, NULL);
  g_object_get (answerer, "local-answer-sdp", &local_answer, NULL);

  fail_unless (local_offer != NULL);
  fail_unless (remote_answer != NULL);
  fail_unless (remote_offer != NULL);
  fail_unless (local_answer != NULL);

  local_offer_str = gst_sdp_message_as_text (local_offer);
  remote_answer_str = gst_sdp_message_as_text (remote_answer);

  remote_offer_str = gst_sdp_message_as_text (remote_offer);
  local_answer_str = gst_sdp_message_as_text (local_answer);

  GST_DEBUG ("Local offer\n%s", local_offer_str);
  GST_DEBUG ("Remote answer\n%s", remote_answer_str);
  GST_DEBUG ("Remote offer\n%s", remote_offer_str);
  GST_DEBUG ("Local answer\n%s", local_answer_str);

  fail_unless (g_strcmp0 (local_offer_str, remote_offer_str) == 0);
  fail_unless (g_strcmp0 (remote_answer_str, local_answer_str) == 0);

  g_free (local_offer_str);
  g_free (remote_answer_str);
  g_free (local_answer_str);
  g_free (remote_offer_str);

  gst_sdp_message_free (local_offer);
  gst_sdp_message_free (remote_answer);
  gst_sdp_message_free (remote_offer);
  gst_sdp_message_free (local_answer);

  g_object_unref (offerer);
  g_object_unref (answerer);
}

GST_END_TEST
/*
 * End of test cases
 */
static Suite *
sdp_suite (void)
{
  Suite *s = suite_create ("rtpendpoint");
  TCase *tc_chain = tcase_create ("element");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, negotiation_offerer);
  tcase_add_test (tc_chain, loopback);

  return s;
}

GST_CHECK_MAIN (sdp);
