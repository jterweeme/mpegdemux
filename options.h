#ifndef OPTIONS_H
#define OPTIONS_H

#include <inttypes.h>

#define GETOPT_DONE    -1
#define GETOPT_UNKNOWN -2
#define GETOPT_MISSING -3

struct mpegd_option_t
{
    int16_t name1;
    uint16_t argcnt;
    const char *name2;
    const char *argdesc;
    const char *optdesc;
};

class Options
{
private:
    int _no_pack = 0;
    int _remux_skipped = 0;
    int _no_shdr = 0;
    int _split = 0;
    int _dvdac3 = 0;
    int _drop = 1;
    int _atend = 0;
    int index1 = -1;
    int index2 = -1;
    const char *curopt = nullptr;
    mpegd_option_t *_find_option_name1(mpegd_option_t *opt, int name1) const;
    mpegd_option_t *_find_option_name2(mpegd_option_t *opt, const char *name2) const;
public:
    int mpegd_getopt(int argc, char **argv, char ***optarg);
    int no_pack() const;
    void no_pack(int val);
    int remux_skipped() const;
    void remux_skipped(int val);
    int no_shdr() const;
    void no_shdr(int val);
    int split() const;
    void split(int val);
    int dvdac3() const;
    void dvdac3(int val);
    int drop() const;
    void drop(int val);
};

#endif


