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

#include "options.h"
#include "buffer.h"
#include "common.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <ctype.h>
#include <string>
using namespace std;

class Main
{
private:
    char *str_clone(const char *str) const;
    void print_version() const;
    int str_get_streams(const char *str, uint8_t stm[256], unsigned msk) const;
    const char *str_skip_white(const char *str) const;
public:
    int run(int argc, char **argv);
};

static FILE *fp[512];
static uint8_t g_par_stream[256];
static uint8_t g_par_substream[256];
static uint8_t par_stream_map[256];
static uint8_t par_substream_map[256];
static uint64_t g_skip_ofs = 0;
static uint32_t g_skip_cnt = 0;
static mpeg_buffer_t g_packet = { nullptr, 0, 0 };
static mpeg_buffer_t shdr = { nullptr, 0, 0 };
static mpeg_buffer_t pack = { nullptr, 0, 0 };

//virtual method
int mpeg_demux_t::packet_check(mpeg_demux_t *)
{
    if (_options->packet_max() > 0 && _packet.size > _options->packet_max())
        return 1;

    if (g_par_stream[_packet.sid] & PAR_STREAM_INVALID)
        return 1;

    return 0;
}

int mpeg_demux_t::mpeg_stream_excl(uint8_t sid, uint8_t ssid)
{
    if ((g_par_stream[sid] & PAR_STREAM_SELECT) == 0)
        return 1;

    if (sid == 0xbd)
        if ((g_par_substream[ssid] & PAR_STREAM_SELECT) == 0)
            return 1;

    return 0;
}

void Main::print_version() const
{
    fputs("mpegdemux version 0.0.0\n\n" 
        "Copyright (C) 2003-2010 Hampa Hug <hampa@hampa.ch>\n",
        stdout);
}

char *Main::str_clone(const char *str) const
{
    char *ret = (char *)malloc(strlen(str) + 1);

    if (ret == NULL)
        return NULL;

    strcpy(ret, str);
    return ret;
}

const char *Main::str_skip_white(const char *str) const
{
    while (*str == ' ' || *str == '\t')
        str += 1;

    return str;
}

int
Main::str_get_streams(const char *str, uint8_t stm[256], unsigned msk) const
{
    unsigned i;
    char *tmp;
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
        else
        {
            incl = 1;
        }

        if (strncmp(str, "all", 3) == 0)
        {
            str += 3;
            stm1 = 0;
            stm2 = 255;
        }
        else if (strncmp(str, "none", 4) == 0)
        {
            str += 4;
            stm1 = 0;
            stm2 = 255;
            incl = !incl;
        }
        else
        {
            stm1 = uint32_t(strtoul(str, &tmp, 0));

            if (tmp == str)
                return 1;

            str = tmp;

            if (*str == '-')
            {
                str += 1;
                stm2 = unsigned(strtoul(str, &tmp, 0));

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

char *mpeg_demux_t::mpeg_get_name(const char *base, unsigned sid)
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

FILE *MpegDemux::mpeg_demux_open(mpeg_demux_t *, unsigned sid, unsigned ssid)
{
    FILE *fp;

    if (_options->_demux_name == NULL)
    {
        fp = _ext;
    }
    else
    {
        uint32_t seq = sid == 0xbd ? (sid << 8) + ssid : sid;
        char *name = mpeg_get_name(_options->_demux_name, seq);
        fp = fopen(name, "wb");

        if (fp == NULL)
        {
            fprintf(stderr, "can't open stream file (%s)\n", name);

            if (sid == 0xbd)
                g_par_substream[ssid] &= ~PAR_STREAM_SELECT;
            else
                g_par_stream[sid] &= ~PAR_STREAM_SELECT;

            free(name);
            return NULL;
        }

        free(name);
    }

    if (sid == 0xbd && _options->dvdsub())
    {
        if (fwrite("SPU ", 1, 4, fp) != 4)
        {
            fclose(fp);
            return NULL;
        }
    }

    return fp;
}

int MpegDemux::packet()
{
    uint32_t sid = _packet.sid;
    uint32_t ssid = _packet.ssid;

    if (mpeg_stream_excl(sid, ssid))
        return 0;

    uint32_t cnt = _packet.offset;
    uint32_t fpi = sid;

    // select substream in private stream 1 (AC3 audio)
    if (sid == 0xbd)
    {
        fpi = 256 + ssid;
        cnt += 1;

        if (_options->dvdac3())
            cnt += 3;
    }

    if (cnt > _packet.size)
    {
        fprintf(stderr, "demux: AC3 packet too small (sid=%02x size=%u)\n",
            sid, _packet.size);

        return 1;
    }

    if (::fp[fpi] == NULL)
    {
        ::fp[fpi] = mpeg_demux_open(this, sid, ssid);

        if (::fp[fpi] == NULL)
            return 1;
    }

    if (cnt > 0)
        mpegd_skip(this, cnt);

    cnt = _packet.size - cnt;

    if (sid == 0xbd && _options->dvdsub())
        return mpeg_demux_copy_spu(this, ::fp[fpi], cnt);

    int r = 0;

    if (mpeg_buf_read(&::g_packet, cnt))
    {
        fprintf(stderr, "demux: incomplete packet (sid=%02x size=%u/%u)\n",
            sid, ::g_packet.cnt, cnt);

        if (_options->drop())
        {
            ::g_packet.clear();
            return 1;
        }

        r = 1;
    }

    if (::g_packet.write_clear(::fp[fpi]))
        r = 1;

    return r;
}

int MpegDemux::demux(FILE *inp, FILE *out)
{
    for (unsigned i = 0; i < 512; i++)
        ::fp[i] = NULL;

    _ext = out;
    int r = parse(this);
    close();

    for (unsigned i = 0; i < 512; i++)
        if (::fp[i] != NULL && ::fp[i] != out)
            fclose(fp[i]);

    return r;
}

void MpegList::mpeg_list_print_skip(FILE *fp)
{
    if (::g_skip_cnt > 0)
    {
        fprintf(fp, "%08" PRIxMAX ": skip %u\n",
            uintmax_t(::g_skip_ofs), ::g_skip_cnt);

        ::g_skip_cnt = 0;
    }
}

int MpegList::skip()
{
    if (::g_skip_cnt == 0)
        ::g_skip_ofs = _ofs;

    ::g_skip_cnt += 1;
    return 0;
}

int MpegList::list(FILE *inp, FILE *out)
{
    g_skip_cnt = 0;
    g_skip_ofs = 0;
    _ext = out;
    int r = parse(this);
    mpeg_list_print_skip(out);
    mpeg_print_stats(this, out);
    close();
    return r;
}

int MpegRemux::system_header()
{
    if (_options->no_shdr() && _shdr_cnt > 1)
        return 0;

    if (::pack.write_clear(_ext))
        return 1;

    if (mpeg_buf_read(&::shdr, _shdr.size))
        return 1;

    if (::shdr.write_clear(_ext))
        return 1;

    return 0;
}

int MpegRemux::packet()
{
    uint32_t sid = _packet.sid;
    uint32_t ssid = _packet.ssid;

    if (mpeg_stream_excl(sid, ssid))
        return 0;

    int r = 0;

    if (mpeg_buf_read(&::g_packet, _packet.size))
    {
        fprintf(stderr, "remux: incomplete packet (sid=%02x size=%u/%u)\n",
            sid, ::g_packet.cnt, _packet.size);

        if (_options->drop())
        {
            ::g_packet.clear();
            return 1;
        }

        r = 1;
    }

    if (::g_packet.cnt >= 4)
    {
        ::g_packet.buf[3] = par_stream_map[sid];

        if (sid == 0xbd && ::g_packet.cnt > _packet.offset)
            ::g_packet.buf[_packet.offset] = par_substream_map[ssid];
    }

    if (::pack.write_clear(_ext))
        return 1;

    if (::g_packet.write_clear(_ext))
        return 1;

    return r;
}

int MpegRemux::pack()
{
    if (mpeg_buf_read(&::pack, _pack.size))
        return 1;

    if (_options->empty_pack())
        if (::pack.write_clear(_ext))
            return 1;

    return 0;
}

int MpegRemux::remux(FILE *inp, FILE *out)
{
    if (_options->split())
    {
        _ext = NULL;
        _sequence = 0;

        if (mpeg_remux_next_fp(this))
            return 1;
    }
    else
    {
        _ext = out;
    }

    ::shdr.init();
    ::pack.init();
    ::g_packet.init();
    int r = parse(this);

    if (_options->no_end())
    {
        uint8_t buf[4];
        buf[0] = MPEG_END_CODE >> 24 & 0xff;
        buf[1] = MPEG_END_CODE >> 16 & 0xff;
        buf[2] = MPEG_END_CODE >> 8 & 0xff;
        buf[3] = MPEG_END_CODE & 0xff;

        if (fwrite(buf, 1, 4, _ext) != 4)
            r = 1;
    }

    if (_options->split())
    {
        fclose(_ext);
        _ext = NULL;
    }

    close();
    ::shdr.free();
    ::pack.free();
    g_packet.free();
    return r;
}

int Main::run(int argc, char **argv)
{
    FILE *par_inp = NULL;
    FILE *par_out = NULL;
    uint8_t par_mode = PAR_MODE_SCAN;
    unsigned i;
    int r;
    char **optarg;
    Options opts;

    for (i = 0; i < 256; i++)
    {
        g_par_stream[i] = 0;
        g_par_substream[i] = 0;
        par_stream_map[i] = i;
        par_substream_map[i] = i;
    }

    while (true)
    {
        r = opts.mpegd_getopt(argc, argv, &optarg);

        if (r == GETOPT_DONE)
            break;

        if (r < 0)
            return 1;

        switch (r)
        {
        case '?':
            return 0;
        case 'a':
            opts.dvdac3(1);
            break;
        case 'b':
            if (opts._demux_name != NULL)
                free(opts._demux_name);
            
            opts._demux_name = str_clone(optarg[0]);
            break;
        case 'c':
            par_mode = PAR_MODE_SCAN;

            for (i = 0; i < 256; i++)
            {
                g_par_stream[i] |= PAR_STREAM_SELECT;
                g_par_substream[i] |= PAR_STREAM_SELECT;
            }
            break;
        case 'd':
            par_mode = PAR_MODE_DEMUX;
            break;
        case 'D':
            opts.drop(0);
            break;
        case 'e':
            opts.no_end(1);
            break;
        case 'E':
            opts.empty_pack(1);
            break;
        case 'F':
            opts.first_pts(1);
            break;
        case 'h':
            opts.no_shdr(1);
            break;
        case 'i':
            if (strcmp(optarg[0], "-") == 0)
            {
                for (i = 0; i < 256; i++)
                {
                    if (g_par_stream[i] & PAR_STREAM_SELECT)
                        g_par_stream[i] &= ~PAR_STREAM_INVALID;
                    else
                        g_par_stream[i] |= PAR_STREAM_INVALID;
                }
            }
            else
            {
                if (str_get_streams(optarg[0], g_par_stream, PAR_STREAM_INVALID))
                {
                    fprintf(stderr, "%s: bad stream id (%s)\n", argv[0], optarg[0]);
                    return 1;
                }
            }
            break;
        case 'k':
            opts.no_pack(1);
            break;
        case 'K':
            opts.remux_skipped(1);
            break;
        case 'l':
            par_mode = PAR_MODE_LIST;
            break;
        case 'm':
            opts.packet_max(unsigned(strtoul(optarg[0], NULL, 0)));
            break;
        case 'p':
            if (str_get_streams(optarg[0], g_par_substream, PAR_STREAM_SELECT))
            {
                fprintf(stderr, "%s: bad substream id (%s)\n", argv[0], optarg[0]);
                return 1;
            }
            break;
        case 'P':
        {
            unsigned id1 = unsigned(strtoul(optarg[0], NULL, 0));
            unsigned id2 = unsigned(strtoul(optarg[1], NULL, 0));
            par_substream_map[id1 & 0xff] = id2 & 0xff;
        }
            break;
        case 'r':
            par_mode = PAR_MODE_REMUX;
            break;
        case 's':
            if (str_get_streams(optarg[0], g_par_stream, PAR_STREAM_SELECT))
            {
                fprintf(stderr, "%s: bad stream id (%s)\n", argv[0], optarg[0]);
                return 1;
            }
            break;
        case 'S':
        {
            unsigned id1 = unsigned(strtoul(optarg[0], NULL, 0));
            unsigned id2 = unsigned(strtoul(optarg[1], NULL, 0));
            par_stream_map[id1 & 0xff] = id2 & 0xff;
        }
            break;
        case 't':
            opts.no_packet(1);
            break;
        case 'u':
            opts.dvdsub(1);
            break;
        case 'V':
            print_version();
            return (0);
        case 'x':
            opts.split(1);
            break;
        case 0:
            if (par_inp == NULL)
            {
                if (strcmp(optarg[0], "-") == 0)
                    par_inp = stdin;
                else
                    par_inp = fopen(optarg[0], "rb");

                if (par_inp == NULL)
                {
                    fprintf(stderr, "%s: can't open input file (%s)\n",
                            argv[0], optarg[0]);

                    return 1;
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
                    fprintf(stderr, "%s: can't open output file (%s)\n",
                                argv[0], optarg[0]);

                    return 1;
                }
            }
            else
            {
                fprintf(stderr, "%s: too many files (%s)\n", argv[0], optarg[0]);
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

    switch (par_mode)
    {
    case PAR_MODE_SCAN:
    {
        MpegScan mpeg(par_inp, &opts);
        r = mpeg.scan(par_inp, par_out);
    }
        break;
    case PAR_MODE_LIST:
    {
        MpegList mpeg(par_inp, &opts);
        r = mpeg.list(par_inp, par_out);
    }
        break;
    case PAR_MODE_REMUX:
    {
        MpegRemux mpeg(par_inp, &opts);
        r = mpeg.remux(par_inp, par_out);
    }
        break;
    case PAR_MODE_DEMUX:
    {
        MpegDemux mpeg(par_inp, &opts);
        r = mpeg.demux(par_inp, par_out);
    }
        break;
    default:
        r = 1;
        break;
    }

    if (r)
        return 1;

    return 0;
}

int main(int argc, char **argv)
{
    Main main;
    return main.run(argc, argv);
}


