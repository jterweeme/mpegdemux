#include "common.h"
#include "buffer.h"
#include "options.h"
#include <cstring>
#include <cstdlib>

//virtual method
int mpeg_demux_t::pack()
{
    return 0;
}

//virtual method
int mpeg_demux_t::packet()
{
    return 0;
}

//virtual method
int mpeg_demux_t::system_header()
{
    return 0;
}

//virtual method
int mpeg_demux_t::skip()
{
    return 0;
}

//virtual method
int mpeg_demux_t::end()
{
    return 0;
}

mpeg_demux_t::mpeg_demux_t(FILE *fp, Options *options) : _options(options), _fp(fp)
{
    _ext = NULL;
    _resetStats();
}

MpegDemux::MpegDemux(FILE *fp, Options *options) : mpeg_demux_t(fp, options)
{
}

MpegList::MpegList(FILE *fp, Options *options) : mpeg_demux_t(fp, options)
{
}

MpegRemux::MpegRemux(FILE *fp, Options *options) : mpeg_demux_t(fp, options)
{
}

MpegScan::MpegScan(FILE *inp, Options *options) : mpeg_demux_t(inp, options)
{
}

int MpegList::system_header()
{
    if (_options->no_shdr())
        return 0;

    mpeg_list_print_skip(_ext);

    fprintf(_ext, "%08" PRIxMAX ": system header[%u]: "
        "size=%u fixed=%d csps=%d\n", uintmax_t(_ofs),
        _shdr_cnt - 1, _shdr.size, _shdr.fixed, _shdr.csps);

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
        mpegd_read(this, buf, 1);

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
            pts = _packet.pts;

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

int MpegScan::packet()
{
    unsigned sid = _packet.sid;
    unsigned ssid = _packet.ssid;

    if (mpeg_stream_excl(sid, ssid))
        return 0;

    FILE *fp = _ext;
    uint64_t ofs = _ofs;

    if (mpegd_set_offset(this, ofs + this->_packet.size))
    {
        fprintf(fp, "%08" PRIxMAX ": sid=%02x ssid=%02x incomplete packet\n",
            uintmax_t(ofs), sid, ssid);
    }

    if (sid == 0xbd)
    {
        if (this->substreams[ssid].packet_cnt > 1)
        {
            if (_options->first_pts() == 0)
                return 0;

            if (!_packet.have_pts)
                return 0;

            if (_packet.pts >= pts2[ssid])
                return 0;
        }

        if (_packet.pts < pts2[ssid])
            pts2[ssid] = _packet.pts;
    }
    else
    {
        if (this->streams[sid].packet_cnt > 1)
        {
            if (_options->first_pts() == 0)
                return 0; 

            if (!_packet.have_pts)
                return 0;
    
            if (_packet.pts >= pts1[sid])
                return 0;
        }

        if (_packet.pts < pts1[sid])
            pts1[sid] = _packet.pts;
    }
    
    fprintf(fp, "%08" PRIxMAX ": sid=%02x", uintmax_t(ofs), sid);

    if (sid == 0xbd)
        fprintf(fp, "[%02x]", ssid);
    else
        fputs("    ", fp);

    if (_packet.type == 1)
        fputs(" MPEG1", fp);
    else if (_packet.type == 2)
        fputs(" MPEG2", fp);
    else
        fputs(" UNKWN", fp);

    if (_packet.have_pts)
    {
        fprintf(fp, " pts=%" PRIuMAX "[%.4f]",
            uintmax_t(_packet.pts), double(_packet.pts) / 90000.0);
    }

    fputs("\n", fp);
    fflush(fp);
    return 0;
}

int mpeg_demux_t::mpegd_parse_packet(mpeg_demux_t *)
{
    _packet.type = 0;
    uint32_t sid = mpegd_get_bits(24, 8);
    uint32_t ssid = 0;
    _packet.sid = sid;
    _packet.ssid = ssid;
    _packet.size = mpegd_get_bits(32, 16) + 6;
    _packet.offset = 6;
    _packet.have_pts = 0;
    _packet.pts = 0;
    _packet.have_dts = 0;
    _packet.dts = 0;
    unsigned i = 48;

    if ((sid >= 0xc0 && sid < 0xf0) || sid == 0xbd)
    {
        while (mpegd_get_bits(i, 8) == 0xff)
        {
            if (i > (48 + 16 * 8))
                break;

            i += 8;
        }

        if (mpegd_get_bits(i, 2) == 0x02)
        {
            if (mpegd_parse_packet2(this, i))
                return 1;
        }
        else
        {
            if (mpegd_parse_packet1(this, i))
                return 1;
        }
    }
    else if (sid == 0xbe)
    {
        _packet.type = 1;
    }

    if (sid == 0xbd)
    {
        ssid = mpegd_get_bits(8 * _packet.offset, 8);
        _packet.ssid = ssid;
    }

    if (packet_check(this))
    {
        if (mpegd_skip(this, 1))
            return 1;
    }
    else
    {
        _packet_cnt += 1;
        this->streams[sid].packet_cnt += 1;
        this->streams[sid].size += _packet.size - _packet.offset;

        if (sid == 0xbd)
        {
            this->substreams[ssid].packet_cnt += 1;
            this->substreams[ssid].size += _packet.size - _packet.offset;
        }

        uint64_t ofs = _ofs + _packet.size;

        if (packet())
            return 1;

        mpegd_set_offset(this, ofs);
    }

    return 0;
}

void mpeg_demux_t::mpeg_print_stats(mpeg_demux_t *mpeg, FILE *fp)
{
    fprintf(fp,
        "System headers: %u\n"
        "Packs:          %u\n"
        "Packets:        %u\n"
        "End codes:      %u\n"
        "Skipped:        %u bytes\n",
        _shdr_cnt, _pack_cnt, _packet_cnt, _end_cnt, _skip_cnt);

    for (unsigned i = 0; i < 256; i++)
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

    for (unsigned i = 0; i < 256; i++)
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

int MpegList::packet()
{
    if (_options->no_packet())
        return 0;

    uint32_t sid = _packet.sid;
    uint32_t ssid = _packet.ssid;

    if (mpeg_stream_excl(sid, ssid))
        return 0;

    FILE *fp = _ext;
    mpeg_list_print_skip(fp);

    fprintf(fp, "%08" PRIxMAX ": packet[%u]: sid=%02x",
        uintmax_t(_ofs), this->streams[sid].packet_cnt - 1, sid);

    if (sid == 0xbd)
        fprintf (fp, "[%02x]", ssid);
    else
        fputs ("    ", fp);

    if (_packet.type == 1)
        fputs(" MPEG1", fp);
    else if (_packet.type == 2)
        fputs(" MPEG2", fp);
    else
        fputs(" UNKWN", fp);

    fprintf(fp, " size=%u", _packet.size);

    if (_packet.have_pts || _packet.have_dts)
    {
        fprintf(fp, " pts=%" PRIuMAX "[%.4f] dts=%" PRIuMAX "[%.4f]",
            uintmax_t(_packet.pts), double(_packet.pts) / 90000.0,
            uintmax_t(_packet.dts), double(_packet.dts) / 90000.0);
    }

    fputs("\n", fp);
    return 0;
}

void mpeg_demux_t::_resetStats()
{
    _shdr_cnt = 0;
    _pack_cnt = 0;
    _packet_cnt = 0;
    _end_cnt = 0;
    _skip_cnt = 0;

    for (uint32_t i = 0; i < 256; i++)
    {
        this->streams[i].packet_cnt = 0;
        this->streams[i].size = 0;
        this->substreams[i].packet_cnt = 0;
        this->substreams[i].size = 0;
    }
}

int mpeg_demux_t::mpegd_parse_packet2(mpeg_demux_t *, unsigned i)
{
    _packet.type = 2;
    uint32_t pts_dts_flag = mpegd_get_bits(i + 8, 2);
    uint32_t cnt = mpegd_get_bits(i + 16, 8);

    if (pts_dts_flag == 0x02)
    {
        if (mpegd_get_bits(i + 24, 4) == 0x02)
        {
            uint64_t tmp = mpegd_get_bits(i + 28, 3);
            tmp = (tmp << 15) | mpegd_get_bits(i + 32, 15);
            tmp = (tmp << 15) | mpegd_get_bits(i + 48, 15);
            _packet.have_pts = 1;
            _packet.pts = tmp;
        }
    }
    else if ((pts_dts_flag & 0x03) == 0x03)
    {
        if (mpegd_get_bits(i + 24, 4) == 0x03)
        {
            uint64_t tmp = mpegd_get_bits(i + 28, 3);
            tmp = tmp << 15 | mpegd_get_bits(i + 32, 15);
            tmp = tmp << 15 | mpegd_get_bits(i + 48, 15);
            _packet.have_pts = 1;
            _packet.pts = tmp;
        }

        if (mpegd_get_bits(i + 64, 4) == 0x01)
        {
            uint64_t tmp = mpegd_get_bits(i + 68, 3);
            tmp = tmp << 15 | mpegd_get_bits(i + 72, 15);
            tmp = tmp << 15 | mpegd_get_bits(i + 88, 15);
            _packet.have_dts = 1;
            _packet.dts = tmp;
        }
    }

    i += 8 * (cnt + 3);
    _packet.offset = i / 8;
    return 0;
}

int mpeg_demux_t::mpegd_parse_pack(mpeg_demux_t *mpeg)
{
    if (mpegd_get_bits(32, 4) == 0x02)
    {
        _pack.type = 1;
        _pack.scr = mpegd_get_bits(36, 3);
        _pack.scr = mpeg->_pack.scr << 15 | mpegd_get_bits(40, 15);
        _pack.scr = mpeg->_pack.scr << 15 | mpegd_get_bits(56, 15);
        _pack.mux_rate = mpegd_get_bits(73, 22);
        _pack.stuff = 0;
        _pack.size = 12;
    }
    else if (mpegd_get_bits(32, 2) == 0x01)
    {
        _pack.type = 2;
        _pack.scr = mpegd_get_bits(34, 3);
        _pack.scr = mpeg->_pack.scr << 15 | mpegd_get_bits(38, 15);
        _pack.scr = mpeg->_pack.scr << 15 | mpegd_get_bits(54, 15);
        _pack.mux_rate = mpegd_get_bits(80, 22);
        _pack.stuff = mpegd_get_bits(109, 3);
        _pack.size = 14 + mpeg->_pack.stuff;
    }
    else
    {
        _pack.type = 0;
        _pack.scr = 0;
        _pack.mux_rate = 0;
        _pack.size = 4;
    }

    uint64_t ofs = _ofs + _pack.size;
    _pack_cnt += 1;

    if (mpeg->pack())
        return 1;

    mpegd_set_offset(this, ofs);
    mpegd_seek_header();

    if (mpegd_get_bits(0, 32) == MPEG_SYSTEM_HEADER)
    {
        if (mpegd_parse_system_header())
            return 1;

        mpegd_seek_header();
    }

    while (mpegd_get_bits(0, 24) == MPEG_PACKET_START)
    {
        uint32_t sid = mpegd_get_bits(24, 8);

        if (sid == 0xba || sid == 0xb9 || sid == 0xbb)
            break;

        mpegd_parse_packet(mpeg);
        mpegd_seek_header();
    }

    return 0;
}

int mpeg_demux_t::mpegd_parse_system_header()
{
    _shdr.size = mpegd_get_bits(32, 16) + 6;
    _shdr.fixed = mpegd_get_bits(78, 1);
    _shdr.csps = mpegd_get_bits(79, 1);
    _shdr_cnt += 1;
    uint64_t ofs = _ofs + _shdr.size;

    if (system_header())
        return 1;

    mpegd_set_offset(this, ofs);
    return 0;
}

int mpeg_demux_t::mpegd_skip(mpeg_demux_t *mpeg, unsigned n)
{
    size_t r;
    _ofs += n;
    
    if (n <= _buf_n)
    {
        _buf_i += n;
        _buf_n -= n;
        return 0;
    }
    
    n -= _buf_n;
    _buf_i = 0;
    _buf_n = 0; 

    while (n > 0)
    {
        if (n <= MPEG_DEMUX_BUFFER)
            r = fread(this->buf, 1, n, _fp);
        else
            r = fread(this->buf, 1, MPEG_DEMUX_BUFFER, _fp);

        if (r <= 0)
            return 1;

        n -= unsigned(r);
    }

    return 0;
}

int MpegList::pack()
{
    if (_options->no_pack())
        return 0;

    mpeg_list_print_skip(_ext);

    fprintf(_ext, "%08" PRIxMAX ": pack[%u]: "
        "type=%u scr=%" PRIuMAX "[%.4f] mux=%u[%.2f] stuff=%u\n",
        uintmax_t(_ofs), _pack_cnt - 1, _pack.type,
        uintmax_t(_pack.scr), double(_pack.scr) / 90000.0,
        _pack.mux_rate, 50.0 * _pack.mux_rate,
        _pack.stuff);

    fflush(_ext);
    return 0;
}

int MpegScan::scan(FILE *inp, FILE *out)
{
    for (uint32_t i = 0; i < 256; i++)
    {
        pts1[i] = 0xffffffffffffffff;
        pts2[i] = 0xffffffffffffffff;
    }

    _ext = out;
    int r = parse(this);
    mpeg_print_stats(this, out);
    close();
    return r;
}

int mpeg_demux_t::mpegd_seek_header()
{
    while (mpegd_get_bits(0, 24) != 1)
    {
        uint64_t ofs = _ofs + 1;

        if (skip())
            return 1;

        if (mpegd_set_offset(this, ofs))
            return 1;

        _skip_cnt += 1;
    }

    return 0;
}

int MpegScan::end()
{
    if (_options->no_end() == 0)
        fprintf(_ext, "%08" PRIxMAX ": end code\n", uintmax_t(_ofs));

    return 0;
}

uint32_t mpeg_demux_t::mpegd_get_bits(unsigned i, unsigned n)
{
    if (_mpegd_need_bits(i + n))
        return 0;
    
    uint32_t m;
    unsigned b_i;
    uint8_t *buf = this->buf + _buf_i;
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
        return r;
    }

    while (n > 0)
    {
        unsigned b_n = 8 - (i & 7);

        if (b_n > n)
            b_n = n; 

        b_i = 8 - (i & 7) - b_n;
        m = (1 << b_n) - 1;
        uint32_t v = (buf[i >> 3] >> b_i) & m;
        r = (r << b_n) | v;
        i += b_n;
        n -= b_n;
    }
    
    return r;
}

int mpeg_demux_t::mpegd_parse_packet1(mpeg_demux_t *mpeg, unsigned i)
{
    _packet.type = 1;

    if (mpegd_get_bits(i, 2) == 0x01)
        i += 16;

    uint32_t val = mpegd_get_bits(i, 8);

    if ((val & 0xf0) == 0x20)
    {
        uint64_t tmp = mpegd_get_bits(i + 4, 3);
        tmp = tmp << 15 | mpegd_get_bits(i + 8, 15);
        tmp = tmp << 15 | mpegd_get_bits(i + 24, 15);
        mpeg->_packet.have_pts = 1;
        mpeg->_packet.pts = tmp;
        i += 40;
    }
    else if ((val & 0xf0) == 0x30)
    {
        uint64_t tmp = mpegd_get_bits(i + 4, 3);
        tmp = tmp << 15 | mpegd_get_bits(i + 8, 15);
        tmp = tmp << 15 | mpegd_get_bits(i + 24, 15);
        mpeg->_packet.have_pts = 1;
        mpeg->_packet.pts = tmp;
        tmp = mpegd_get_bits(i + 44, 3);
        tmp = tmp << 15 | mpegd_get_bits(i + 48, 15);
        tmp = tmp << 15 | mpegd_get_bits(i + 64, 15);
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

int MpegRemux::skip()
{
    if (_options->remux_skipped() == 0)
        return 0;

    if (mpeg_copy(this, _ext, 1))
        return 1;

    return 0;
}

int MpegList::end()
{
    if (_options->no_end())
        return 0;

    mpeg_list_print_skip(_ext);
    fprintf(_ext, "%08" PRIxMAX ": end\n", uintmax_t(_ofs));
    return 0;
}

int mpeg_demux_t::mpeg_copy(mpeg_demux_t *, FILE *fp, unsigned n)
{
    uint8_t buf[4096];

    while (n > 0)
    {
        uint32_t i = n < 4096 ? n : 4096;
        uint32_t j = mpegd_read(this, buf, i);

        if (j > 0)
            if (fwrite(buf, 1, j, fp) != j)
                return 1;

        if (i != j)
            return 1;

        n -= i;
    }

    return 0;
}

int MpegRemux::end()
{
    if (_options->no_end())
        return 0;

    if (mpeg_copy(this, _ext, 4))
        return 1;

    if (_options->split())
        if (mpeg_remux_next_fp(this))
            return 1;

    return 0;
}

int mpeg_demux_t::_mpegd_need_bits(unsigned n)
{
    n = (n + 7) / 8;

    if (n > _buf_n)
        _mpegd_buffer_fill(this);
    
    if (n > _buf_n)
        return 1;
    
    return 0;
}

int mpeg_demux_t::_mpegd_buffer_fill(mpeg_demux_t *mpeg)
{
    unsigned n;

    if (_buf_i > 0 && _buf_n > 0)
        for (unsigned i = 0; i < _buf_n; i++)
            this->buf[i] = this->buf[_buf_i + i];
    
    mpeg->_buf_i = 0;
    n = MPEG_DEMUX_BUFFER - _buf_n;
    
    if (n > 0)
    {
        size_t r = fread(this->buf + _buf_n, 1, n, _fp);
    
        if (r < 0)
            return 1;

        _buf_n += unsigned(r);
    }

    return 0;
}


void mpeg_demux_t::close()
{
    if (_close)
        fclose(_fp);
}

int mpeg_demux_t::mpegd_set_offset(mpeg_demux_t *mpeg, uint64_t ofs)
{
    if (ofs == mpeg->_ofs)
        return 0;

    if (ofs > mpeg->_ofs)
        return mpegd_skip(mpeg, uint32_t(ofs - mpeg->_ofs));

    return 1;
}

unsigned mpeg_demux_t::mpegd_read(mpeg_demux_t *, void *buf, unsigned n)
{
    uint8_t *tmp = (uint8_t *)buf;
    uint32_t i = n < _buf_n ? n : _buf_n;
    uint32_t ret = i;

    if (i > 0)
    {
        memcpy(tmp, &this->buf[_buf_i], i);
        tmp += i;
        _buf_i += i;
        _buf_n -= i;
        n -= i;
    }

    if (n > 0)
        ret += fread(tmp, 1, n, _fp);

    _ofs += ret;
    return ret;
}

int mpeg_demux_t::parse(mpeg_demux_t *mpeg)
{
    while (true)
    {
        if (mpegd_seek_header())
            return 0;

        switch (mpegd_get_bits(0, 32))
        {
        case MPEG_PACK_START:
            if (mpegd_parse_pack(this))
                return 1;

            break;
        case MPEG_END_CODE:
        {
            _end_cnt += 1;
            uint64_t ofs = _ofs + 4;

            if (mpeg->end())
                return 1;

            if (mpegd_set_offset(this, ofs))
                return 1;
        } 
            break;
        default:
        {
            uint64_t ofs = _ofs + 1;

            if (skip())
                return 1;

            if (mpegd_set_offset(this, ofs))
                return 0;
        }
            break;
        }
    }

    return 0;
}

int MpegRemux::mpeg_remux_next_fp(mpeg_demux_t *mpeg)
{
    //close current file
    if (_ext != NULL)
        fclose(_ext);

    char *fname = mpeg_get_name(_options->_demux_name, _sequence);

    if (fname == NULL)
        return 1;

    _sequence += 1;
    _ext = fopen(fname, "wb");
    free(fname);
    return _ext == NULL ? 1 : 0;
}

int mpeg_demux_t::mpeg_buf_read(mpeg_buffer_t *buf, unsigned cnt)
{
    if (buf->setCnt(cnt))
        return 1;

    buf->cnt = mpegd_read(this, buf->buf, cnt);

    if (buf->cnt != cnt)
        return 1;

    return 0;
}


