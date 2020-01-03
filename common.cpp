#include "common.h"

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

MpegDemux::MpegDemux(FILE *fp) : mpeg_demux_t(fp)
{
}




