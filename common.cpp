#include "common.h"
#include "buffer.h"
#include <cstring>

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
    this->ext = NULL;
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

int mpeg_demux_t::mpegd_buffer_fill(mpeg_demux_t *mpeg)
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

        switch (mpegd_get_bits(mpeg, 0, 32))
        {
        case MPEG_PACK_START:
            if (mpegd_parse_pack(this))
                return 1;

            break;
        case MPEG_END_CODE:
        {
            mpeg->end_cnt += 1;
            uint64_t ofs = mpeg->_ofs + 4;

            if (mpeg->end())
                return 1;

            if (mpegd_set_offset(mpeg, ofs))
                return 1;
        } 
            break;
        default:
        {
            uint64_t ofs = this->_ofs + 1;

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

int mpeg_demux_t::mpeg_buf_read(mpeg_buffer_t *buf, unsigned cnt)
{
    if (buf->setCnt(cnt))
        return 1;

    buf->cnt = mpegd_read(this, buf->buf, cnt);

    if (buf->cnt != cnt)
        return 1;

    return 0;
}


