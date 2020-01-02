#include "options.h"
#include <cstring>
#include <cstdio>

int Options::no_pack() const
{
    return _no_pack;
}

void Options::no_pack(int val)
{
    _no_pack = val;
}

int Options::remux_skipped() const
{
    return _remux_skipped;
}

void Options::remux_skipped(int val)
{
    _remux_skipped = val;
}

int Options::no_shdr() const
{
    return _no_shdr;
}

void Options::no_shdr(int val)
{
    _no_shdr = val;
}

int Options::split() const
{
    return _split;
}

void Options::split(int val)
{
    _split = val;
}

int Options::drop() const
{
    return _drop;
}

void Options::drop(int val)
{
    _drop = val;
}

int Options::dvdac3() const
{
    return _dvdac3;
}

void Options::dvdac3(int val)
{
    _dvdac3 = val;
}

mpegd_option_t *Options::_find_option_name1(mpegd_option_t *opt, int name1) const
{
    while (opt->name1 >= 0)
    {
        if (opt->name1 == name1)
            return (opt);

        opt += 1;
    }

    return nullptr;
}

mpegd_option_t *
Options::_find_option_name2(mpegd_option_t *opt, const char *name2) const
{
    while (opt->name1 >= 0)
    {
        if (strcmp(opt->name2, name2) == 0)
            return opt;

        opt += 1;
    }

    return nullptr;
}

static mpegd_option_t opt[] = {
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

int Options::mpegd_getopt(int argc, char **argv, char ***optarg)
{
    mpegd_option_t *ret;

    if (index1 < 0)
    {
        _atend = 0;
        index1 = 0;
        index2 = 1;
        curopt = nullptr;
    }

    if (_atend)
    {
        if (index2 >= argc)
            return (GETOPT_DONE);

        index1 = index2;
        index2 += 1;
        *optarg = argv + index1;
        return 0;
    }

    if (curopt == nullptr || *curopt == 0)
    {
        if (index2 >= argc)
            return (GETOPT_DONE);

        index1 = index2;
        index2 += 1;
        curopt = argv[index1];

        if ((curopt[0] != '-') || (curopt[1] == 0))
        {
            *optarg = argv + index1;
            curopt = nullptr;
            return 0;
        }

        if (curopt[1] == '-')
        {
            if (curopt[2] == 0)
            {
                _atend = 1;

                if (index2 >= argc)
                    return (GETOPT_DONE);

                index1 = index2;
                index2 += 1;
                *optarg = argv + index1;
                return 0;
            }

            ret = _find_option_name2 (opt, curopt + 2);

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

    ret = _find_option_name1 (opt, *curopt);

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


