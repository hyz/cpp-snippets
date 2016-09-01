#ifndef ILDEC_H__
#define ILDEC_H__

#include <stdint.h>

typedef struct imgdata_t Image;

struct AvcDecoder /*: boost::noncopyable*/ {
    struct ILAvcDec* impl = 0;

    AvcDecoder() {}
    ~AvcDecoder();
    bool setup(unsigned width, unsigned height, uint8_t* sps[2], uint8_t* pps[2]);

    void input(uint8_t* data, uint8_t* end, int flags=0);
    Image* outpoll(Image&, bool*interrupt); //(uint8_t* out[2]/*, OMX_BUFFERHEADERTYPE* bh = 0*/);
    //OMX_BUFFERHEADERTYPE* outpoll(OMX_BUFFERHEADERTYPE& bh) //(uint8_t* out[2]/*, OMX_BUFFERHEADERTYPE* bh = 0*/);

private:
    AvcDecoder& operator=(AvcDecoder const&);
    AvcDecoder(AvcDecoder const&);
};

#endif // ILDEC_H__

