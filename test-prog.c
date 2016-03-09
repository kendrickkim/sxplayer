/*
 * This file is part of sxplayer.
 *
 * Copyright (c) 2015 Stupeflix
 *
 * sxplayer is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * sxplayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with sxplayer; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <libavutil/avassert.h>
#include <libavutil/common.h>
#include <float.h> // for DBL_MAX

#include "sxplayer.h"

#define BITS_PER_ACTION 4

enum action {
    EOA,                    // end of actions
    ACTION_PREFETCH,        // request a prefetch
    ACTION_FETCH_INFO,      // fetch the media info
    ACTION_START,           // request a frame at t=0
    ACTION_MIDDLE,          // request a few frames in the middle
    ACTION_END,             // request the last frame post end
    NB_ACTIONS
};

static int action_prefetch(struct sxplayer_ctx *s, int opt_test_flags)
{
    return sxplayer_prefetch(s);
}

static int action_fetch_info(struct sxplayer_ctx *s, int opt_test_flags)
{
    struct sxplayer_info info;
    int ret = sxplayer_get_info(s, &info);
    if (ret < 0)
        return ret;
    if (info.width != 16 || info.height != 16)
        return -1;
    return 0;
}

#define N 4
#define SOURCE_FPS 25

#define FLAG_SKIP          (1<<0)
#define FLAG_TRIM_DURATION (1<<1)
#define FLAG_AUDIO         (1<<2)

#define TESTVAL_SKIP           7.12
#define TESTVAL_TRIM_DURATION 53.43

static int check_frame(struct sxplayer_frame *f, double t, int opt_test_flags)
{
    const double skip          = (opt_test_flags & FLAG_SKIP)          ? TESTVAL_SKIP          :  0;
    const double trim_duration = (opt_test_flags & FLAG_TRIM_DURATION) ? TESTVAL_TRIM_DURATION : -1;
    const double playback_time = av_clipd(t, 0, trim_duration < 0 ? DBL_MAX : trim_duration);

    const double frame_ts = f->ts;
    const double estimated_time_from_ts = frame_ts - skip;
    const double diff_ts = fabs(playback_time - estimated_time_from_ts);

    if (!(opt_test_flags & FLAG_AUDIO)) {
        const uint32_t c = *(const uint32_t *)f->data;
        const int r = c >> (N+16) & 0xf;
        const int g = c >> (N+ 8) & 0xf;
        const int b = c >> (N+ 0) & 0xf;
        const int frame_id = r<<(N*2) | g<<N | b;

        const double video_ts = frame_id * 1. / SOURCE_FPS;
        const double estimated_time_from_color = video_ts - skip;
        const double diff_color = fabs(playback_time - estimated_time_from_color);

        if (diff_color > 1./SOURCE_FPS) {
            fprintf(stderr, "requested t=%f (clipped to %f with trim_duration=%f),\n"
                    "got video_ts=%f (frame id #%d), corresponding to t=%f (with skip=%f)\n"
                    "diff_color: %f\n",
                    t, playback_time, trim_duration,
                    video_ts, frame_id, estimated_time_from_color, skip,
                    diff_color);
            return -1;
        }
    }
    if (diff_ts > 1./SOURCE_FPS) {
        fprintf(stderr, "requested t=%f (clipped to %f with trim_duration=%f),\n"
                "got frame_ts=%f, corresponding to t=%f (with skip=%f)\n"
                "diff_ts: %f\n",
                t, playback_time, trim_duration,
                frame_ts, estimated_time_from_ts, skip,
                diff_ts);
        return -1;
    }
    return 0;
}

static int action_start(struct sxplayer_ctx *s, int opt_test_flags)
{
    int ret;
    struct sxplayer_frame *frame = sxplayer_get_frame(s, 0);

    if ((ret = check_frame(frame, 0, opt_test_flags)) < 0)
        return ret;
    sxplayer_release_frame(frame);
    return 0;
}

static int action_middle(struct sxplayer_ctx *s, int opt_test_flags)
{
    int ret;
    struct sxplayer_frame *f0 = sxplayer_get_frame(s, 30.0);
    struct sxplayer_frame *f1 = sxplayer_get_frame(s, 30.1);
    struct sxplayer_frame *f2 = sxplayer_get_frame(s, 30.2);
    struct sxplayer_frame *f3 = sxplayer_get_frame(s, 15.0);
    struct sxplayer_frame *f4 = sxplayer_get_next_frame(s);
    struct sxplayer_frame *f5 = sxplayer_get_next_frame(s);

    if ((ret = check_frame(f0, 30.0,               opt_test_flags)) < 0 ||
        (ret = check_frame(f1, 30.1,               opt_test_flags)) < 0 ||
        (ret = check_frame(f2, 30.2,               opt_test_flags)) < 0 ||
        (ret = check_frame(f3, 15.0,               opt_test_flags)) < 0 ||
        (ret = check_frame(f4, 15.0+1./SOURCE_FPS, opt_test_flags)) < 0 ||
        (ret = check_frame(f5, 15.0+2./SOURCE_FPS, opt_test_flags)) < 0)
        return ret;

    sxplayer_release_frame(f0);
    sxplayer_release_frame(f5);
    sxplayer_release_frame(f1);
    sxplayer_release_frame(f4);
    sxplayer_release_frame(f2);
    sxplayer_release_frame(f3);

    f0 = sxplayer_get_next_frame(s);
    f1 = sxplayer_get_frame(s, 16.0);
    f2 = sxplayer_get_frame(s, 16.001);

    if ((ret = check_frame(f0, 15.0+3./SOURCE_FPS, opt_test_flags)) < 0 ||
        (ret = check_frame(f1, 16.0,               opt_test_flags)) < 0)
        return ret;

    if (f2) {
        fprintf(stderr, "got f2\n");
        return -1;
    }

    sxplayer_release_frame(f1);
    sxplayer_release_frame(f0);

    return 0;
}

static int action_end(struct sxplayer_ctx *s, int opt_test_flags)
{
    struct sxplayer_frame *f;

    f = sxplayer_get_frame(s, 999999.0);
    if (!f)
        return -1;
    sxplayer_release_frame(f);

    f = sxplayer_get_frame(s, 99999.0);
    if (f) {
        sxplayer_release_frame(f);
        return -1;
    }

    return 0;
}

static const struct {
    const char *name;
    int (*func)(struct sxplayer_ctx *s, int opt_test_flags);
} actions_desc[] = {
    [ACTION_PREFETCH]   = {"prefetch",  action_prefetch},
    [ACTION_FETCH_INFO] = {"fetchinfo", action_fetch_info},
    [ACTION_START]      = {"start",     action_start},
    [ACTION_MIDDLE]     = {"middle",    action_middle},
    [ACTION_END]        = {"end",       action_end},
};

#define GET_ACTION(c, id) ((c) >> ((id)*BITS_PER_ACTION) & ((1<<BITS_PER_ACTION)-1))

static void print_comb_name(uint64_t comb, int opt_test_flags)
{
    int i;

    printf(":: test-%s-", (opt_test_flags & FLAG_AUDIO) ? "audio" : "video");
    if (opt_test_flags & FLAG_SKIP)          printf("skip-");
    if (opt_test_flags & FLAG_TRIM_DURATION) printf("trimdur-");
    for (i = 0; i < NB_ACTIONS; i++) {
        const int action = GET_ACTION(comb, i);
        if (!action)
            break;
        printf("%s%s", i ? "-" : "", actions_desc[action].name);
    }
    printf("\n");
}

static int exec_comb(struct sxplayer_ctx *s, uint64_t comb, int opt_test_flags)
{
    int i;

    print_comb_name(comb, opt_test_flags);
    for (i = 0; i < NB_ACTIONS; i++) {
        int ret;
        const int action = GET_ACTION(comb, i);
        if (!action)
            break;
        ret = actions_desc[action].func(s, opt_test_flags);
        if (ret < 0)
            return ret;
    }
    return 0;
}

static int has_dup(uint64_t comb)
{
    int i;
    uint64_t actions = 0;

    for (i = 0; i < NB_ACTIONS; i++) {
        const int action = GET_ACTION(comb, i);
        if (!action)
            break;
        if (actions & (1<<action))
            return 1;
        actions |= 1 << action;
    }
    return 0;
}

static uint64_t get_next_comb(uint64_t comb)
{
    int i = 0, need_inc = 1;
    uint64_t ret = 0;

    for (;;) {
        int action = GET_ACTION(comb, i);
        if (i == NB_ACTIONS)
            return EOA;
        if (!action && !need_inc)
            break;
        if (need_inc) {
            action++;
            if (action == NB_ACTIONS)
                action = 1; // back to first action
            else
                need_inc = 0;
        }
        ret |= action << (i*BITS_PER_ACTION);
        i++;
    }
    if (has_dup(ret))
        return get_next_comb(ret);
    return ret;
}

static int run_tests_all_combs(const char *filename, int opt_test_flags)
{
    int ret;
    uint64_t comb = 0;
    struct sxplayer_ctx *s = NULL;

    for (;;) {
        s = sxplayer_create(filename);
        if (!s)
            return -1;

        sxplayer_set_option(s, "auto_hwaccel", 0);

        if (opt_test_flags & FLAG_SKIP)          sxplayer_set_option(s, "skip",          TESTVAL_SKIP);
        if (opt_test_flags & FLAG_TRIM_DURATION) sxplayer_set_option(s, "trim_duration", TESTVAL_TRIM_DURATION);
        if (opt_test_flags & FLAG_AUDIO)         sxplayer_set_option(s, "avselect",      SXPLAYER_SELECT_AUDIO);

        comb = get_next_comb(comb);
        if (comb == EOA)
            break;
        ret = exec_comb(s, comb, opt_test_flags);
        if (ret < 0) {
            fprintf(stderr, "test failed\n");
            break;
        }
        sxplayer_free(&s);
    }
    sxplayer_free(&s);
    return ret;
}

static int run_image_test(const char *filename)
{
    struct sxplayer_info info;
    struct sxplayer_ctx *s = sxplayer_create(filename);
    struct sxplayer_frame *f;

    if (!s)
        return -1;
    f = sxplayer_get_frame(s, 53.0);
    if (!f) {
        fprintf(stderr, "didn't get an image\n");
        return -1;
    }

    if (sxplayer_get_info(s, &info) < 0) {
        fprintf(stderr, "can not fetch image info\n");
    }
    if (info.width != 480 || info.height != 640) {
        fprintf(stderr, "image isn't the expected size\n");
        return -1;
    }

    sxplayer_free(&s);
    sxplayer_release_frame(f);
    return 0;
}

static int test_next_frame(const char *filename)
{
    int i = 0, ret = 0, r;
    struct sxplayer_ctx *s = sxplayer_create(filename);

    sxplayer_set_option(s, "auto_hwaccel", 0);

    for (r = 0; r < 2; r++) {
        printf("Test: %s run #%d\n", __FUNCTION__, r+1);
        for (;;) {
            struct sxplayer_frame *frame = sxplayer_get_next_frame(s);

            if (!frame) {
                printf("null frame\n");
                break;
            }
            printf("frame #%d / data:%p ts:%f %dx%d lz:%d sfxpixfmt:%d\n",
                   i++, frame->data, frame->ts, frame->width, frame->height,
                   frame->linesize, frame->pix_fmt);

            sxplayer_release_frame(frame);
        }
    }

    sxplayer_free(&s);
    return ret;
}

static const char *filename = "/i/do/not/exist";

static void log_callback(void *arg, int level, const char *fmt, va_list vl)
{
    av_assert0(arg == filename);
    printf("fmt=%s level=%d\n", fmt, level);
}

static int run_notavail_file_test(void)
{
    struct sxplayer_ctx *s = sxplayer_create(filename);

    if (!s)
        return -1;
    sxplayer_set_log_callback(s, (void*)filename, log_callback);
    sxplayer_release_frame(sxplayer_get_frame(s, -1));
    sxplayer_release_frame(sxplayer_get_frame(s, 1.0));
    sxplayer_release_frame(sxplayer_get_frame(s, 3.0));
    sxplayer_free(&s);
    return 0;
}

int main(int ac, char **av)
{
    if (ac != 3) {
        fprintf(stderr, "Usage: %s <media.mkv> <image.jpg>\n", av[0]);
        return -1;
    }

    if (run_image_test(av[2]) < 0)
        return -1;

    if (run_notavail_file_test() < 0)
        return -1;

    if (test_next_frame(av[1]) < 0)
        return -1;

    if (run_tests_all_combs(av[1],                            0) < 0 ||
        run_tests_all_combs(av[1], FLAG_SKIP                   ) < 0 ||
        run_tests_all_combs(av[1],           FLAG_TRIM_DURATION) < 0 ||
        run_tests_all_combs(av[1], FLAG_SKIP|FLAG_TRIM_DURATION) < 0)
        return -1;

#if 0
    if (run_tests_all_combs(av[1], FLAG_AUDIO                             ) < 0 ||
        run_tests_all_combs(av[1], FLAG_AUDIO|FLAG_SKIP                   ) < 0 ||
        run_tests_all_combs(av[1], FLAG_AUDIO|          FLAG_TRIM_DURATION) < 0 ||
        run_tests_all_combs(av[1], FLAG_AUDIO|FLAG_SKIP|FLAG_TRIM_DURATION) < 0)
        return -1;
#endif

    printf("All tests OK\n");

    return 0;
}
