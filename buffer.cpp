#include "buffer.h"
#include <cstdlib>

void mpeg_buffer_t::init()
{
    buf = nullptr;
    max = 0;
    cnt = 0;
}

void mpeg_buffer_t::free()
{
    ::free(buf);
    buf = nullptr;
    cnt = 0;
    max = 0;
}

void mpeg_buffer_t::clear()
{
    cnt = 0;
}

int mpeg_buffer_t::setMax(unsigned max)
{
    if (this->max == max)
        return 0;

    if (max == 0)
    {
        ::free(this->buf);
        this->max = 0;
        this->cnt = 0;
        return 0;
    }

    this->buf = (uint8_t *)realloc(this->buf, max);

    if (this->buf == nullptr)
    {
        this->max = 0;
        this->cnt = 0;
        return 1;
    }

    this->max = max;

    if (this->cnt > max)
        this->cnt = max;

    return 0;
}

int mpeg_buffer_t::setCnt(unsigned cnt)
{
    if (cnt > this->max)
        if (setMax(cnt))
            return 1;

    this->cnt = cnt;
    return 0;
}

int mpeg_buffer_t::write_clear(FILE *fp)
{
    if (cnt > 0)
    {
        if (fwrite(buf, 1, cnt, fp) != cnt)
        {
            cnt = 0;
            return 1;
        }
    }

    cnt = 0;
    return 0;
}


