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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <ctype.h>

static constexpr uint8_t PAR_STREAM_SELECT = 0x01;
static constexpr uint8_t PAR_STREAM_INVALID = 0x02;
static constexpr uint8_t PAR_MODE_SCAN = 0;
static constexpr uint8_t PAR_MODE_LIST = 1;
static constexpr uint8_t PAR_MODE_REMUX = 2;
static constexpr uint8_t PAR_MODE_DEMUX = 3;
static constexpr unsigned MPEG_DEMUX_BUFFER = 4096;
static constexpr uint16_t MPEG_END_CODE = 0x01b9;
static constexpr uint16_t MPEG_PACK_START = 0x01ba;
static constexpr uint16_t MPEG_SYSTEM_HEADER = 0x01bb;
static constexpr uint16_t MPEG_PACKET_START = 0x0001;

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

class mpeg_demux_t
{
private:
    void _resetStats();
    int _close = 0;
public:
    FILE *_fp;
    uint64_t ofs;
    uint32_t buf_i;
    uint32_t buf_n;
    uint8_t buf[MPEG_DEMUX_BUFFER];
    mpeg_shdr_t shdr;
    mpeg_packet_t _packet;
    mpeg_pack_t _pack;
    uint32_t shdr_cnt;
    uint32_t pack_cnt;
    uint32_t packet_cnt;
    uint32_t end_cnt;
    uint32_t skip_cnt;
    mpeg_stream_info_t streams[256];
    mpeg_stream_info_t substreams[256];
    FILE *ext;
protected:
    static int mpeg_buf_read(mpeg_buffer_t *buf, mpeg_demux_t *mpeg, unsigned cnt);
    static int returnNull(mpeg_demux_t *mpeg);
    static int packetCheck(mpeg_demux_t *mpeg);
    static int mpeg_copy(mpeg_demux_t *mpeg, FILE *fp, unsigned n);
    static int mpeg_stream_excl(uint8_t sid, uint8_t ssid);
public:
    int (*mpeg_skip)(mpeg_demux_t *mpeg);
    int (*mpeg_pack)(mpeg_demux_t *mpeg);
    int (*mpeg_system_header)(mpeg_demux_t *mpeg);
    int (*mpeg_packet)(mpeg_demux_t *mpeg);
    int (*mpeg_packet_check)(mpeg_demux_t *mpeg);
    int (*mpeg_end)(mpeg_demux_t *mpeg);
public:
    mpeg_demux_t(FILE *fp);
    void close();
};

class MpegDemux : public mpeg_demux_t
{
private:
    static int mpeg_demux_copy_spu(mpeg_demux_t *mpeg, FILE *fp, unsigned cnt);
    static FILE *mpeg_demux_open(mpeg_demux_t *mpeg, unsigned sid, unsigned ssid);
public:
    MpegDemux(FILE *fp);
    static int packet(mpeg_demux_t *);
};

class MpegRemux : public mpeg_demux_t
{
public:
    MpegRemux(FILE *fp);
    static int mpeg_remux_next_fp(mpeg_demux_t *mpeg);
    static int skip(mpeg_demux_t *);
    static int pack(mpeg_demux_t *);
    static int system_header(mpeg_demux_t *);
    static int packet(mpeg_demux_t *);
    static int packet_check(mpeg_demux_t *);
    static int end(mpeg_demux_t *);
};

class MpegScan : public mpeg_demux_t
{
public:
    MpegScan(FILE *fp);
    static int packet(mpeg_demux_t *);
    static int packet_check(mpeg_demux_t *);
    static int end(mpeg_demux_t *);
    int scan(FILE *inp, FILE *out);
};

class MpegList : public mpeg_demux_t
{
public:
    MpegList(FILE *fp);
    static void mpeg_list_print_skip(FILE *fp);
    static int skip(mpeg_demux_t *);
    static int pack(mpeg_demux_t *);
    static int system_header(mpeg_demux_t *);
    static int packet(mpeg_demux_t *);
    static int packet_check(mpeg_demux_t *);
    static int end(mpeg_demux_t *);
};

class Main
{
    char *str_clone(const char *str) const;
    void print_version() const;
    int str_get_streams(const char *str, uint8_t stm[256], unsigned msk) const;
    const char *str_skip_white(const char *str) const;
    int mpeg_scan(FILE *inp, FILE *out);
    int mpeg_demux(FILE *inp, FILE *out);
    int mpeg_remux(FILE *inp, FILE *out);
    int mpeg_list(FILE *inp, FILE *out);
    void mpeg_print_stats(mpeg_demux_t *mpeg, FILE *fp);
public:
    int run(int argc, char **argv);
};

static Options g_opts;
static FILE *fp[512];
static uint8_t par_stream[256];
static uint8_t par_substream[256];
static uint8_t par_stream_map[256];
static uint8_t par_substream_map[256];
static char *par_demux_name = nullptr;
static uint64_t pts1[256];
static uint64_t pts2[256];
static uint64_t skip_ofs = 0;
static uint32_t g_skip_cnt = 0;
static mpeg_buffer_t packet = { nullptr, 0, 0 };
static mpeg_buffer_t shdr = { nullptr, 0, 0 };
static mpeg_buffer_t pack = { nullptr, 0, 0 };
static uint32_t sequence = 0;

static void prt_msg(const char *msg, ...)
{
    va_list va;
    va_start(va, msg);
    vfprintf(stderr, msg, va);
    fflush(stderr);
    va_end(va);
}

int mpeg_demux_t::packetCheck(mpeg_demux_t *mpeg)
{
    if (g_opts.packet_max() > 0 && mpeg->_packet.size > g_opts.packet_max())
        return 1;

    if (par_stream[mpeg->_packet.sid] & PAR_STREAM_INVALID)
        return 1;

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
        ret += fread(tmp, 1, n, mpeg->_fp);

    mpeg->ofs += ret;
    return (ret);
}

int mpeg_demux_t::mpeg_buf_read(mpeg_buffer_t *buf, mpeg_demux_t *mpeg, unsigned cnt)
{
    if (buf->setCnt(cnt))
        return 1;

    buf->cnt = mpegd_read(mpeg, buf->buf, cnt);

    if (buf->cnt != cnt)
        return 1;

    return 0;
}

int mpeg_demux_t::returnNull(mpeg_demux_t *mpeg)
{
    return 0;
}

void mpeg_demux_t::_resetStats()
{
    shdr_cnt = 0;
    pack_cnt = 0;
    packet_cnt = 0;
    end_cnt = 0;
    skip_cnt = 0;
    
    for (uint32_t i = 0; i < 256; i++)
    {
        this->streams[i].packet_cnt = 0;
        this->streams[i].size = 0;
        this->substreams[i].packet_cnt = 0;
        this->substreams[i].size = 0;
    }
}

mpeg_demux_t::mpeg_demux_t(FILE *fp) : _fp(fp)
{
    this->ofs = 0;
    this->buf_i = 0;
    this->buf_n = 0;
    this->ext = NULL;
    this->mpeg_skip = returnNull;
    this->mpeg_system_header = returnNull;
    this->mpeg_packet = returnNull;
    this->mpeg_packet_check = returnNull;
    this->mpeg_pack = returnNull;
    this->mpeg_end = returnNull;
    _resetStats();
}

void mpeg_demux_t::close()
{
    if (_close)
        fclose(_fp);
}

static int mpegd_buffer_fill(mpeg_demux_t *mpeg)
{
    unsigned n;

    if (mpeg->buf_i > 0 && mpeg->buf_n > 0)
        for (unsigned i = 0; i < mpeg->buf_n; i++)
            mpeg->buf[i] = mpeg->buf[mpeg->buf_i + i];

    mpeg->buf_i = 0;
    n = MPEG_DEMUX_BUFFER - mpeg->buf_n;

    if (n > 0)
    {
        size_t r = fread(mpeg->buf + mpeg->buf_n, 1, n, mpeg->_fp);

        if (r < 0)
            return 1;

        mpeg->buf_n += unsigned(r);
    }

    return 0;
}

static int mpegd_need_bits(mpeg_demux_t *mpeg, unsigned n)
{
    n = (n + 7) / 8;

    if (n > mpeg->buf_n)
        mpegd_buffer_fill(mpeg);

    if (n > mpeg->buf_n)
        return 1;

    return 0;
}

static uint32_t mpegd_get_bits(mpeg_demux_t *mpeg, unsigned i, unsigned n)
{
    uint32_t v, m;
    unsigned b_i, b_n;

    if (mpegd_need_bits(mpeg, i + n))
        return 0;

    uint8_t *buf = mpeg->buf + mpeg->buf_i;
    uint32_t r = 0;

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

    return r;
}

static int mpegd_skip(mpeg_demux_t *mpeg, unsigned n)
{
    size_t r;
    mpeg->ofs += n;

    if (n <= mpeg->buf_n)
    {
        mpeg->buf_i += n;
        mpeg->buf_n -= n;
        return 0;
    }

    n -= mpeg->buf_n;
    mpeg->buf_i = 0;
    mpeg->buf_n = 0;

    while (n > 0)
    {
        if (n <= MPEG_DEMUX_BUFFER)
            r = fread(mpeg->buf, 1, n, mpeg->_fp);
        else
            r = fread(mpeg->buf, 1, MPEG_DEMUX_BUFFER, mpeg->_fp);

        if (r <= 0)
            return 1;

        n -= unsigned(r);
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

static int mpegd_seek_header(mpeg_demux_t *mpeg)
{
    uint64_t ofs;

    while (mpegd_get_bits(mpeg, 0, 24) != 1)
    {
        ofs = mpeg->ofs + 1;

        if (mpeg->mpeg_skip(mpeg))
            return 1;

        if (mpegd_set_offset(mpeg, ofs))
            return 1;

        mpeg->skip_cnt += 1;
    }

    return 0;
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
    mpeg->_packet.type = 1;

    if (mpegd_get_bits(mpeg, i, 2) == 0x01)
        i += 16;

    uint32_t val = mpegd_get_bits(mpeg, i, 8);

    if ((val & 0xf0) == 0x20)
    {
        tmp = mpegd_get_bits(mpeg, i + 4, 3);
        tmp = tmp << 15 | mpegd_get_bits(mpeg, i + 8, 15);
        tmp = tmp << 15 | mpegd_get_bits(mpeg, i + 24, 15);
        mpeg->_packet.have_pts = 1;
        mpeg->_packet.pts = tmp;
        i += 40;
    }
    else if ((val & 0xf0) == 0x30)
    {
        tmp = mpegd_get_bits(mpeg, i + 4, 3);
        tmp = tmp << 15 | mpegd_get_bits(mpeg, i + 8, 15);
        tmp = tmp << 15 | mpegd_get_bits(mpeg, i + 24, 15);
        mpeg->_packet.have_pts = 1;
        mpeg->_packet.pts = tmp;
        tmp = mpegd_get_bits(mpeg, i + 44, 3);
        tmp = tmp << 15 | mpegd_get_bits(mpeg, i + 48, 15);
        tmp = tmp << 15 | mpegd_get_bits(mpeg, i + 64, 15);
        mpeg->_packet.have_dts = 1;
        mpeg->_packet.dts = tmp;
        i += 80;
    }
    else if (val == 0x0f)
    {
        i += 8;
    }

    mpeg->_packet.offset = i / 8;
    return 0;
}

static int mpegd_parse_packet2(mpeg_demux_t *mpeg, unsigned i)
{
    uint64_t tmp;
    mpeg->_packet.type = 2;
    uint32_t pts_dts_flag = mpegd_get_bits(mpeg, i + 8, 2);
    uint32_t cnt = mpegd_get_bits(mpeg, i + 16, 8);

    if (pts_dts_flag == 0x02)
    {
        if (mpegd_get_bits (mpeg, i + 24, 4) == 0x02)
        {
            tmp = mpegd_get_bits (mpeg, i + 28, 3);
            tmp = (tmp << 15) | mpegd_get_bits(mpeg, i + 32, 15);
            tmp = (tmp << 15) | mpegd_get_bits(mpeg, i + 48, 15);
            mpeg->_packet.have_pts = 1;
            mpeg->_packet.pts = tmp;
        }
    }
    else if ((pts_dts_flag & 0x03) == 0x03)
    {
        if (mpegd_get_bits(mpeg, i + 24, 4) == 0x03)
        {
            tmp = mpegd_get_bits(mpeg, i + 28, 3);
            tmp = tmp << 15 | mpegd_get_bits(mpeg, i + 32, 15);
            tmp = tmp << 15 | mpegd_get_bits(mpeg, i + 48, 15);
            mpeg->_packet.have_pts = 1;
            mpeg->_packet.pts = tmp;
        }

        if (mpegd_get_bits(mpeg, i + 64, 4) == 0x01)
        {
            tmp = mpegd_get_bits(mpeg, i + 68, 3);
            tmp = tmp << 15 | mpegd_get_bits(mpeg, i + 72, 15);
            tmp = tmp << 15 | mpegd_get_bits(mpeg, i + 88, 15);
            mpeg->_packet.have_dts = 1;
            mpeg->_packet.dts = tmp;
        }
    }

    i += 8 * (cnt + 3);
    mpeg->_packet.offset = i / 8;
    return 0;
}

static int mpegd_parse_packet(mpeg_demux_t *mpeg)
{
    uint64_t ofs;
    mpeg->_packet.type = 0;
    uint32_t sid = mpegd_get_bits(mpeg, 24, 8);
    uint32_t ssid = 0;
    mpeg->_packet.sid = sid;
    mpeg->_packet.ssid = ssid;
    mpeg->_packet.size = mpegd_get_bits(mpeg, 32, 16) + 6;
    mpeg->_packet.offset = 6;
    mpeg->_packet.have_pts = 0;
    mpeg->_packet.pts = 0;
    mpeg->_packet.have_dts = 0;
    mpeg->_packet.dts = 0;
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
        mpeg->_packet.type = 1;
    }

    if (sid == 0xbd)
    {
        ssid = mpegd_get_bits(mpeg, 8 * mpeg->_packet.offset, 8);
        mpeg->_packet.ssid = ssid;
    }

    if (mpeg->mpeg_packet_check(mpeg))
    {
        if (mpegd_skip(mpeg, 1))
            return 1;
    }
    else
    {
        mpeg->packet_cnt += 1;
        mpeg->streams[sid].packet_cnt += 1;
        mpeg->streams[sid].size += mpeg->_packet.size - mpeg->_packet.offset;

        if (sid == 0xbd)
        {
            mpeg->substreams[ssid].packet_cnt += 1;
            mpeg->substreams[ssid].size += mpeg->_packet.size - mpeg->_packet.offset;
        }

        ofs = mpeg->ofs + mpeg->_packet.size;

        if (mpeg->mpeg_packet(mpeg))
            return 1;

        mpegd_set_offset(mpeg, ofs);
    }

    return 0;
}

static int mpegd_parse_pack(mpeg_demux_t *mpeg)
{
    if (mpegd_get_bits(mpeg, 32, 4) == 0x02)
    {
        mpeg->_pack.type = 1;
        mpeg->_pack.scr = mpegd_get_bits(mpeg, 36, 3);
        mpeg->_pack.scr = mpeg->_pack.scr << 15 | mpegd_get_bits(mpeg, 40, 15);
        mpeg->_pack.scr = mpeg->_pack.scr << 15 | mpegd_get_bits(mpeg, 56, 15);
        mpeg->_pack.mux_rate = mpegd_get_bits(mpeg, 73, 22);
        mpeg->_pack.stuff = 0;
        mpeg->_pack.size = 12;
    }
    else if (mpegd_get_bits(mpeg, 32, 2) == 0x01)
    {
        mpeg->_pack.type = 2;
        mpeg->_pack.scr = mpegd_get_bits(mpeg, 34, 3);
        mpeg->_pack.scr = mpeg->_pack.scr << 15 | mpegd_get_bits(mpeg, 38, 15);
        mpeg->_pack.scr = mpeg->_pack.scr << 15 | mpegd_get_bits(mpeg, 54, 15);
        mpeg->_pack.mux_rate = mpegd_get_bits(mpeg, 80, 22);
        mpeg->_pack.stuff = mpegd_get_bits(mpeg, 109, 3);
        mpeg->_pack.size = 14 + mpeg->_pack.stuff;
    }
    else
    {
        mpeg->_pack.type = 0;
        mpeg->_pack.scr = 0;
        mpeg->_pack.mux_rate = 0;
        mpeg->_pack.size = 4;
    }

    uint64_t ofs = mpeg->ofs + mpeg->_pack.size;
    mpeg->pack_cnt += 1;

    if (mpeg->mpeg_pack != NULL)
        if (mpeg->mpeg_pack(mpeg))
            return 1;

    mpegd_set_offset(mpeg, ofs);
    mpegd_seek_header(mpeg);

    if (mpegd_get_bits(mpeg, 0, 32) == MPEG_SYSTEM_HEADER)
    {
        if (mpegd_parse_system_header(mpeg))
            return 1;

        mpegd_seek_header(mpeg);
    }

    while (mpegd_get_bits(mpeg, 0, 24) == MPEG_PACKET_START)
    {
        uint32_t sid = mpegd_get_bits(mpeg, 24, 8);

        if (sid == 0xba || sid == 0xb9 || sid == 0xbb)
            break;
        
        mpegd_parse_packet(mpeg);
        mpegd_seek_header(mpeg);
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

            if (mpeg->mpeg_skip(mpeg))
                return 1;

            if (mpegd_set_offset(mpeg, ofs))
                return 0;

            break;
        }
    }

    return 0;
}

int mpeg_demux_t::mpeg_stream_excl(uint8_t sid, uint8_t ssid)
{
    if ((par_stream[sid] & PAR_STREAM_SELECT) == 0)
        return 1;

    if (sid == 0xbd)
        if ((par_substream[ssid] & PAR_STREAM_SELECT) == 0)
            return 1;

    return 0;
}

int MpegScan::packet(mpeg_demux_t *mpeg)
{
    unsigned sid = mpeg->_packet.sid;
    unsigned ssid = mpeg->_packet.ssid;

    if (mpeg_stream_excl(sid, ssid))
        return 0;

    FILE *fp = mpeg->ext;
    uint64_t ofs = mpeg->ofs;

    if (mpegd_set_offset(mpeg, ofs + mpeg->_packet.size))
    {
        fprintf(fp, "%08" PRIxMAX ": sid=%02x ssid=%02x incomplete packet\n",
            uintmax_t(ofs), sid, ssid);
    }

    if (sid == 0xbd)
    {
        if (mpeg->substreams[ssid].packet_cnt > 1)
        {
            if (g_opts.first_pts() == 0)
                return 0;

            if (!mpeg->_packet.have_pts)
                return 0;

            if (mpeg->_packet.pts >= pts2[ssid])
                return 0;
        }

        if (mpeg->_packet.pts < pts2[ssid])
            pts2[ssid] = mpeg->_packet.pts;
    }
    else
    {
        if (mpeg->streams[sid].packet_cnt > 1)
        {
            if (g_opts.first_pts() == 0)
                return 0;

            if (!mpeg->_packet.have_pts)
                return 0;

            if (mpeg->_packet.pts >= pts1[sid])
                return 0;
        }

        if (mpeg->_packet.pts < pts1[sid])
            pts1[sid] = mpeg->_packet.pts;
    }

    fprintf(fp, "%08" PRIxMAX ": sid=%02x", uintmax_t(ofs), sid);

    if (sid == 0xbd)
        fprintf (fp, "[%02x]", ssid);
    else
        fputs("    ", fp);

    if (mpeg->_packet.type == 1)
    {
        fputs(" MPEG1", fp);
    }
    else if (mpeg->_packet.type == 2)
    {
        fputs(" MPEG2", fp);
    }
    else
    {
        fputs(" UNKWN", fp);
    }

    if (mpeg->_packet.have_pts)
    {
        fprintf(fp, " pts=%" PRIuMAX "[%.4f]",
            uintmax_t(mpeg->_packet.pts),
            double(mpeg->_packet.pts) / 90000.0);
    }

    fputs("\n", fp);
    fflush(fp);
    return 0;
}

int MpegScan::end(mpeg_demux_t *mpeg)
{
    FILE *fp = mpeg->ext;

    if (g_opts.no_end() == 0)
        fprintf(fp, "%08" PRIxMAX ": end code\n", uintmax_t(mpeg->ofs));

    return 0;
}

void Main::mpeg_print_stats(mpeg_demux_t *mpeg, FILE *fp)
{
    unsigned i;

    fprintf(fp,
        "System headers: %u\n"
        "Packs:          %u\n"
        "Packets:        %u\n"
        "End codes:      %u\n"
        "Skipped:        %u bytes\n",
        mpeg->shdr_cnt, mpeg->pack_cnt, mpeg->packet_cnt, mpeg->end_cnt,
        mpeg->skip_cnt);

    for (i = 0; i < 256; i++)
    {
        if (mpeg->streams[i].packet_cnt > 0)
        {
            fprintf(fp,
                "Stream %02x:      "
                "%u packets / %" PRIuMAX " bytes\n",
                i, mpeg->streams[i].packet_cnt,
                uintmax_t(mpeg->streams[i].size));
        }
    }

    for (i = 0; i < 256; i++)
    {
        if (mpeg->substreams[i].packet_cnt > 0)
        {
            fprintf(fp, "Substream %02x:   "
                "%u packets / %" PRIuMAX " bytes\n",
                i, mpeg->substreams[i].packet_cnt,
                uintmax_t(mpeg->substreams[i].size));
        }
    }

    fflush(fp);
}

int MpegScan::scan(FILE *inp, FILE *out)
{
    return mpegd_parse(this);
}

MpegScan::MpegScan(FILE *inp) : mpeg_demux_t(inp)
{
    mpeg_packet = this->packet;
    mpeg_packet_check = this->packetCheck;
    mpeg_end = this->end;
}

int Main::mpeg_scan(FILE *inp, FILE *out)
{
    for (uint32_t i = 0; i < 256; i++)
    {
        pts1[i] = 0xffffffffffffffff;
        pts2[i] = 0xffffffffffffffff;
    }

    MpegScan mpeg(inp);
    mpeg.ext = out;
    int r = mpeg.scan(inp, out);
    mpeg_print_stats(&mpeg, out);
    mpeg.close();
    return r;
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
        return (NULL);

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

int mpeg_demux_t::mpeg_copy(mpeg_demux_t *mpeg, FILE *fp, unsigned n)
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

int MpegDemux::mpeg_demux_copy_spu(mpeg_demux_t *mpeg, FILE *fp, unsigned cnt)
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
            pts = mpeg->_packet.pts;

            for (i = 0; i < 8; i++)
            {
                buf[7 - i] = pts & 0xff;
                pts = pts >> 8;
            }

            if (fwrite(buf, 1, 8, fp) != 8)
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

FILE *MpegDemux::mpeg_demux_open(mpeg_demux_t *mpeg, unsigned sid, unsigned ssid)
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

            free(name);
            return NULL;
        }

        free(name);
    }

    if (sid == 0xbd && g_opts.dvdsub())
    {
        if (fwrite("SPU ", 1, 4, fp) != 4)
        {
            fclose(fp);
            return NULL;
        }
    }

    return fp;
}

int MpegDemux::packet(mpeg_demux_t *mpeg)
{
    int r;
    uint32_t sid = mpeg->_packet.sid;
    uint32_t ssid = mpeg->_packet.ssid;

    if (mpeg_stream_excl(sid, ssid))
        return (0);

    uint32_t cnt = mpeg->_packet.offset;
    uint32_t fpi = sid;

    // select substream in private stream 1 (AC3 audio)
    if (sid == 0xbd)
    {
        fpi = 256 + ssid;
        cnt += 1;

        if (g_opts.dvdac3())
            cnt += 3;
    }

    if (cnt > mpeg->_packet.size)
    {
        prt_msg("demux: AC3 packet too small (sid=%02x size=%u)\n",
            sid, mpeg->_packet.size);

        return 1;
    }

    if (::fp[fpi] == NULL)
    {
        ::fp[fpi] = mpeg_demux_open(mpeg, sid, ssid);

        if (::fp[fpi] == NULL)
            return 1;
    }

    if (cnt > 0)
        mpegd_skip(mpeg, cnt);

    cnt = mpeg->_packet.size - cnt;

    if (sid == 0xbd && g_opts.dvdsub())
        return mpeg_demux_copy_spu(mpeg, ::fp[fpi], cnt);

    r = 0;

    if (mpeg_buf_read(&::packet, mpeg, cnt))
    {
        prt_msg("demux: incomplete packet (sid=%02x size=%u/%u)\n",
            sid, ::packet.cnt, cnt);

        if (g_opts.drop())
        {
            ::packet.clear();
            return 1;
        }

        r = 1;
    }

    if (::packet.write_clear(::fp[fpi]))
        r = 1;

    return r;
}

MpegDemux::MpegDemux(FILE *fp) : mpeg_demux_t(fp)
{
    mpeg_packet = this->packet;
    mpeg_packet_check = this->packetCheck;
}

int Main::mpeg_demux(FILE *inp, FILE *out)
{
    for (unsigned i = 0; i < 512; i++)
        fp[i] = NULL;

    MpegDemux mpeg(inp);

    mpeg.ext = out;
    int r = mpegd_parse(&mpeg);
    mpeg.close();

    for (unsigned i = 0; i < 512; i++)
        if (fp[i] != NULL && fp[i] != out)
            fclose(fp[i]);

    return r;
}

void MpegList::mpeg_list_print_skip(FILE *fp)
{
    if (::g_skip_cnt > 0)
    {
        fprintf(fp, "%08" PRIxMAX ": skip %u\n",
            uintmax_t(::skip_ofs), ::g_skip_cnt);

        ::g_skip_cnt = 0;
    }
}

int MpegList::skip(mpeg_demux_t *mpeg)
{
    if (::g_skip_cnt == 0)
        ::skip_ofs = mpeg->ofs;

    ::g_skip_cnt += 1;
    return 0;
}

int MpegList::system_header(mpeg_demux_t *mpeg)
{
    if (g_opts.no_shdr())
        return 0;

    FILE *fp = mpeg->ext;
    mpeg_list_print_skip(fp);

    fprintf(fp, "%08" PRIxMAX ": system header[%u]: "
        "size=%u fixed=%d csps=%d\n",
        (uintmax_t)mpeg->ofs,
        mpeg->shdr_cnt - 1,
        mpeg->shdr.size, mpeg->shdr.fixed, mpeg->shdr.csps);

    return 0;
}

int MpegList::packet(mpeg_demux_t *mpeg)
{
    if (g_opts.no_packet())
        return 0;

    uint32_t sid = mpeg->_packet.sid;
    uint32_t ssid = mpeg->_packet.ssid;

    if (mpeg_stream_excl(sid, ssid))
        return 0;

    FILE *fp = mpeg->ext;
    mpeg_list_print_skip(fp);

    fprintf(fp, "%08" PRIxMAX ": packet[%u]: sid=%02x",
        uintmax_t(mpeg->ofs), mpeg->streams[sid].packet_cnt - 1, sid);

    if (sid == 0xbd)
        fprintf (fp, "[%02x]", ssid);
    else
        fputs ("    ", fp);

    if (mpeg->_packet.type == 1)
        fputs(" MPEG1", fp);
    else if (mpeg->_packet.type == 2)
        fputs(" MPEG2", fp);
    else
        fputs(" UNKWN", fp);

    fprintf (fp, " size=%u", mpeg->_packet.size);

    if (mpeg->_packet.have_pts || mpeg->_packet.have_dts)
    {
        fprintf(fp, " pts=%" PRIuMAX "[%.4f] dts=%" PRIuMAX "[%.4f]",
            uintmax_t(mpeg->_packet.pts),
            double(mpeg->_packet.pts) / 90000.0,
            uintmax_t(mpeg->_packet.dts),
            double(mpeg->_packet.dts) / 90000.0);
    }

    fputs("\n", fp);
    return 0;
}

int MpegList::pack(mpeg_demux_t *mpeg)
{
    if (g_opts.no_pack())
        return 0;

    FILE *fp = mpeg->ext;
    mpeg_list_print_skip (fp);

    fprintf(fp, "%08" PRIxMAX ": pack[%u]: "
        "type=%u scr=%" PRIuMAX "[%.4f] mux=%u[%.2f] stuff=%u\n",
        uintmax_t(mpeg->ofs), mpeg->pack_cnt - 1, mpeg->_pack.type,
        uintmax_t(mpeg->_pack.scr), double(mpeg->_pack.scr) / 90000.0,
        mpeg->_pack.mux_rate, 50.0 * mpeg->_pack.mux_rate,
        mpeg->_pack.stuff);

    fflush(fp);
    return 0;
}

int MpegList::end(mpeg_demux_t *mpeg)
{
    if (g_opts.no_end())
        return 0;

    FILE *fp = mpeg->ext;
    mpeg_list_print_skip (fp);
    fprintf(fp, "%08" PRIxMAX ": end\n", uintmax_t(mpeg->ofs));
    return 0;
}

MpegList::MpegList(FILE *fp) : mpeg_demux_t(fp)
{
    mpeg_skip = this->skip;
    mpeg_system_header = this->system_header;
    mpeg_pack = this->pack;
    mpeg_packet = this->packet;
    mpeg_packet_check = this->packetCheck;
    mpeg_end = this->end;
}

int Main::mpeg_list(FILE *inp, FILE *out)
{
    MpegList mpeg(inp);
    g_skip_cnt = 0;
    skip_ofs = 0;
    mpeg.ext = out;
    int r = mpegd_parse(&mpeg);
    MpegList::mpeg_list_print_skip(out);
    mpeg_print_stats(&mpeg, out);
    mpeg.close();
    return r;
}

int MpegRemux::mpeg_remux_next_fp(mpeg_demux_t *mpeg)
{
    char *fname;
    FILE *fp = (FILE *)mpeg->ext;

    if (fp != NULL)
    {
        fclose (fp);
        mpeg->ext = NULL;
    }

    fname = mpeg_get_name(par_demux_name, ::sequence);

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

int MpegRemux::skip(mpeg_demux_t *mpeg)
{
    if (g_opts.remux_skipped() == 0)
        return 0;

    if (mpeg_copy(mpeg, mpeg->ext, 1))
        return 1;

    return 0;
}

int MpegRemux::system_header(mpeg_demux_t *mpeg)
{
    if (g_opts.no_shdr() && mpeg->shdr_cnt > 1)
        return 0;

    if (::pack.write_clear(mpeg->ext))
        return 1;

    if (mpeg_buf_read(&::shdr, mpeg, mpeg->shdr.size))
        return 1;

    if (::shdr.write_clear(mpeg->ext))
        return 1;

    return 0;
}

int MpegRemux::packet(mpeg_demux_t *mpeg)
{
    uint32_t sid = mpeg->_packet.sid;
    uint32_t ssid = mpeg->_packet.ssid;

    if (mpeg_stream_excl (sid, ssid))
        return 0;

    int r = 0;

    if (mpeg_buf_read(&::packet, mpeg, mpeg->_packet.size))
    {
        prt_msg("remux: incomplete packet (sid=%02x size=%u/%u)\n",
            sid, ::packet.cnt, mpeg->_packet.size);

        if (g_opts.drop())
        {
            ::packet.clear();
            return 1;
        }

        r = 1;
    }

    if (::packet.cnt >= 4)
    {
        ::packet.buf[3] = par_stream_map[sid];

        if (sid == 0xbd && ::packet.cnt > mpeg->_packet.offset)
            ::packet.buf[mpeg->_packet.offset] = par_substream_map[ssid];
    }

    if (::pack.write_clear(mpeg->ext))
        return 1;

    if (::packet.write_clear(mpeg->ext))
        return 1;

    return r;
}

int MpegRemux::pack(mpeg_demux_t *mpeg)
{
    if (mpeg_buf_read(&::pack, mpeg, mpeg->_pack.size))
        return 1;

    if (g_opts.empty_pack())
        if (::pack.write_clear(mpeg->ext))
            return 1;

    return 0;
}

int MpegRemux::end(mpeg_demux_t *mpeg)
{
    if (g_opts.no_end())
        return 0;

    if (mpeg_copy(mpeg, mpeg->ext, 4))
        return 1;

    if (g_opts.split())
        if (mpeg_remux_next_fp(mpeg))
            return 1;

    return 0;
}

MpegRemux::MpegRemux(FILE *fp) : mpeg_demux_t(fp)
{
    mpeg_skip = this->skip;
    mpeg_system_header = this->system_header;
    mpeg_pack = this->pack;
    mpeg_packet = this->packet;
    mpeg_packet_check = this->packetCheck;
    mpeg_end = this->end;
}

int Main::mpeg_remux(FILE *inp, FILE *out)
{
    MpegRemux mpeg(inp);

    if (g_opts.split())
    {
        mpeg.ext = NULL;
        ::sequence = 0;

        if (mpeg.mpeg_remux_next_fp(&mpeg))
            return 1;
    }
    else
    {
        mpeg.ext = out;
    }

    ::shdr.init();
    ::pack.init();
    ::packet.init();
    int r = ::mpegd_parse(&mpeg);

    if (g_opts.no_end())
    {
        uint8_t buf[4];
        buf[0] = MPEG_END_CODE >> 24 & 0xff;
        buf[1] = MPEG_END_CODE >> 16 & 0xff;
        buf[2] = MPEG_END_CODE >> 8 & 0xff;
        buf[3] = MPEG_END_CODE & 0xff;

        if (fwrite(buf, 1, 4, mpeg.ext) != 4)
            r = 1;
    }

    if (g_opts.split())
    {
        fclose(mpeg.ext);
        mpeg.ext = NULL;
    }

    mpeg.close();
    shdr.free();
    pack.free();
    packet.free();
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

    for (i = 0; i < 256; i++)
    {
        par_stream[i] = 0;
        par_substream[i] = 0;
        par_stream_map[i] = i;
        par_substream_map[i] = i;
    }

    while (true)
    {
        r = g_opts.mpegd_getopt(argc, argv, &optarg);

        if (r == GETOPT_DONE)
            break;

        if (r < 0)
            return 1;

        switch (r)
        {
        case '?':
            return 0;
        case 'a':
            g_opts.dvdac3(1);
            break;
        case 'b':
            if (par_demux_name != NULL)
                free(par_demux_name);
            
            par_demux_name = str_clone(optarg[0]);
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
            g_opts.drop(0);
            break;
        case 'e':
            g_opts.no_end(1);
            break;
        case 'E':
            g_opts.empty_pack(1);
            break;
        case 'F':
            g_opts.first_pts(1);
            break;
        case 'h':
            g_opts.no_shdr(1);
            break;
        case 'i':
            if (strcmp(optarg[0], "-") == 0)
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
            g_opts.no_pack(1);
            break;
        case 'K':
            g_opts.remux_skipped(1);
            break;
        case 'l':
            par_mode = PAR_MODE_LIST;
            break;
        case 'm':
            g_opts.packet_max(unsigned(strtoul(optarg[0], NULL, 0)));
            break;
        case 'p':
            if (str_get_streams(optarg[0], par_substream, PAR_STREAM_SELECT))
            {
                prt_msg("%s: bad substream id (%s)\n", argv[0], optarg[0]);
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
            if (str_get_streams(optarg[0], par_stream, PAR_STREAM_SELECT))
            {
                prt_msg("%s: bad stream id (%s)\n", argv[0], optarg[0]);
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
            g_opts.no_packet(1);
            break;
        case 'u':
            g_opts.dvdsub(1);
            break;
        case 'V':
            print_version();
            return (0);
        case 'x':
            g_opts.split(1);
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

int main(int argc, char **argv)
{
    Main main;
    return main.run(argc, argv);
}


