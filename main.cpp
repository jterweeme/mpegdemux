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

class Main
{
private:
    void print_version() const;
public:
    int run(int argc, char **argv);
};

void Main::print_version() const
{
    fputs("mpegdemux version 0.0.0\n\n" 
        "Copyright (C) 2003-2010 Hampa Hug <hampa@hampa.ch>\n",
        stdout);
}

int Main::run(int argc, char **argv)
{
    Options opts;
    opts.parse(argc, argv);
    int ret = 1;

    switch (opts._par_mode)
    {
    case PAR_MODE_SCAN:
    {
        MpegScan mpeg(opts._par_inp, &opts);
        ret = mpeg.scan(opts._par_inp, opts._par_out);
    }
        break;
    case PAR_MODE_LIST:
    {
        MpegList mpeg(opts._par_inp, &opts);
        ret = mpeg.list(opts._par_inp, opts._par_out);
    }
        break;
    case PAR_MODE_REMUX:
    {
        MpegRemux mpeg(opts._par_inp, &opts);
        ret = mpeg.remux(opts._par_inp, opts._par_out);
    }
        break;
    case PAR_MODE_DEMUX:
    {
        MpegDemux mpeg(opts._par_inp, &opts);
        ret = mpeg.demux(opts._par_inp, opts._par_out);
    }
        break;
    default:
        ret = 1;
        break;
    }

    if (ret)
        return 1;

    return 0;
}

int main(int argc, char **argv)
{
    Main main;
    return main.run(argc, argv);
}


