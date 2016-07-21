/***
  This file is part of xmms-pulse.

  xmms-pulse is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  xmms-pulse is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with xmms-pulse; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
  USA.
***/

#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <pulse/pulseaudio.h>

#include <libaudcore/runtime.h>
#include <libaudcore/plugin.h>
#include <libaudcore/i18n.h>

class PulseOutput : public OutputPlugin
{
public:
    static const char about[];

    static constexpr PluginInfo info = {
        N_("PulseAudio Output"),
        PACKAGE,
        about
    };

    constexpr PulseOutput () : OutputPlugin (info, 8) {}

    bool init ();

    StereoVolume get_volume ();
    void set_volume (StereoVolume v);

    bool open_audio (int fmt, int rate, int nch, String & error);
    void close_audio ();

    void period_wait ();
    int write_audio (const void * ptr, int length);
    void drain ();

    int get_delay ();

    void pause (bool pause);
    void flush ();
};

EXPORT PulseOutput aud_plugin_instance;

static pa_context *context = nullptr;
static pa_stream *stream = nullptr;
static pa_threaded_mainloop *mainloop = nullptr;

static pa_cvolume volume;
static bool volume_valid = false;

static bool connected = false;

#define CHECK_DEAD_GOTO(label, warn) do { \
if (!mainloop || \
    !context || pa_context_get_state(context) != PA_CONTEXT_READY || \
    !stream || pa_stream_get_state(stream) != PA_STREAM_READY) { \
        if (warn) \
            AUDDBG("Connection died: %s\n", context ? pa_strerror(pa_context_errno(context)) : "nullptr"); \
        goto label; \
    }  \
} while(0);

#define CHECK_CONNECTED(retval) \
do { \
    if (!connected) return retval; \
} while (0);

static void info_cb(struct pa_context *c, const struct pa_sink_input_info *i, int is_last, void *userdata) {
    assert(c);

    if (!i)
        return;

    volume = i->volume;
    volume_valid = true;
}

static void subscribe_cb(struct pa_context *c, enum pa_subscription_event_type t, uint32_t index, void *userdata) {
    pa_operation *o;

    assert(c);

    if (!stream ||
        index != pa_stream_get_index(stream) ||
        (t != (PA_SUBSCRIPTION_EVENT_SINK_INPUT|PA_SUBSCRIPTION_EVENT_CHANGE) &&
         t != (PA_SUBSCRIPTION_EVENT_SINK_INPUT|PA_SUBSCRIPTION_EVENT_NEW)))
        return;

    if (!(o = pa_context_get_sink_input_info(c, index, info_cb, nullptr))) {
        AUDDBG("pa_context_get_sink_input_info() failed: %s\n", pa_strerror(pa_context_errno(c)));
        return;
    }

    pa_operation_unref(o);
}

static void context_state_cb(pa_context *c, void *userdata) {
    assert(c);

    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_READY:
        case PA_CONTEXT_TERMINATED:
        case PA_CONTEXT_FAILED:
            pa_threaded_mainloop_signal(mainloop, 0);
            break;

        case PA_CONTEXT_UNCONNECTED:
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
            break;
    }
}

static void stream_state_cb(pa_stream *s, void * userdata) {
    assert(s);

    switch (pa_stream_get_state(s)) {

        case PA_STREAM_READY:
        case PA_STREAM_FAILED:
        case PA_STREAM_TERMINATED:
            pa_threaded_mainloop_signal(mainloop, 0);
            break;

        case PA_STREAM_UNCONNECTED:
        case PA_STREAM_CREATING:
            break;
    }
}

static void stream_success_cb(pa_stream *s, int success, void *userdata) {
    assert(s);

    if (userdata)
        *(int*) userdata = success;

    pa_threaded_mainloop_signal(mainloop, 0);
}

static void context_success_cb(pa_context *c, int success, void *userdata) {
    assert(c);

    if (userdata)
        *(int*) userdata = success;

    pa_threaded_mainloop_signal(mainloop, 0);
}

static void stream_request_cb(pa_stream *s, size_t length, void *userdata) {
    assert(s);

    pa_threaded_mainloop_signal(mainloop, 0);
}

static void stream_latency_update_cb(pa_stream *s, void *userdata) {
    assert(s);

    pa_threaded_mainloop_signal(mainloop, 0);
}

StereoVolume PulseOutput::get_volume ()
{
    StereoVolume v = {0, 0};

    if (! connected || ! volume_valid)
        return v;

    pa_threaded_mainloop_lock (mainloop);
    CHECK_DEAD_GOTO (fail, 1);

    if (volume.channels == 2)
    {
        v.left = aud::rescale<int> (volume.values[0], PA_VOLUME_NORM, 100);
        v.right = aud::rescale<int> (volume.values[1], PA_VOLUME_NORM, 100);
    }
    else
        v.left = v.right = aud::rescale<int> (pa_cvolume_avg (& volume), PA_VOLUME_NORM, 100);

fail:
    pa_threaded_mainloop_unlock (mainloop);
    return v;
}

void PulseOutput::set_volume (StereoVolume v)
{
    pa_operation * o;

    if (! connected)
        return;

    pa_threaded_mainloop_lock (mainloop);
    CHECK_DEAD_GOTO (fail, 1);

    if (! volume_valid || volume.channels != 1)
    {
        volume.values[0] = aud::rescale<int> (v.left, 100, PA_VOLUME_NORM);
        volume.values[1] = aud::rescale<int> (v.right, 100, PA_VOLUME_NORM);
        volume.channels = 2;
    }
    else
    {
        volume.values[0] = aud::rescale<int> (aud::max (v.left, v.right), 100, PA_VOLUME_NORM);
        volume.channels = 1;
    }

    volume_valid = true;

    if (! (o = pa_context_set_sink_input_volume (context, pa_stream_get_index
     (stream), & volume, nullptr, nullptr)))
        AUDDBG ("pa_context_set_sink_input_volume() failed: %s\n", pa_strerror
         (pa_context_errno (context)));
    else
        pa_operation_unref(o);

fail:
    pa_threaded_mainloop_unlock (mainloop);
}

void PulseOutput::pause (bool pause)
{
    pa_operation *o = nullptr;
    int success = 0;

    CHECK_CONNECTED();

    pa_threaded_mainloop_lock(mainloop);
    CHECK_DEAD_GOTO(fail, 1);

    if (!(o = pa_stream_cork(stream, pause, stream_success_cb, &success))) {
        AUDDBG("pa_stream_cork() failed: %s\n", pa_strerror(pa_context_errno(context)));
        goto fail;
    }

    while (pa_operation_get_state(o) != PA_OPERATION_DONE) {
        CHECK_DEAD_GOTO(fail, 1);
        pa_threaded_mainloop_wait(mainloop);
    }

    if (!success)
        AUDDBG("pa_stream_cork() failed: %s\n", pa_strerror(pa_context_errno(context)));

fail:

    if (o)
        pa_operation_unref(o);

    pa_threaded_mainloop_unlock(mainloop);
}

int PulseOutput::get_delay ()
{
    int delay = 0;

    CHECK_CONNECTED(0);

    pa_threaded_mainloop_lock(mainloop);

    pa_usec_t usec;
    int neg;
    if (pa_stream_get_latency (stream, & usec, & neg) == PA_OK)
        delay = usec / 1000;

    pa_threaded_mainloop_unlock(mainloop);

    return delay;
}

void PulseOutput::drain ()
{
    pa_operation *o = nullptr;
    int success = 0;

    CHECK_CONNECTED();

    pa_threaded_mainloop_lock(mainloop);
    CHECK_DEAD_GOTO(fail, 0);

    if (!(o = pa_stream_drain(stream, stream_success_cb, &success))) {
        AUDDBG("pa_stream_drain() failed: %s\n", pa_strerror(pa_context_errno(context)));
        goto fail;
    }

    while (pa_operation_get_state(o) != PA_OPERATION_DONE) {
        CHECK_DEAD_GOTO(fail, 1);
        pa_threaded_mainloop_wait(mainloop);
    }

    if (!success)
        AUDDBG("pa_stream_drain() failed: %s\n", pa_strerror(pa_context_errno(context)));

fail:
    if (o)
        pa_operation_unref(o);

    pa_threaded_mainloop_unlock(mainloop);
}

void PulseOutput::flush ()
{
    pa_operation *o = nullptr;
    int success = 0;

    CHECK_CONNECTED();

    pa_threaded_mainloop_lock(mainloop);
    CHECK_DEAD_GOTO(fail, 1);

    if (!(o = pa_stream_flush(stream, stream_success_cb, &success))) {
        AUDDBG("pa_stream_flush() failed: %s\n", pa_strerror(pa_context_errno(context)));
        goto fail;
    }

    while (pa_operation_get_state(o) != PA_OPERATION_DONE) {
        CHECK_DEAD_GOTO(fail, 1);
        pa_threaded_mainloop_wait(mainloop);
    }

    if (!success)
        AUDDBG("pa_stream_flush() failed: %s\n", pa_strerror(pa_context_errno(context)));

fail:
    if (o)
        pa_operation_unref(o);

    pa_threaded_mainloop_unlock(mainloop);
}

void PulseOutput::period_wait ()
{
    pa_operation * o = nullptr;
    int success = 0;

    CHECK_CONNECTED ();

    pa_threaded_mainloop_lock (mainloop);
    CHECK_DEAD_GOTO (fail, 1);

    if (! (o = pa_stream_trigger (stream, stream_success_cb, & success)))
    {
        AUDDBG ("pa_stream_trigger() failed: %s\n", pa_strerror (pa_context_errno (context)));
        goto fail;
    }

    while (pa_operation_get_state (o) != PA_OPERATION_DONE)
    {
        CHECK_DEAD_GOTO (fail, 1);
        pa_threaded_mainloop_wait (mainloop);
    }

    if (! success)
        AUDDBG ("pa_stream_trigger() failed: %s\n", pa_strerror (pa_context_errno (context)));

    while (! pa_stream_writable_size (stream))
    {
        CHECK_DEAD_GOTO (fail, 1);
        pa_threaded_mainloop_wait (mainloop);
    }

fail:
    if (o)
        pa_operation_unref(o);

    pa_threaded_mainloop_unlock (mainloop);
}

int PulseOutput::write_audio (const void * ptr, int length)
{
    CHECK_CONNECTED(0);

    int ret = 0;
    pa_threaded_mainloop_lock(mainloop);
    CHECK_DEAD_GOTO(fail, 1);

    length = aud::min ((size_t) length, pa_stream_writable_size (stream));

    if (pa_stream_write (stream, ptr, length, nullptr, 0, PA_SEEK_RELATIVE) < 0)
    {
        AUDDBG ("pa_stream_write() failed: %s\n", pa_strerror (pa_context_errno (context)));
        goto fail;
    }

    ret = length;

fail:
    pa_threaded_mainloop_unlock(mainloop);
    return ret;
}

void PulseOutput::close_audio ()
{
    connected = false;

    if (mainloop)
        pa_threaded_mainloop_stop(mainloop);

    if (stream) {
        pa_stream_disconnect(stream);
        pa_stream_unref(stream);
        stream = nullptr;
    }

    if (context) {
        pa_context_disconnect(context);
        pa_context_unref(context);
        context = nullptr;
    }

    if (mainloop) {
        pa_threaded_mainloop_free(mainloop);
        mainloop = nullptr;
    }

    volume_valid = false;
}

static pa_sample_format_t to_pulse_format (int aformat)
{
    switch (aformat)
    {
    case FMT_U8:      return PA_SAMPLE_U8;
    case FMT_S16_LE:  return PA_SAMPLE_S16LE;
    case FMT_S16_BE:  return PA_SAMPLE_S16BE;
#ifdef PA_SAMPLE_S24_32LE
    case FMT_S24_LE:  return PA_SAMPLE_S24_32LE;
    case FMT_S24_BE:  return PA_SAMPLE_S24_32BE;
#endif
#ifdef PA_SAMPLE_S32LE
    case FMT_S32_LE:  return PA_SAMPLE_S32LE;
    case FMT_S32_BE:  return PA_SAMPLE_S32BE;
#endif
	case FMT_FLOAT:   return PA_SAMPLE_FLOAT32NE;
    default:          return PA_SAMPLE_INVALID;
    }
}

bool PulseOutput::open_audio (int fmt, int rate, int nch, String & error)
{
    pa_sample_spec ss;

    assert(!mainloop);
    assert(!context);
    assert(!stream);
    assert(!connected);

    ss.format = to_pulse_format (fmt);
    if (ss.format == PA_SAMPLE_INVALID)
        return false;

    ss.rate = rate;
    ss.channels = nch;

    if (!pa_sample_spec_valid(&ss))
        return false;

    if (!(mainloop = pa_threaded_mainloop_new())) {
        AUDERR ("Failed to allocate main loop\n");
        return false;
    }

    pa_threaded_mainloop_lock(mainloop);

    if (!(context = pa_context_new(pa_threaded_mainloop_get_api(mainloop), "Audacious"))) {
        AUDERR ("Failed to allocate context\n");
        goto FAIL1;
    }

    pa_context_set_state_callback(context, context_state_cb, nullptr);
    pa_context_set_subscribe_callback(context, subscribe_cb, nullptr);

    if (pa_context_connect(context, nullptr, (pa_context_flags_t) 0, nullptr) < 0) {
        AUDERR ("Failed to connect to server: %s\n", pa_strerror(pa_context_errno(context)));
        goto FAIL1;
    }

    if (pa_threaded_mainloop_start(mainloop) < 0) {
        AUDERR ("Failed to start main loop\n");
        goto FAIL1;
    }

    /* Wait until the context is ready */
    pa_threaded_mainloop_wait(mainloop);

    if (pa_context_get_state(context) != PA_CONTEXT_READY) {
        AUDERR ("Failed to connect to server: %s\n", pa_strerror(pa_context_errno(context)));
        goto FAIL1;
    }

    if (!(stream = pa_stream_new(context, "Audacious", &ss, nullptr))) {
        AUDERR ("Failed to create stream: %s\n", pa_strerror(pa_context_errno(context)));

FAIL1:
        pa_threaded_mainloop_unlock (mainloop);
        close_audio ();
        return false;
    }

    pa_stream_set_state_callback(stream, stream_state_cb, nullptr);
    pa_stream_set_write_callback(stream, stream_request_cb, nullptr);
    pa_stream_set_latency_update_callback(stream, stream_latency_update_cb, nullptr);

    /* Connect stream with sink and default volume */
    /* Buffer struct */

    int aud_buffer = aud_get_int(nullptr, "output_buffer_size");
    size_t buffer_size = pa_usec_to_bytes(aud_buffer, &ss) * 1000;
    pa_buffer_attr buffer = {(uint32_t) -1, (uint32_t) buffer_size,
     (uint32_t) -1, (uint32_t) -1, (uint32_t) buffer_size};

    pa_operation *o = nullptr;
    int success;

    if (pa_stream_connect_playback (stream, nullptr, & buffer, (pa_stream_flags_t)
     (PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_AUTO_TIMING_UPDATE), nullptr, nullptr) < 0)
    {
        AUDERR ("Failed to connect stream: %s\n", pa_strerror(pa_context_errno(context)));
        goto FAIL2;
    }


    /* Wait until the stream is ready */
    pa_threaded_mainloop_wait(mainloop);

    if (pa_stream_get_state(stream) != PA_STREAM_READY) {
        AUDERR ("Failed to connect stream: %s\n", pa_strerror(pa_context_errno(context)));
        goto FAIL2;
    }

    /* Now subscribe to events */
    if (!(o = pa_context_subscribe(context, PA_SUBSCRIPTION_MASK_SINK_INPUT, context_success_cb, &success))) {
        AUDERR ("pa_context_subscribe() failed: %s\n", pa_strerror(pa_context_errno(context)));
        goto FAIL2;
    }

    success = 0;
    while (pa_operation_get_state(o) != PA_OPERATION_DONE) {
        CHECK_DEAD_GOTO(FAIL2, 1);
        pa_threaded_mainloop_wait(mainloop);
    }

    if (!success) {
        AUDERR ("pa_context_subscribe() failed: %s\n", pa_strerror(pa_context_errno(context)));
        goto FAIL2;
    }

    pa_operation_unref(o);

    /* Now request the initial stream info */
    if (!(o = pa_context_get_sink_input_info(context, pa_stream_get_index(stream), info_cb, nullptr))) {
        AUDERR ("pa_context_get_sink_input_info() failed: %s\n", pa_strerror(pa_context_errno(context)));
        goto FAIL2;
    }

    while (pa_operation_get_state(o) != PA_OPERATION_DONE) {
        CHECK_DEAD_GOTO(FAIL2, 1);
        pa_threaded_mainloop_wait(mainloop);
    }

    if (!volume_valid) {
        AUDERR ("pa_context_get_sink_input_info() failed: %s\n", pa_strerror(pa_context_errno(context)));
        goto FAIL2;
    }
    pa_operation_unref(o);

    connected = true;

    pa_threaded_mainloop_unlock(mainloop);

    return true;

FAIL2:
    if (o)
        pa_operation_unref(o);

    pa_threaded_mainloop_unlock(mainloop);
    close_audio ();
    return false;
}

bool PulseOutput::init ()
{
    String error;
    if (! open_audio (FMT_S16_NE, 44100, 2, error))
        return false;

    close_audio ();
    return true;
}

const char PulseOutput::about[] =
 N_("Audacious PulseAudio Output Plugin\n\n"
    "This program is free software; you can redistribute it and/or modify "
    "it under the terms of the GNU General Public License as published by "
    "the Free Software Foundation; either version 2 of the License, or "
    "(at your option) any later version.\n\n"
    "This program is distributed in the hope that it will be useful, "
    "but WITHOUT ANY WARRANTY; without even the implied warranty of "
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the "
    "GNU General Public License for more details.\n\n"
    "You should have received a copy of the GNU General Public License "
    "along with this program; if not, write to the Free Software "
    "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, "
    "USA.");
