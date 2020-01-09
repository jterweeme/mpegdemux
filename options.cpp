#include "options.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

static char *str_clone(const char *str)
{
    char *ret = (char *)malloc(strlen(str) + 1);

    if (ret == NULL)
        return NULL;

    strcpy(ret, str);
    return ret;
}

static const char *str_skip_white(const char *str)
{
    while (*str == ' ' || *str == '\t')
        str += 1;

    return str;
}

static int
str_get_streams(const char *str, uint8_t stm[256], unsigned msk)
{
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
            for (unsigned i = stm1; i <= stm2; i++)
                stm[i] |= msk;
        }
        else
        {
            for (unsigned i = stm1; i <= stm2; i++)
                stm[i] &= ~msk;
        }

        str = str_skip_white(str);

        if (*str == '/')
            str += 1;
    }

    return 0;
}


Options::Options()
{
    for (uint16_t i = 0; i < 256; i++)
    {
        _par_stream[i] = 0;
        _par_substream[i] = 0;
        _par_stream_map[i] = i;
        _par_substream_map[i] = i;
    }
}

uint32_t Options::packet_max() const
{
    return _packet_max;
}

void Options::packet_max(uint32_t val)
{
    _packet_max = val;
}

int Options::dvdsub() const
{
    return _dvdsub;
}

void Options::dvdsub(int val)
{
    _dvdsub = val;
}

int Options::first_pts() const
{
    return _first_pts;
}

void Options::first_pts(int val)
{
    _first_pts = val;
}

int Options::empty_pack() const
{
    return _empty_pack;
}

void Options::empty_pack(int val)
{
    _empty_pack = val;
}

int Options::no_end() const
{
    return _no_end;
}

void Options::no_end(int val)
{
    _no_end = val;
}

int Options::no_packet() const
{
    return _no_packet;
}

void Options::no_packet(int val)
{
    _no_packet = val;
}

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

int Options::parse(int argc, char **argv)
{
    char **optarg;
    
    while (true)
    {
        int r = mpegd_getopt(argc, argv, &optarg);

        if (r == GETOPT_DONE)
            break;

        if (r < 0)
            return 1;

        switch (r)
        {
        case '?':
            return 0;
        case 'a':
            dvdac3(1);
            break;
        case 'b':
            if (_demux_name != NULL)
                free(_demux_name);

            _demux_name = str_clone(optarg[0]);
            break;
        case 'c':
            _par_mode = PAR_MODE_SCAN;

            for (unsigned i = 0; i < 256; i++)
            {
                _par_stream[i] |= PAR_STREAM_SELECT;
                _par_substream[i] |= PAR_STREAM_SELECT;
            }
            break;
        case 'd':
            _par_mode = PAR_MODE_DEMUX;
            break;
        case 'D':
            drop(0);
            break;
        case 'e':
            no_end(1);
            break;
        case 'E':
            empty_pack(1);
            break;
        case 'F':
            first_pts(1);
            break;
        case 'h':
            no_shdr(1);
            break;
        case 'i':
            if (strcmp(optarg[0], "-") == 0)
            {
                for (unsigned i = 0; i < 256; i++)
                {
                    if (_par_stream[i] & PAR_STREAM_SELECT)
                        _par_stream[i] &= ~PAR_STREAM_INVALID;
                    else
                        _par_stream[i] |= PAR_STREAM_INVALID;
                }
            }
            else
            {
                if (str_get_streams(optarg[0], _par_stream, PAR_STREAM_INVALID))
                {
                    fprintf(stderr, "%s: bad stream id (%s)\n", argv[0], optarg[0]);
                    return 1;
                }
            }
            break;
        case 'k':
            no_pack(1);
            break;
        case 'K':
            remux_skipped(1);
            break;
        case 'l':
            _par_mode = PAR_MODE_LIST;
            break;
        case 'm':
            packet_max(unsigned(strtoul(optarg[0], NULL, 0)));
            break;
        case 'p':
            if (str_get_streams(optarg[0], _par_substream, PAR_STREAM_SELECT))
            {
                fprintf(stderr, "%s: bad substream id (%s)\n", argv[0], optarg[0]);
                return 1;
            }
            break;
        case 'P':
        {
            unsigned id1 = unsigned(strtoul(optarg[0], NULL, 0));
            unsigned id2 = unsigned(strtoul(optarg[1], NULL, 0));
            _par_substream_map[id1 & 0xff] = id2 & 0xff;
        }
            break;
        case 'r':
            _par_mode = PAR_MODE_REMUX;
            break;
        case 's':
            if (str_get_streams(optarg[0], _par_stream, PAR_STREAM_SELECT))
            {
                fprintf(stderr, "%s: bad stream id (%s)\n", argv[0], optarg[0]);
                return 1;
            }
            break;
        case 'S':
        {
            unsigned id1 = unsigned(strtoul(optarg[0], NULL, 0));
            unsigned id2 = unsigned(strtoul(optarg[1], NULL, 0));
            _par_stream_map[id1 & 0xff] = id2 & 0xff;
        }
            break;
        case 't':
            no_packet(1);
            break;
        case 'u':
            dvdsub(1);
            break;
        case 'V':
            //print_version();
            return 0;
        case 'x':
            split(1);
            break;
        case 0:
            if (_par_inp == nullptr)
            {
                if (strcmp(optarg[0], "-") == 0)
                    _par_inp = stdin;
                else
                    _par_inp = fopen(optarg[0], "rb");

                if (_par_inp == nullptr)
                {
                    fprintf(stderr, "%s: can't open input file (%s)\n",
                                argv[0], optarg[0]);

                    return 1;
                }
            }
            else if (_par_out == nullptr)
            {
                if (strcmp(optarg[0], "-") == 0)
                    _par_out = stdout;
                else
                    _par_out = fopen(optarg[0], "wb");

                if (_par_out == NULL)
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

    if (_par_inp == nullptr)
        _par_inp = stdin;

    if (_par_out == nullptr)
        _par_out = stdout;

    return 0;
}


