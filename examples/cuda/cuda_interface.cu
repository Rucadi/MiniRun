
#include "cuda_interface.hpp"

__global__ void cudaSaxpy(int n, float a, float *x, float *y) 
{
    int i = blockIdx.x*blockDim.x + threadIdx.x;
    if (i < n)  y[i] = a*x[i] + y[i];
}


void* createStream()
{
    cudaStream_t stream;
    cudaStreamCreate(&stream);
    return (void*) stream;
}

#include <iostream>
void saxpy(int N, float* d_x, float* d_y, float value, void* stream)
{
    printf("CALLING SAXPY %p %p %p \n", d_x, d_y, stream);
    cudaSaxpy<<<4096,256,0,(cudaStream_t) stream>>>(N,value, d_x, d_y);
}

void setActive(int idx)
{
    cudaSetDevice(idx);
}


void* cMalloc(size_t size)
{
    void* ptr;
    cudaMalloc(&ptr, size);
    return ptr;
}
void cFree(void* ptr)
{
    cudaFree(ptr);
}


void copyToDevice(void* dst, void* src, size_t N, void* stream)
{
    cudaMemcpyAsync(dst, src, N, cudaMemcpyHostToDevice, (cudaStream_t) stream);
}
void copyToHost(void* dst, void* src, size_t N, void* stream)
{
    cudaMemcpyAsync(dst, src, N, cudaMemcpyDeviceToHost, (cudaStream_t) stream); 
}

bool streamEmpty(void* stream)
{
    return cudaStreamQuery((cudaStream_t)  stream) == cudaSuccess;
}