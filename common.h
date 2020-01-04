#ifndef COMMON_H
#define COMMON_H

#include <inttypes.h>
#include <cstdio>

class mpeg_buffer_t;
class Options;

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
    int mpegd_seek_header();
    int mpegd_parse_system_header();
    int mpegd_buffer_fill(mpeg_demux_t *mpeg);
    int _mpegd_need_bits(unsigned n);
protected:
    Options *_options;
    char *mpeg_get_name(const char *base, unsigned sid);
    uint32_t mpegd_get_bits(mpeg_demux_t *mpeg, unsigned i, unsigned n);
    int mpegd_parse_packet1(mpeg_demux_t *mpeg, unsigned i);
    int mpegd_parse_packet2(mpeg_demux_t *mpeg, unsigned i);
    int mpegd_skip(mpeg_demux_t *mpeg, unsigned n);
    int mpegd_set_offset(mpeg_demux_t *mpeg, uint64_t ofs);
    int mpegd_parse_packet(mpeg_demux_t *mpeg);
    unsigned mpegd_read(mpeg_demux_t *mpeg, void *buf, unsigned n);
    int mpeg_buf_read(mpeg_buffer_t *buf, unsigned cnt);
    int mpeg_copy(mpeg_demux_t *mpeg, FILE *fp, unsigned n);
    int mpeg_stream_excl(uint8_t sid, uint8_t ssid);
public:
    FILE *_fp;
    uint64_t _ofs = 0;
    uint32_t _buf_i = 0;
    uint32_t _buf_n = 0;
    uint8_t buf[MPEG_DEMUX_BUFFER];
    mpeg_shdr_t _shdr;
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
    int mpegd_parse_pack(mpeg_demux_t *mpeg);
    int parse(mpeg_demux_t *mpeg);
    virtual int pack();
    virtual int packet();
    virtual int end();
    virtual int skip();
    virtual int system_header();
    virtual int packet_check(mpeg_demux_t *mpeg);
    mpeg_demux_t(FILE *fp, Options *options);
    void close();
};

class MpegDemux : public mpeg_demux_t
{
private:
    int mpeg_demux_copy_spu(mpeg_demux_t *mpeg, FILE *fp, unsigned cnt);
    FILE *mpeg_demux_open(mpeg_demux_t *mpeg, unsigned sid, unsigned ssid);
public:
    MpegDemux(FILE *fp, Options *options);
    int packet() override;
};

class MpegRemux : public mpeg_demux_t
{
public:
    MpegRemux(FILE *fp, Options *options);
    int mpeg_remux_next_fp(mpeg_demux_t *mpeg);
    int skip() override;
    int pack() override;
    int system_header() override;
    int packet() override;
    int end() override;
};

class MpegScan : public mpeg_demux_t
{
public:
    MpegScan(FILE *fp, Options *options);
    int packet() override;
    int end() override;
    int scan(FILE *inp, FILE *out);
};

class MpegList : public mpeg_demux_t
{
public:
    MpegList(FILE *fp, Options *options);
    static void mpeg_list_print_skip(FILE *fp);
    int skip() override;
    int pack() override;
    int system_header() override;
    int packet() override;
    int end() override;
};

#endif


