
#include <stddef.h>


extern "C"
{

    void saxpy(int N, float* d_x, float* d_y, float value, void* stream);

    void* createStream();

    void  setActive(int idx);

    void *cMalloc(size_t size);
    void cFree(void* ptr);

    void copyToDevice(void* dst, void* src, size_t size, void* stream);
    void copyToHost(void* dst, void* src, size_t size, void* stream);

    bool streamEmpty(void* stream);
};