/*****************************************************************************
 * File name:   src/mpegdemux.c                                              *
 * Created:     2003-02-01 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2003-2010 Hampa Hug <hampa@hampa.ch>                     *
 *****************************************************************************/

/*****************************************************************************
 * This program is free software. You can redistribute it and / or modify it *
 * under the terms of the GNU General Public License version 2 as  published *
 * by the Free Software Foundation.                                          *
 *                                                                           *
 * This program is distributed in the hope  that  it  will  be  useful,  but *
 * WITHOUT  ANY   WARRANTY,   without   even   the   implied   warranty   of *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU  General *
 * Public License for more details.                                          *
 *****************************************************************************/

#define MPEGDEMUX_VERSION_MAJ 0
#define MPEGDEMUX_VERSION_MIN 1
#define MPEGDEMUX_VERSION_MIC 4
#define MPEGDEMUX_VERSION_STR "0.1.4"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <inttypes.h>

#define GETOPT_DONE    -1
#define GETOPT_UNKNOWN -2
#define GETOPT_MISSING -3

#define PAR_STREAM_SELECT  0x01
#define PAR_STREAM_INVALID 0x02

#define PAR_MODE_SCAN  0
#define PAR_MODE_LIST  1
#define PAR_MODE_REMUX 2
#define PAR_MODE_DEMUX 3

#define MPEG_DEMUX_BUFFER 4096

static constexpr uint16_t MPEG_END_CODE = 0x01b9;
static constexpr uint16_t MPEG_PACK_START = 0x01ba;
static constexpr uint16_t MPEG_SYSTEM_HEADER = 0x01bb;
static constexpr uint16_t MPEG_PACKET_START = 0x0001;

struct mpeg_buffer_t
{
    uint8_t *buf;
    uint32_t cnt;
    uint32_t max;
};

struct mpeg_stream_info_t
{
    uint32_t packet_cnt;
    uint64_t size;
};

struct mpeg_shdr_t
{
    unsigned size;
    int fixed;
    int csps;
};

struct mpeg_packet_t
{
    unsigned type;
    unsigned sid;
    unsigned ssid;
    unsigned size;
    unsigned offset;
    char have_pts;
    uint64_t pts;
    char have_dts;
    uint64_t dts;
};

struct mpeg_pack_t
{
    unsigned size;
    unsigned type;
    uint64_t scr;
    uint32_t mux_rate;
    uint32_t stuff;
};

struct mpegd_option_t
{
    int16_t name1;
    uint16_t argcnt;
    const char *name2;
    const char *argdesc;
    const char *optdesc;
};

static mpeg_buffer_t packet = { NULL, 0, 0 };
static FILE *fp[512];

static void prt_msg(const char *msg, ...)
{
    va_list va;
    va_start(va, msg);
    vfprintf(stderr, msg, va);
    fflush(stderr);
    va_end(va);
}

static int opt_cmp(const mpegd_option_t *opt1, const mpegd_option_t *opt2)
{
    int c1 = opt1->name1 <= 255 ? tolower(opt1->name1) : opt1->name1;
    int c2 = opt2->name1 <= 255 ? tolower(opt2->name1) : opt2->name1;

    if (c1 < c2)
        return -1;
    
    if (c1 > c2)
        return 1;
    
    if (opt1->name1 < opt2->name1)
        return 1;
    
    if (opt1->name1 > opt2->name1)
        return -1;

    return 0;
}

static unsigned opt_get_width(const mpegd_option_t *opt)
{
    unsigned n;

    if (opt->optdesc == NULL)
        return 0;

    n = 0;

    if (opt->name1 <= 255)
    {
        n += 2;

        if (opt->name2 != NULL)
            n += 2;
    }

    if (opt->name2 != NULL)
        n += 2 + strlen (opt->name2);

    if (opt->argdesc != NULL)
        n += 1 + strlen (opt->argdesc);

    return n;
}

static unsigned opt_max_width(const mpegd_option_t *opt)
{
    uint32_t w = 0;
    uint32_t i = 0;

    while (opt[i].name1 >= 0)
    {
        uint32_t n = opt_get_width(&opt[i]);

        if (n > w)
            w = n;

        i += 1;
    }

    return w;
}

static void sort_options(mpegd_option_t *opt)
{
    mpegd_option_t tmp;

    if (opt[0].name1 < 0)
        return;

    uint32_t i = 1;

    while (opt[i].name1 >= 0)
    {
        if (opt_cmp (&opt[i], &opt[i - 1]) >= 0)
        {
            i += 1;
            continue;
        }

        uint32_t j = i - 1;
        tmp = opt[i];
        opt[i] = opt[j];

        while (j > 0 && opt_cmp(&tmp, &opt[j - 1]) < 0)
        {
            opt[j] = opt[j - 1];
            j -= 1;
        }

        opt[j] = tmp;
        i += 1;
    }
}

static void print_option(const mpegd_option_t *opt, unsigned w)
{
    unsigned n = 0;

    if (opt->name1 <= 255)
    {
        printf ("  -%c", opt->name1);
        n += 2;

        if (opt->name2 != NULL)
        {
            printf (", ");
            n += 2;
        }
    }
    else
    {
        printf ("  ");
    }

    if (opt->name2 != NULL)
    {
        printf ("--%s", opt->name2);
        n += 2 + strlen (opt->name2);
    }

    if (opt->argdesc != NULL)
    {
        printf (" %s", opt->argdesc);
        n += 1 + strlen (opt->argdesc);
    }

    while (n < w)
    {
        fputc (' ', stdout);
        n += 1;
    }

    printf ("%s\n", opt->optdesc);
}

static void
mpegd_getopt_help(const char *tag, const char *usage, mpegd_option_t *opt)
{
    sort_options(opt);
    unsigned w = opt_max_width (opt);

    if (tag != NULL)
        printf("%s\n\n", tag);

    if (usage != NULL)
        printf("%s\n", usage);

    while (opt->name1 >= 0)
    {
        print_option(opt, w + 2);
        opt += 1;
    }
}

static mpegd_option_t *find_option_name1(mpegd_option_t *opt, int name1)
{
    while (opt->name1 >= 0)
    {
        if (opt->name1 == name1)
            return (opt);

        opt += 1;
    }

    return NULL;
}

static mpegd_option_t *
find_option_name2(mpegd_option_t *opt, const char *name2)
{
    while (opt->name1 >= 0)
    {
        if (strcmp (opt->name2, name2) == 0)
            return (opt);

        opt += 1;
    }

    return NULL;
}

static int
mpegd_getopt(int argc, char **argv, char ***optarg, mpegd_option_t *opt)
{
    mpegd_option_t *ret;
    static int atend = 0;
    static int index1 = -1;
    static int index2 = -1;
    static const char *curopt = NULL;

    if (index1 < 0)
    {
        atend = 0;
        index1 = 0;
        index2 = 1;
        curopt = NULL;
    }

    if (atend)
    {
        if (index2 >= argc)
            return (GETOPT_DONE);

        index1 = index2;
        index2 += 1;
        *optarg = argv + index1;
        return 0;
    }

    if ((curopt == NULL) || (*curopt == 0))
    {
        if (index2 >= argc)
            return (GETOPT_DONE);

        index1 = index2;
        index2 += 1;
        curopt = argv[index1];

        if ((curopt[0] != '-') || (curopt[1] == 0))
        {
            *optarg = argv + index1;
            curopt = NULL;
            return 0;
        }

        if (curopt[1] == '-')
        {
            if (curopt[2] == 0)
            {
                atend = 1;

                if (index2 >= argc)
                    return (GETOPT_DONE);

                index1 = index2;
                index2 += 1;
                *optarg = argv + index1;
                return 0;
            }

            ret = find_option_name2 (opt, curopt + 2);

            if (ret == NULL) {
                fprintf (stderr, "%s: unknown option (%s)\n",
                    argv[0], curopt
                );
                return GETOPT_UNKNOWN;
            }

            if ((index2 + ret->argcnt) > argc)
            {
                fprintf (stderr, "%s: missing option argument (%s)\n",
                    argv[0], curopt);

                return GETOPT_MISSING;
            }

            *optarg = argv + index2;
            index2 += ret->argcnt;
            curopt = NULL;
            return ret->name1;
        }

        curopt += 1;
    }

    ret = find_option_name1 (opt, *curopt);

    if (ret == NULL) {
        fprintf (stderr, "%s: unknown option (-%c)\n",
            argv[0], *curopt
        );
        return (GETOPT_UNKNOWN);
    }

    if (index2 + ret->argcnt > argc)
    {
        fprintf(stderr, "%s: missing option argument (-%c)\n",
            argv[0], *curopt);

        return (GETOPT_MISSING);
    }

    *optarg = argv + index2;
    index2 += ret->argcnt;
    curopt += 1;
    return ret->name1;
}

static uint8_t par_stream[256];
static uint8_t par_substream[256];
static uint8_t par_stream_map[256];
static uint8_t par_substream_map[256];
static int par_no_shdr = 0;
static int par_no_pack = 0;
static int par_no_packet = 0;
static int par_no_end = 0;
static int par_empty_pack = 0;
static int par_remux_skipped = 0;
static int par_split = 0;
static int par_drop = 1;
static int par_scan = 0;
static int par_first_pts = 0;
static int par_dvdac3 = 0;
static int par_dvdsub = 0;
static char *par_demux_name = NULL;
static unsigned par_packet_max = 0;
struct mpeg_demux_t;

struct mpeg_demux_t
{
    int close;
    int free;
    FILE *fp;
    uint64_t ofs;
    uint32_t buf_i;
    uint32_t buf_n;
    uint8_t buf[MPEG_DEMUX_BUFFER];
    mpeg_shdr_t shdr;
    mpeg_packet_t packet;
    mpeg_pack_t pack;
    uint32_t shdr_cnt;
    uint32_t pack_cnt;
    uint32_t packet_cnt;
    uint32_t end_cnt;
    uint32_t skip_cnt;
    mpeg_stream_info_t streams[256];
    mpeg_stream_info_t substreams[256];
    FILE *ext;
    int (*mpeg_skip) (struct mpeg_demux_t *mpeg);
    int (*mpeg_pack) (struct mpeg_demux_t *mpeg);
    int (*mpeg_system_header) (struct mpeg_demux_t *mpeg);
    int (*mpeg_packet) (struct mpeg_demux_t *mpeg);
    int (*mpeg_packet_check) (struct mpeg_demux_t *mpeg);
    int (*mpeg_end) (struct mpeg_demux_t *mpeg);
};

static int mpeg_packet_check(mpeg_demux_t *mpeg)
{
    if (par_packet_max > 0 && mpeg->packet.size > par_packet_max)
        return 1;

    if (par_stream[mpeg->packet.sid] & PAR_STREAM_INVALID)
        return 1;

    return 0;
}

static void mpeg_buf_init(mpeg_buffer_t *buf)
{
    buf->buf = NULL;
    buf->max = 0;
    buf->cnt = 0;
}

static void mpeg_buf_free(mpeg_buffer_t *buf)
{
    free(buf->buf);
    buf->buf = NULL;
    buf->cnt = 0;
    buf->max = 0;
}

static void mpeg_buf_clear (mpeg_buffer_t *buf)
{
    buf->cnt = 0;
}

static int mpeg_buf_set_max (mpeg_buffer_t *buf, unsigned max)
{
    if (buf->max == max)
        return 0;

    if (max == 0)
    {
        free(buf->buf);
        buf->max = 0;
        buf->cnt = 0;
        return 0;
    }

    buf->buf = (uint8_t *)realloc(buf->buf, max);

    if (buf->buf == NULL)
    {
        buf->max = 0;
        buf->cnt = 0;
        return 1;
    }

    buf->max = max;

    if (buf->cnt > max)
        buf->cnt = max;

    return 0;
}

static int mpeg_buf_set_cnt(mpeg_buffer_t *buf, unsigned cnt)
{
    if (cnt > buf->max)
        if (mpeg_buf_set_max (buf, cnt))
            return 1;

    buf->cnt = cnt;
    return 0;
}

static unsigned mpegd_read(mpeg_demux_t *mpeg, void *buf, unsigned n)
{
    uint8_t *tmp = (uint8_t *)buf;
    uint32_t i = n < mpeg->buf_n ? n : mpeg->buf_n;
    uint32_t ret = i;

    if (i > 0)
    {
        memcpy (tmp, &mpeg->buf[mpeg->buf_i], i);
        tmp += i;
        mpeg->buf_i += i;
        mpeg->buf_n -= i;
        n -= i;
    }

    if (n > 0)
        ret += fread(tmp, 1, n, mpeg->fp);

    mpeg->ofs += ret;
    return (ret);
}

static int
mpeg_buf_read(mpeg_buffer_t *buf, mpeg_demux_t *mpeg, unsigned cnt)
{
    if (mpeg_buf_set_cnt(buf, cnt))
        return 1;

    buf->cnt = mpegd_read (mpeg, buf->buf, cnt);

    if (buf->cnt != cnt)
        return 1;

    return 0;
}

static int mpeg_buf_write(mpeg_buffer_t *buf, FILE *fp)
{
    if (buf->cnt > 0)
        if (fwrite (buf->buf, 1, buf->cnt, fp) != buf->cnt)
            return 1;

    return 0;
}

static int mpeg_buf_write_clear(mpeg_buffer_t *buf, FILE *fp)
{
    if (buf->cnt > 0)
    {
        if (fwrite (buf->buf, 1, buf->cnt, fp) != buf->cnt)
        {
            buf->cnt = 0;
            return 1;
        }
    }

    buf->cnt = 0;
    return 0;
}

static void mpegd_reset_stats(mpeg_demux_t *mpeg)
{
    mpeg->shdr_cnt = 0;
    mpeg->pack_cnt = 0;
    mpeg->packet_cnt = 0;
    mpeg->end_cnt = 0;
    mpeg->skip_cnt = 0;

    for (uint32_t i = 0; i < 256; i++)
    {
        mpeg->streams[i].packet_cnt = 0;
        mpeg->streams[i].size = 0;
        mpeg->substreams[i].packet_cnt = 0;
        mpeg->substreams[i].size = 0;
    }
}

static mpeg_demux_t *mpegd_open_fp(mpeg_demux_t *mpeg, FILE *fp, int close)
{
    if (mpeg == NULL)
    {
        mpeg = (mpeg_demux_t *)malloc(sizeof (mpeg_demux_t));

        if (mpeg == NULL)
            return NULL;
        
        mpeg->free = 1;
    }
    else
    {
        mpeg->free = 0;
    }

    mpeg->fp = fp;
    mpeg->close = close;
    mpeg->ofs = 0;
    mpeg->buf_i = 0;
    mpeg->buf_n = 0;
    mpeg->ext = NULL;
    mpeg->mpeg_skip = NULL;
    mpeg->mpeg_system_header = NULL;
    mpeg->mpeg_packet = NULL;
    mpeg->mpeg_packet_check = NULL;
    mpeg->mpeg_pack = NULL;
    mpeg->mpeg_end = NULL;
    mpegd_reset_stats (mpeg);
    return mpeg;
}

static void mpegd_close(mpeg_demux_t *mpeg)
{
    if (mpeg->close)
        fclose (mpeg->fp);

    if (mpeg->free)
        free (mpeg);
}

static int mpegd_buffer_fill(mpeg_demux_t *mpeg)
{
    unsigned i, n;
    size_t   r;

    if ((mpeg->buf_i > 0) && (mpeg->buf_n > 0))
        for (i = 0; i < mpeg->buf_n; i++)
            mpeg->buf[i] = mpeg->buf[mpeg->buf_i + i];

    mpeg->buf_i = 0;
    n = MPEG_DEMUX_BUFFER - mpeg->buf_n;

    if (n > 0)
    {
        r = fread (mpeg->buf + mpeg->buf_n, 1, n, mpeg->fp);

        if (r < 0)
            return 1;

        mpeg->buf_n += (unsigned)r;
    }

    return 0;
}

static int mpegd_need_bits(mpeg_demux_t *mpeg, unsigned n)
{
    n = (n + 7) / 8;

    if (n > mpeg->buf_n)
        mpegd_buffer_fill (mpeg);

    if (n > mpeg->buf_n)
        return 1;

    return 0;
}

static uint32_t mpegd_get_bits(mpeg_demux_t *mpeg, unsigned i, unsigned n)
{
    uint32_t r, v, m;
    unsigned b_i, b_n;
    uint8_t *buf;

    if (mpegd_need_bits(mpeg, i + n))
        return 0;

    buf = mpeg->buf + mpeg->buf_i;
    r = 0;

    /* aligned bytes */
    if (((i | n) & 7) == 0)
    {
        i = i / 8;
        n = n / 8;

        while (n > 0)
        {
            r = (r << 8) | buf[i];
            i += 1;
            n -= 1;
        }
        return (r);
    }

    while (n > 0)
    {
        b_n = 8 - (i & 7);

        if (b_n > n)
            b_n = n;

        b_i = 8 - (i & 7) - b_n;
        m = (1 << b_n) - 1;
        v = (buf[i >> 3] >> b_i) & m;
        r = (r << b_n) | v;
        i += b_n;
        n -= b_n;
    }

    return (r);
}

static int mpegd_skip(mpeg_demux_t *mpeg, unsigned n)
{
    size_t r;
    mpeg->ofs += n;

    if (n <= mpeg->buf_n)
    {
        mpeg->buf_i += n;
        mpeg->buf_n -= n;
        return (0);
    }

    n -= mpeg->buf_n;
    mpeg->buf_i = 0;
    mpeg->buf_n = 0;

    while (n > 0)
    {
        if (n <= MPEG_DEMUX_BUFFER)
            r = fread (mpeg->buf, 1, n, mpeg->fp);
        else
            r = fread (mpeg->buf, 1, MPEG_DEMUX_BUFFER, mpeg->fp);

        if (r <= 0)
            return (1);

        n -= (unsigned)r;
    }

    return 0;
}

static int mpegd_set_offset(mpeg_demux_t *mpeg, uint64_t ofs)
{
    if (ofs == mpeg->ofs)
        return 0;

    if (ofs > mpeg->ofs)
        return mpegd_skip(mpeg, uint32_t(ofs - mpeg->ofs));

    return 1;
}

static int mpegd_seek_header (mpeg_demux_t *mpeg)
{
    uint64_t ofs;

    while (mpegd_get_bits (mpeg, 0, 24) != 1)
    {
        ofs = mpeg->ofs + 1;

        if (mpeg->mpeg_skip != NULL)
        {
            if (mpeg->mpeg_skip (mpeg))
                return 1;
        }

        if (mpegd_set_offset (mpeg, ofs))
            return 1;

        mpeg->skip_cnt += 1;
    }

    return (0);
}

static int mpegd_parse_system_header(mpeg_demux_t *mpeg)
{
    mpeg->shdr.size = mpegd_get_bits(mpeg, 32, 16) + 6;
    mpeg->shdr.fixed = mpegd_get_bits(mpeg, 78, 1);
    mpeg->shdr.csps = mpegd_get_bits(mpeg, 79, 1);
    mpeg->shdr_cnt += 1;
    uint64_t ofs = mpeg->ofs + mpeg->shdr.size;

    if (mpeg->mpeg_system_header != NULL)
        if (mpeg->mpeg_system_header(mpeg))
            return 1;

    mpegd_set_offset (mpeg, ofs);
    return 0;
}

static int mpegd_parse_packet1(mpeg_demux_t *mpeg, unsigned i)
{
    uint64_t tmp;
    mpeg->packet.type = 1;

    if (mpegd_get_bits(mpeg, i, 2) == 0x01)
        i += 16;

    uint32_t val = mpegd_get_bits(mpeg, i, 8);

    if ((val & 0xf0) == 0x20)
    {
        tmp = mpegd_get_bits(mpeg, i + 4, 3);
        tmp = tmp << 15 | mpegd_get_bits(mpeg, i + 8, 15);
        tmp = tmp << 15 | mpegd_get_bits(mpeg, i + 24, 15);
        mpeg->packet.have_pts = 1;
        mpeg->packet.pts = tmp;
        i += 40;
    }
    else if ((val & 0xf0) == 0x30)
    {
        tmp = mpegd_get_bits(mpeg, i + 4, 3);
        tmp = tmp << 15 | mpegd_get_bits(mpeg, i + 8, 15);
        tmp = tmp << 15 | mpegd_get_bits(mpeg, i + 24, 15);
        mpeg->packet.have_pts = 1;
        mpeg->packet.pts = tmp;
        tmp = mpegd_get_bits(mpeg, i + 44, 3);
        tmp = tmp << 15 | mpegd_get_bits(mpeg, i + 48, 15);
        tmp = tmp << 15 | mpegd_get_bits(mpeg, i + 64, 15);
        mpeg->packet.have_dts = 1;
        mpeg->packet.dts = tmp;
        i += 80;
    }
    else if (val == 0x0f)
    {
        i += 8;
    }

    mpeg->packet.offset = i / 8;
    return 0;
}

static int mpegd_parse_packet2(mpeg_demux_t *mpeg, unsigned i)
{
    uint64_t tmp;
    mpeg->packet.type = 2;
    uint32_t pts_dts_flag = mpegd_get_bits(mpeg, i + 8, 2);
    uint32_t cnt = mpegd_get_bits(mpeg, i + 16, 8);

    if (pts_dts_flag == 0x02)
    {
        if (mpegd_get_bits (mpeg, i + 24, 4) == 0x02)
        {
            tmp = mpegd_get_bits (mpeg, i + 28, 3);
            tmp = (tmp << 15) | mpegd_get_bits(mpeg, i + 32, 15);
            tmp = (tmp << 15) | mpegd_get_bits(mpeg, i + 48, 15);
            mpeg->packet.have_pts = 1;
            mpeg->packet.pts = tmp;
        }
    }
    else if ((pts_dts_flag & 0x03) == 0x03)
    {
        if (mpegd_get_bits(mpeg, i + 24, 4) == 0x03)
        {
            tmp = mpegd_get_bits(mpeg, i + 28, 3);
            tmp = tmp << 15 | mpegd_get_bits(mpeg, i + 32, 15);
            tmp = tmp << 15 | mpegd_get_bits(mpeg, i + 48, 15);
            mpeg->packet.have_pts = 1;
            mpeg->packet.pts = tmp;
        }

        if (mpegd_get_bits(mpeg, i + 64, 4) == 0x01)
        {
            tmp = mpegd_get_bits(mpeg, i + 68, 3);
            tmp = tmp << 15 | mpegd_get_bits(mpeg, i + 72, 15);
            tmp = tmp << 15 | mpegd_get_bits(mpeg, i + 88, 15);
            mpeg->packet.have_dts = 1;
            mpeg->packet.dts = tmp;
        }
    }

    i += 8 * (cnt + 3);
    mpeg->packet.offset = i / 8;
    return 0;
}

static int mpegd_parse_packet(mpeg_demux_t *mpeg)
{
    uint64_t ofs;
    mpeg->packet.type = 0;
    uint32_t sid = mpegd_get_bits(mpeg, 24, 8);
    uint32_t ssid = 0;
    mpeg->packet.sid = sid;
    mpeg->packet.ssid = ssid;
    mpeg->packet.size = mpegd_get_bits(mpeg, 32, 16) + 6;
    mpeg->packet.offset = 6;
    mpeg->packet.have_pts = 0;
    mpeg->packet.pts = 0;
    mpeg->packet.have_dts = 0;
    mpeg->packet.dts = 0;
    unsigned i = 48;

    if ((sid >= 0xc0 && sid < 0xf0) || sid == 0xbd)
    {
        while (mpegd_get_bits(mpeg, i, 8) == 0xff)
        {
            if (i > (48 + 16 * 8))
                break;
            
            i += 8;
        }

        if (mpegd_get_bits(mpeg, i, 2) == 0x02)
        {
            if (mpegd_parse_packet2(mpeg, i))
                return 1;
        }
        else
        {
            if (mpegd_parse_packet1(mpeg, i))
                return 1;
        }
    }
    else if (sid == 0xbe)
    {
        mpeg->packet.type = 1;
    }

    if (sid == 0xbd)
    {
        ssid = mpegd_get_bits(mpeg, 8 * mpeg->packet.offset, 8);
        mpeg->packet.ssid = ssid;
    }

    if (mpeg->mpeg_packet_check != NULL && mpeg->mpeg_packet_check(mpeg))
    {
        if (mpegd_skip(mpeg, 1))
            return 1;
    }
    else
    {
        mpeg->packet_cnt += 1;
        mpeg->streams[sid].packet_cnt += 1;
        mpeg->streams[sid].size += mpeg->packet.size - mpeg->packet.offset;

        if (sid == 0xbd)
        {
            mpeg->substreams[ssid].packet_cnt += 1;
            mpeg->substreams[ssid].size += mpeg->packet.size - mpeg->packet.offset;
        }

        ofs = mpeg->ofs + mpeg->packet.size;

        if (mpeg->mpeg_packet != NULL)
            if (mpeg->mpeg_packet (mpeg))
                return 1;

        mpegd_set_offset (mpeg, ofs);
    }

    return 0;
}

static int mpegd_parse_pack(mpeg_demux_t *mpeg)
{
    if (mpegd_get_bits(mpeg, 32, 4) == 0x02)
    {
        mpeg->pack.type = 1;
        mpeg->pack.scr = mpegd_get_bits(mpeg, 36, 3);
        mpeg->pack.scr = mpeg->pack.scr << 15 | mpegd_get_bits(mpeg, 40, 15);
        mpeg->pack.scr = mpeg->pack.scr << 15 | mpegd_get_bits(mpeg, 56, 15);
        mpeg->pack.mux_rate = mpegd_get_bits(mpeg, 73, 22);
        mpeg->pack.stuff = 0;
        mpeg->pack.size = 12;
    }
    else if (mpegd_get_bits (mpeg, 32, 2) == 0x01)
    {
        mpeg->pack.type = 2;
        mpeg->pack.scr = mpegd_get_bits (mpeg, 34, 3);
        mpeg->pack.scr = (mpeg->pack.scr << 15) | mpegd_get_bits (mpeg, 38, 15);
        mpeg->pack.scr = (mpeg->pack.scr << 15) | mpegd_get_bits (mpeg, 54, 15);
        mpeg->pack.mux_rate = mpegd_get_bits (mpeg, 80, 22);
        mpeg->pack.stuff = mpegd_get_bits (mpeg, 109, 3);
        mpeg->pack.size = 14 + mpeg->pack.stuff;
    }
    else
    {
        mpeg->pack.type = 0;
        mpeg->pack.scr = 0;
        mpeg->pack.mux_rate = 0;
        mpeg->pack.size = 4;
    }

    uint64_t ofs = mpeg->ofs + mpeg->pack.size;
    mpeg->pack_cnt += 1;

    if (mpeg->mpeg_pack != NULL)
        if (mpeg->mpeg_pack (mpeg))
            return 1;

    mpegd_set_offset (mpeg, ofs);
    mpegd_seek_header (mpeg);

    if (mpegd_get_bits(mpeg, 0, 32) == MPEG_SYSTEM_HEADER)
    {
        if (mpegd_parse_system_header(mpeg))
            return 1;

        mpegd_seek_header (mpeg);
    }

    while (mpegd_get_bits(mpeg, 0, 24) == MPEG_PACKET_START)
    {
        uint32_t sid = mpegd_get_bits(mpeg, 24, 8);

        if ((sid == 0xba) || (sid == 0xb9) || (sid == 0xbb))
            break;
        
        mpegd_parse_packet (mpeg);
        mpegd_seek_header (mpeg);
    }

    return 0;
}

static int mpegd_parse(mpeg_demux_t *mpeg)
{
    uint64_t ofs;

    while (true)
    {
        if (mpegd_seek_header(mpeg))
            return 0;

        switch (mpegd_get_bits(mpeg, 0, 32))
        {
        case MPEG_PACK_START:
            if (mpegd_parse_pack(mpeg))
                return 1;
            
            break;
        case MPEG_END_CODE:
            mpeg->end_cnt += 1;
            ofs = mpeg->ofs + 4;

            if (mpeg->mpeg_end != NULL)
                if (mpeg->mpeg_end(mpeg))
                    return 1;

            if (mpegd_set_offset(mpeg, ofs))
                return 1;
            
            break;
        default:
            ofs = mpeg->ofs + 1;

            if (mpeg->mpeg_skip != NULL)
                if (mpeg->mpeg_skip(mpeg))
                    return 1;

            if (mpegd_set_offset (mpeg, ofs))
                return 0;

            break;
        }
    }

    return 0;
}

static uint64_t pts1[256];
static uint64_t pts2[256];

static int mpeg_scan_system_header(mpeg_demux_t *mpeg)
{
    return 0;
}

static int mpeg_stream_excl(uint8_t sid, uint8_t ssid)
{
    if ((par_stream[sid] & PAR_STREAM_SELECT) == 0)
        return 1;

    if (sid == 0xbd)
        if ((par_substream[ssid] & PAR_STREAM_SELECT) == 0)
            return 1;

    return 0;
}

static int mpeg_scan_packet(mpeg_demux_t *mpeg)
{
    int skip;
    unsigned sid = mpeg->packet.sid;
    unsigned ssid = mpeg->packet.ssid;

    if (mpeg_stream_excl (sid, ssid))
        return 0;

    FILE *fp = mpeg->ext;
    uint64_t ofs = mpeg->ofs;

    if (mpegd_set_offset (mpeg, ofs + mpeg->packet.size))
    {
        fprintf (fp, "%08" PRIxMAX ": sid=%02x ssid=%02x incomplete packet\n",
            uintmax_t(ofs), sid, ssid);
    }

    skip = 0;

    if (sid == 0xbd)
    {
        if (mpeg->substreams[ssid].packet_cnt > 1)
        {
            if (!par_first_pts)
                return 0;

            if (!mpeg->packet.have_pts)
                return 0;

            if (mpeg->packet.pts >= pts2[ssid])
                return 0;
        }

        if (mpeg->packet.pts < pts2[ssid])
            pts2[ssid] = mpeg->packet.pts;
    }
    else
    {
        if (mpeg->streams[sid].packet_cnt > 1)
        {
            if (!par_first_pts)
                return 0;

            if (!mpeg->packet.have_pts)
                return 0;

            if (mpeg->packet.pts >= pts1[sid])
                return 0;
        }

        if (mpeg->packet.pts < pts1[sid])
            pts1[sid] = mpeg->packet.pts;
    }

    fprintf(fp, "%08" PRIxMAX ": sid=%02x", uintmax_t(ofs), sid);

    if (sid == 0xbd)
        fprintf (fp, "[%02x]", ssid);
    else
        fputs ("    ", fp);

    if (mpeg->packet.type == 1)
    {
        fputs (" MPEG1", fp);
    }
    else if (mpeg->packet.type == 2)
    {
        fputs (" MPEG2", fp);
    }
    else
    {
        fputs (" UNKWN", fp);
    }

    if (mpeg->packet.have_pts)
    {
        fprintf(fp, " pts=%" PRIuMAX "[%.4f]",
            uintmax_t(mpeg->packet.pts),
            double(mpeg->packet.pts) / 90000.0);
    }

    fputs ("\n", fp);
    fflush (fp);
    return (0);
}

static int mpeg_scan_pack(mpeg_demux_t *mpeg)
{
    return (0);
}

static int mpeg_scan_end(mpeg_demux_t *mpeg)
{
    FILE *fp = mpeg->ext;

    if (!par_no_end)
    {
        fprintf(fp, "%08" PRIxMAX ": end code\n",
            uintmax_t(mpeg->ofs));
    }

    return 0;
}

static void mpeg_print_stats(mpeg_demux_t *mpeg, FILE *fp)
{
    unsigned i;

    fprintf(fp,
        "System headers: %lu\n"
        "Packs:          %lu\n"
        "Packets:        %lu\n"
        "End codes:      %lu\n"
        "Skipped:        %lu bytes\n",
        mpeg->shdr_cnt, mpeg->pack_cnt, mpeg->packet_cnt, mpeg->end_cnt,
        mpeg->skip_cnt);

    for (i = 0; i < 256; i++)
    {
        if (mpeg->streams[i].packet_cnt > 0)
        {
            fprintf(fp,
                "Stream %02x:      "
                "%lu packets / %" PRIuMAX " bytes\n",
                i, mpeg->streams[i].packet_cnt,
                uintmax_t(mpeg->streams[i].size));
        }
    }

    for (i = 0; i < 256; i++)
    {
        if (mpeg->substreams[i].packet_cnt > 0)
        {
            fprintf(fp, "Substream %02x:   "
                "%lu packets / %" PRIuMAX " bytes\n",
                i, mpeg->substreams[i].packet_cnt,
                uintmax_t(mpeg->substreams[i].size));
        }
    }

    fflush (fp);
}

static int mpeg_scan(FILE *inp, FILE *out)
{
    int r;
    unsigned i;
    mpeg_demux_t *mpeg;

    for (i = 0; i < 256; i++)
    {
        pts1[i] = 0xffffffffffffffff;
        pts2[i] = 0xffffffffffffffff;
    }

    mpeg = mpegd_open_fp(NULL, inp, 0);

    if (mpeg == NULL)
        return 1;

    mpeg->ext = out;
    mpeg->mpeg_system_header = &mpeg_scan_system_header;
    mpeg->mpeg_pack = &mpeg_scan_pack;
    mpeg->mpeg_packet = &mpeg_scan_packet;
    mpeg->mpeg_packet_check = &mpeg_packet_check;
    mpeg->mpeg_end = &mpeg_scan_end;
    r = mpegd_parse(mpeg);
    mpeg_print_stats(mpeg, out);
    mpegd_close(mpeg);
    return r;
}

static unsigned par_mode = PAR_MODE_SCAN;
static FILE     *par_inp = NULL;
static FILE     *par_out = NULL;

static mpegd_option_t opts[] = {
 { '?', 0, "help", NULL, "Print usage information" },
 { 'a', 0, "ac3", NULL, "Assume DVD AC3 headers in private streams" },
 { 'b', 1, "base-name", "name", "Set the base name for demuxed streams" },
 { 'c', 0, "scan", NULL, "Scan the stream [default]" },
 { 'd', 0, "demux", NULL, "Demultiplex streams" },
 { 'D', 0, "no-drop", NULL, "Don't drop incomplete packets" },
 { 'e', 0, "no-end", NULL, "Don't list end codes [no]" },
 { 'E', 0, "empty-packs", NULL, "Remux empty packs [no]" },
 { 'F', 0, "first-pts", NULL, "Print packet with lowest PTS [no]" },
 { 'h', 0, "no-system-headers", NULL, "Don't list system headers" },
 { 'i', 1, "invalid", "id", "Select invalid streams [none]" },
 { 'k', 0, "no-packs", NULL, "Don't list packs" },
 { 'K', 0, "remux-skipped", NULL, "Copy skipped bytes when remuxing [no]" },
 { 'l', 0, "list", NULL, "List the stream contents" },
 { 'm', 1, "packet-max-size", "int", "Set the maximum packet size [0]" },
 { 'p', 1, "substream", "id", "Select substreams [none]" },
 { 'P', 2, "substream-map", "id1 id2", "Remap substream id1 to id2" },
 { 'r', 0, "remux", NULL, "Copy modified input to output" },
 { 's', 1, "stream", "id", "Select streams [none]" },
 { 'S', 2, "stream-map", "id1 id2", "Remap stream id1 to id2" },
 { 't', 0, "no-packets", NULL, "Don't list packets" },
 { 'u', 0, "spu", NULL, "Assume DVD subtitles in private streams" },
 { 'V', 0, "version", NULL, "Print version information" },
 { 'x', 0, "split", NULL, "Split sequences while remuxing [no]" },
 {  -1, 0, NULL, NULL, NULL }
};

static void print_help()
{
    mpegd_getopt_help("mpegdemux: demultiplex MPEG1/2 system streams",
        "usage: mpegdemux [options] [input [output]]", opts);

    fflush (stdout);
}

static void print_version()
{
    fputs("mpegdemux version " MPEGDEMUX_VERSION_STR
        "\n\n"
        "Copyright (C) 2003-2010 Hampa Hug <hampa@hampa.ch>\n",
        stdout);
}

static char *str_clone(const char *str)
{
    char *ret = (char *)malloc(strlen(str) + 1);

    if (ret == NULL)
        return (NULL);

    strcpy(ret, str);
    return ret;
}

static const char *str_skip_white(const char *str)
{
    while ((*str == ' ') || (*str == '\t'))
        str += 1;

    return (str);
}

static int str_get_streams(const char *str, uint8_t stm[256], unsigned msk)
{
    unsigned i;
    char     *tmp;
    unsigned stm1, stm2;
    int incl = 1;

    while (*str != 0)
    {
        str = str_skip_white(str);

        if (*str == '+')
        {
            str += 1;
            incl = 1;
        }
        else if (*str == '-')
        {
            str += 1;
            incl = 0;
        }
        else {
            incl = 1;
        }

        if (strncmp (str, "all", 3) == 0)
        {
            str += 3;
            stm1 = 0;
            stm2 = 255;
        }
        else if (strncmp (str, "none", 4) == 0)
        {
            str += 4;
            stm1 = 0;
            stm2 = 255;
            incl = !incl;
        }
        else
        {
            stm1 = uint32_t(strtoul(str, &tmp, 0));
            if (tmp == str) {
                return (1);
            }

            str = tmp;

            if (*str == '-')
            {
                str += 1;
                stm2 = (unsigned) strtoul (str, &tmp, 0);

                if (tmp == str)
                    return 1;

                str = tmp;
            }
            else
            {
                stm2 = stm1;
            }
        }

        if (incl)
        {
            for (i = stm1; i <= stm2; i++)
                stm[i] |= msk;
        }
        else
        {
            for (i = stm1; i <= stm2; i++)
                stm[i] &= ~msk;
        }

        str = str_skip_white (str);

        if (*str == '/')
            str += 1;
    }

    return 0;
}

static char *mpeg_get_name(const char *base, unsigned sid)
{
    if (base == NULL)
        base = "stream_##.dat";

    uint32_t n = 0;

    while (base[n] != 0)
        n += 1;

    n += 1;
    char *ret = (char *)malloc(n);

    if (ret == NULL)
        return (NULL);

    while (n > 0)
    {
        n -= 1;
        ret[n] = base[n];

        if (ret[n] == '#')
        {
            uint32_t dig = sid % 16;
            sid = sid / 16;
            ret[n] = dig < 10 ? '0' + dig : 'a' + dig - 10;
        }
    }

    return ret;
}

static int mpeg_copy(mpeg_demux_t *mpeg, FILE *fp, unsigned n)
{
    uint8_t buf[4096];

    while (n > 0)
    {
        uint32_t i = n < 4096 ? n : 4096;
        uint32_t j = mpegd_read(mpeg, buf, i);

        if (j > 0)
            if (fwrite(buf, 1, j, fp) != j)
                return 1;

        if (i != j)
            return 1;

        n -= i;
    }

    return 0;
}

static int mpeg_demux_copy_spu(mpeg_demux_t *mpeg, FILE *fp, unsigned cnt)
{
    static unsigned spucnt = 0;
    static int half = 0;
    unsigned i, n;
    uint8_t buf[8];
    uint64_t pts;

    if (half)
    {
        mpegd_read(mpeg, buf, 1);

        if (fwrite(buf, 1, 1, fp) != 1)
            return 1;

        spucnt = (spucnt << 8) + buf[0];
        half = 0;
        spucnt -= 2;
        cnt -= 1;
    }

    while (cnt > 0)
    {
        if (spucnt == 0)
        {
            pts = mpeg->packet.pts;

            for (i = 0; i < 8; i++)
            {
                buf[7 - i] = pts & 0xff;
                pts = pts >> 8;
            }

            if (fwrite (buf, 1, 8, fp) != 8)
                return 1;

            if (cnt == 1)
            {
                mpegd_read(mpeg, buf, 1);

                if (fwrite(buf, 1, 1, fp) != 1)
                    return 1;

                spucnt = buf[0];
                half = 1;
                return 0;
            }

            mpegd_read(mpeg, buf, 2);

            if (fwrite(buf, 1, 2, fp) != 2)
                return 1;

            spucnt = (buf[0] << 8) + buf[1];

            if (spucnt < 2)
                return (1);

            spucnt -= 2;
            cnt -= 2;
        }

        n = cnt < spucnt ? cnt : spucnt;
        mpeg_copy(mpeg, fp, n);
        cnt -= n;
        spucnt -= n;
    }

    return 0;
}

static FILE *
mpeg_demux_open(mpeg_demux_t *mpeg, unsigned sid, unsigned ssid)
{
    FILE *fp;

    if (par_demux_name == NULL)
    {
        fp = mpeg->ext;
    }
    else
    {
        uint32_t seq = sid == 0xbd ? (sid << 8) + ssid : sid;
        char *name = mpeg_get_name(par_demux_name, seq);
        fp = fopen(name, "wb");

        if (fp == NULL)
        {
            prt_msg("can't open stream file (%s)\n", name);

            if (sid == 0xbd)
                par_substream[ssid] &= ~PAR_STREAM_SELECT;
            else
                par_stream[sid] &= ~PAR_STREAM_SELECT;

            free (name);
            return NULL;
        }

        free(name);
    }

    if (sid == 0xbd && par_dvdsub)
    {
        if (fwrite("SPU ", 1, 4, fp) != 4)
        {
            fclose(fp);
            return NULL;
        }
    }

    return fp;
}

static int mpeg_demux_system_header(mpeg_demux_t *mpeg)
{
    return 0;
}

static int mpeg_demux_packet(mpeg_demux_t *mpeg)
{
    unsigned fpi;
    unsigned cnt;
    int      r;
    uint32_t sid = mpeg->packet.sid;
    uint32_t ssid = mpeg->packet.ssid;

    if (mpeg_stream_excl (sid, ssid))
        return (0);

    cnt = mpeg->packet.offset;
    fpi = sid;

    // select substream in private stream 1 (AC3 audio)
    if (sid == 0xbd)
    {
        fpi = 256 + ssid;
        cnt += 1;

        if (par_dvdac3)
            cnt += 3;
    }

    if (cnt > mpeg->packet.size)
    {
        prt_msg("demux: AC3 packet too small (sid=%02x size=%u)\n",
            sid, mpeg->packet.size);

        return 1;
    }

    if (fp[fpi] == NULL)
    {
        fp[fpi] = mpeg_demux_open(mpeg, sid, ssid);

        if (fp[fpi] == NULL)
            return 1;
    }

    if (cnt > 0)
        mpegd_skip(mpeg, cnt);

    cnt = mpeg->packet.size - cnt;

    if ((sid == 0xbd) && par_dvdsub)
        return mpeg_demux_copy_spu(mpeg, fp[fpi], cnt);

    r = 0;

    if (mpeg_buf_read (&packet, mpeg, cnt))
    {
        prt_msg("demux: incomplete packet (sid=%02x size=%u/%u)\n",
            sid, packet.cnt, cnt);

        if (par_drop) {
            mpeg_buf_clear (&packet);
            return (1);
        }

        r = 1;
    }

    if (mpeg_buf_write_clear (&packet, fp[fpi]))
        r = 1;

    return r;
}

static int mpeg_demux_pack (mpeg_demux_t *mpeg)
{
    return (0);
}

static int mpeg_demux_end (mpeg_demux_t *mpeg)
{
    return (0);
}

static int mpeg_demux(FILE *inp, FILE *out)
{
    for (unsigned i = 0; i < 512; i++)
        fp[i] = NULL;

    mpeg_demux_t *mpeg = mpegd_open_fp(NULL, inp, 0);

    if (mpeg == NULL)
        return 1;

    mpeg->mpeg_system_header = &mpeg_demux_system_header;
    mpeg->mpeg_pack = &mpeg_demux_pack;
    mpeg->mpeg_packet = &mpeg_demux_packet;
    mpeg->mpeg_packet_check = &mpeg_packet_check;
    mpeg->mpeg_end = &mpeg_demux_end;
    mpeg->ext = out;
    int r = mpegd_parse(mpeg);
    mpegd_close(mpeg);

    for (unsigned i = 0; i < 512; i++)
        if ((fp[i] != NULL) && (fp[i] != out))
            fclose(fp[i]);

    return r;
}

static uint64_t skip_ofs = 0;
static uint32_t skip_cnt = 0;

static void mpeg_list_print_skip(FILE *fp)
{
    if (skip_cnt > 0)
    {
        fprintf(fp, "%08" PRIxMAX ": skip %lu\n",
            (uintmax_t)skip_ofs, skip_cnt);

        skip_cnt = 0;
    }
}

static int mpeg_list_skip(mpeg_demux_t *mpeg)
{
    if (skip_cnt == 0)
        skip_ofs = mpeg->ofs;

    skip_cnt += 1;
    return 0;
}

static int mpeg_list_system_header(mpeg_demux_t *mpeg)
{
    if (par_no_shdr)
        return 0;

    FILE *fp = mpeg->ext;
    mpeg_list_print_skip(fp);

    fprintf(fp, "%08" PRIxMAX ": system header[%lu]: "
        "size=%u fixed=%d csps=%d\n",
        (uintmax_t)mpeg->ofs,
        mpeg->shdr_cnt - 1,
        mpeg->shdr.size, mpeg->shdr.fixed, mpeg->shdr.csps);

    return (0);
}

static int mpeg_list_packet (mpeg_demux_t *mpeg)
{
    if (par_no_packet)
        return 0;

    uint32_t sid = mpeg->packet.sid;
    uint32_t ssid = mpeg->packet.ssid;

    if (mpeg_stream_excl(sid, ssid))
        return 0;

    FILE *fp = mpeg->ext;
    mpeg_list_print_skip (fp);

    fprintf(fp, "%08" PRIxMAX ": packet[%lu]: sid=%02x",
        (uintmax_t)mpeg->ofs, mpeg->streams[sid].packet_cnt - 1, sid);

    if (sid == 0xbd)
        fprintf (fp, "[%02x]", ssid);
    else
        fputs ("    ", fp);

    if (mpeg->packet.type == 1)
        fputs (" MPEG1", fp);
    else if (mpeg->packet.type == 2)
        fputs (" MPEG2", fp);
    else
        fputs (" UNKWN", fp);

    fprintf (fp, " size=%u", mpeg->packet.size);

    if (mpeg->packet.have_pts || mpeg->packet.have_dts)
    {
        fprintf(fp, " pts=%" PRIuMAX "[%.4f] dts=%" PRIuMAX "[%.4f]",
            uintmax_t(mpeg->packet.pts),
            double(mpeg->packet.pts) / 90000.0,
            uintmax_t(mpeg->packet.dts),
            double(mpeg->packet.dts) / 90000.0);
    }

    fputs("\n", fp);
    return 0;
}

static int mpeg_list_pack(mpeg_demux_t *mpeg)
{
    if (par_no_pack)
        return 0;

    FILE *fp = mpeg->ext;
    mpeg_list_print_skip (fp);

    fprintf(fp, "%08" PRIxMAX ": pack[%lu]: "
        "type=%u scr=%" PRIuMAX "[%.4f] mux=%lu[%.2f] stuff=%u\n",
        uintmax_t(mpeg->ofs), mpeg->pack_cnt - 1, mpeg->pack.type,
        uintmax_t(mpeg->pack.scr), double(mpeg->pack.scr) / 90000.0,
        mpeg->pack.mux_rate, 50.0 * mpeg->pack.mux_rate,
        mpeg->pack.stuff);

    fflush(fp);
    return 0;
}

static int mpeg_list_end(mpeg_demux_t *mpeg)
{
    if (par_no_end)
        return 0;

    FILE *fp = mpeg->ext;
    mpeg_list_print_skip (fp);
    fprintf (fp, "%08" PRIxMAX ": end\n", (uintmax_t) mpeg->ofs);
    return 0;
}

static int mpeg_list(FILE *inp, FILE *out)
{
    mpeg_demux_t *mpeg;
    mpeg = mpegd_open_fp(NULL, inp, 0);

    if (mpeg == NULL)
        return 1;

    skip_cnt = 0;
    skip_ofs = 0;
    mpeg->ext = out;
    mpeg->mpeg_skip = &mpeg_list_skip;
    mpeg->mpeg_system_header = &mpeg_list_system_header;
    mpeg->mpeg_pack = &mpeg_list_pack;
    mpeg->mpeg_packet = &mpeg_list_packet;
    mpeg->mpeg_packet_check = &mpeg_packet_check;
    mpeg->mpeg_end = &mpeg_list_end;
    int r = mpegd_parse(mpeg);
    mpeg_list_print_skip(out);
    mpeg_print_stats(mpeg, out);
    mpegd_close(mpeg);
    return r;
}

static mpeg_buffer_t shdr = { NULL, 0, 0 };
static mpeg_buffer_t pack = { NULL, 0, 0 };
static unsigned sequence = 0;

static int mpeg_remux_next_fp(mpeg_demux_t *mpeg)
{
    char *fname;
    FILE *fp = (FILE *)mpeg->ext;

    if (fp != NULL) {
        fclose (fp);
        mpeg->ext = NULL;
    }

    fname = mpeg_get_name(par_demux_name, sequence);

    if (fname == NULL)
        return (1);

    sequence += 1;
    fp = fopen(fname, "wb");
    free(fname);

    if (fp == NULL)
        return (1);

    mpeg->ext = fp;
    return (0);
}

static int mpeg_remux_skip(mpeg_demux_t *mpeg)
{
    if (par_remux_skipped == 0)
        return 0;

    if (mpeg_copy(mpeg, mpeg->ext, 1))
        return 1;

    return 0;
}

static int mpeg_remux_system_header(mpeg_demux_t *mpeg)
{
    if (par_no_shdr && (mpeg->shdr_cnt > 1))
        return 0;

    if (mpeg_buf_write_clear(&pack, mpeg->ext))
        return 1;

    if (mpeg_buf_read (&shdr, mpeg, mpeg->shdr.size))
        return 1;

    if (mpeg_buf_write_clear(&shdr, mpeg->ext))
        return 1;

    return 0;
}

static int mpeg_remux_packet(mpeg_demux_t *mpeg)
{
    uint32_t sid = mpeg->packet.sid;
    uint32_t ssid = mpeg->packet.ssid;

    if (mpeg_stream_excl (sid, ssid))
        return 0;

    int r = 0;

    if (mpeg_buf_read (&packet, mpeg, mpeg->packet.size))
    {
        prt_msg("remux: incomplete packet (sid=%02x size=%u/%u)\n",
            sid, packet.cnt, mpeg->packet.size);

        if (par_drop)
        {
            mpeg_buf_clear (&packet);
            return 1;
        }

        r = 1;
    }

    if (packet.cnt >= 4)
    {
        packet.buf[3] = par_stream_map[sid];

        if ((sid == 0xbd) && (packet.cnt > mpeg->packet.offset))
            packet.buf[mpeg->packet.offset] = par_substream_map[ssid];
    }

    if (mpeg_buf_write_clear(&pack, mpeg->ext))
        return 1;

    if (mpeg_buf_write_clear(&packet, mpeg->ext))
        return 1;

    return r;
}

static int mpeg_remux_pack(mpeg_demux_t *mpeg)
{
    if (mpeg_buf_read(&pack, mpeg, mpeg->pack.size))
        return 1;

    if (par_empty_pack)
        if (mpeg_buf_write_clear(&pack, mpeg->ext))
            return 1;

    return 0;
}

static int mpeg_remux_end(mpeg_demux_t *mpeg)
{
    if (par_no_end)
        return 0;

    if (mpeg_copy(mpeg, mpeg->ext, 4))
        return 1;

    if (par_split)
        if (mpeg_remux_next_fp(mpeg))
            return 1;

    return 0;
}

static int mpeg_remux(FILE *inp, FILE *out)
{
    mpeg_demux_t *mpeg;
    mpeg = mpegd_open_fp(NULL, inp, 0);

    if (mpeg == NULL)
        return 1;

    if (par_split)
    {
        mpeg->ext = NULL;
        sequence = 0;

        if (mpeg_remux_next_fp(mpeg))
            return 1;
    }
    else
    {
        mpeg->ext = out;
    }

    mpeg->mpeg_skip = mpeg_remux_skip;
    mpeg->mpeg_system_header = mpeg_remux_system_header;
    mpeg->mpeg_pack = mpeg_remux_pack;
    mpeg->mpeg_packet = mpeg_remux_packet;
    mpeg->mpeg_packet_check = mpeg_packet_check;
    mpeg->mpeg_end = mpeg_remux_end;
    mpeg_buf_init(&shdr);
    mpeg_buf_init(&pack);
    mpeg_buf_init(&packet);
    int r = mpegd_parse(mpeg);

    if (par_no_end)
    {
        uint8_t buf[4];
        buf[0] = MPEG_END_CODE >> 24 & 0xff;
        buf[1] = MPEG_END_CODE >> 16 & 0xff;
        buf[2] = MPEG_END_CODE >> 8 & 0xff;
        buf[3] = MPEG_END_CODE & 0xff;

        if (fwrite(buf, 1, 4, mpeg->ext) != 4)
            r = 1;
    }

    if (par_split)
    {
        fclose((FILE *)mpeg->ext);
        mpeg->ext = NULL;
    }

    mpegd_close(mpeg);
    mpeg_buf_free(&shdr);
    mpeg_buf_free(&pack);
    mpeg_buf_free(&packet);
    return r;
}

int main(int argc, char **argv)
{
    unsigned i;
    int      r;
    unsigned id1, id2;
    char     **optarg;

    for (i = 0; i < 256; i++)
    {
        par_stream[i] = 0;
        par_substream[i] = 0;
        par_stream_map[i] = i;
        par_substream_map[i] = i;
    }

    while (true)
    {
        r = mpegd_getopt(argc, argv, &optarg, opts);

        if (r == GETOPT_DONE)
            break;

        if (r < 0)
            return 1;

        switch (r)
        {
        case '?':
            print_help();
            return 0;
        case 'a':
            par_dvdac3 = 1;
            break;
        case 'b':
            if (par_demux_name != NULL)
                free(par_demux_name);
            
            par_demux_name = str_clone (optarg[0]);
            break;
        case 'c':
            par_mode = PAR_MODE_SCAN;

            for (i = 0; i < 256; i++)
            {
                par_stream[i] |= PAR_STREAM_SELECT;
                par_substream[i] |= PAR_STREAM_SELECT;
            }
            break;
        case 'd':
            par_mode = PAR_MODE_DEMUX;
            break;
        case 'D':
            par_drop = 0;
            break;
        case 'e':
            par_no_end = 1;
            break;
        case 'E':
            par_empty_pack = 1;
            break;
        case 'F':
            par_first_pts = 1;
            break;
        case 'h':
            par_no_shdr = 1;
            break;
        case 'i':
            if (strcmp (optarg[0], "-") == 0)
            {
                for (i = 0; i < 256; i++)
                {
                    if (par_stream[i] & PAR_STREAM_SELECT)
                        par_stream[i] &= ~PAR_STREAM_INVALID;
                    else
                        par_stream[i] |= PAR_STREAM_INVALID;
                }
            }
            else
            {
                if (str_get_streams(optarg[0], par_stream, PAR_STREAM_INVALID))
                {
                    prt_msg("%s: bad stream id (%s)\n", argv[0], optarg[0]);
                    return 1;
                }
            }
            break;
        case 'k':
            par_no_pack = 1;
            break;
        case 'K':
            par_remux_skipped = 1;
            break;
        case 'l':
            par_mode = PAR_MODE_LIST;
            break;
        case 'm':
            par_packet_max = unsigned(strtoul(optarg[0], NULL, 0));
            break;
        case 'p':
            if (str_get_streams (optarg[0], par_substream, PAR_STREAM_SELECT))
            {
                prt_msg("%s: bad substream id (%s)\n", argv[0], optarg[0]);
                return 1;
            }
            break;
        case 'P':
            id1 = unsigned(strtoul(optarg[0], NULL, 0));
            id2 = unsigned(strtoul(optarg[1], NULL, 0));
            par_substream_map[id1 & 0xff] = id2 & 0xff;
            break;
        case 'r':
            par_mode = PAR_MODE_REMUX;
            break;
        case 's':
            if (str_get_streams (optarg[0], par_stream, PAR_STREAM_SELECT))
            {
                prt_msg("%s: bad stream id (%s)\n", argv[0], optarg[0]);
                return 1;
            }
            break;
        case 'S':
            id1 = unsigned(strtoul(optarg[0], NULL, 0));
            id2 = unsigned(strtoul(optarg[1], NULL, 0));
            par_stream_map[id1 & 0xff] = id2 & 0xff;
            break;
        case 't':
            par_no_packet = 1;
            break;
        case 'u':
            par_dvdsub = 1;
            break;
        case 'V':
            print_version();
            return (0);
        case 'x':
            par_split = 1;
            break;
        case 0:
            if (par_inp == NULL)
            {
                if (strcmp (optarg[0], "-") == 0)
                    par_inp = stdin;
                else
                    par_inp = fopen (optarg[0], "rb");

                if (par_inp == NULL)
                {
                    prt_msg("%s: can't open input file (%s)\n",
                            argv[0], optarg[0]);

                    return (1);
                }
            }
            else if (par_out == NULL)
            {
                if (strcmp (optarg[0], "-") == 0)
                    par_out = stdout;
                else
                    par_out = fopen (optarg[0], "wb");

                if (par_out == NULL)
                {
                    prt_msg("%s: can't open output file (%s)\n",
                                argv[0], optarg[0]);

                    return 1;
                }
            }
            else
            {
                prt_msg("%s: too many files (%s)\n", argv[0], optarg[0]);
                return 1;
            }
            break;
        default:
            return 1;
        }
    }

    if (par_inp == NULL)
        par_inp = stdin;

    if (par_out == NULL)
        par_out = stdout;

    switch (par_mode) {
    case PAR_MODE_SCAN:
        r = mpeg_scan(par_inp, par_out);
        break;
    case PAR_MODE_LIST:
        r = mpeg_list(par_inp, par_out);
        break;
    case PAR_MODE_REMUX:
        r = mpeg_remux(par_inp, par_out);
        break;
    case PAR_MODE_DEMUX:
        r = mpeg_demux(par_inp, par_out);
        break;
    default:
        r = 1;
        break;
    }

    if (r)
        return 1;

    return 0;
}


