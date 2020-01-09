#ifndef BUFFER_H
#define BUFFER_H

#include <inttypes.h>
#include <cstdio>

class mpeg_buffer_t
{
public:
    uint8_t *buf = nullptr;
    uint32_t cnt = 0;
    uint32_t max = 0;
    int setCnt(unsigned cnt);
    int setMax(unsigned max);
    void init();
    void free();
    void clear();
    int write_clear(FILE *fp);
};

#endif


