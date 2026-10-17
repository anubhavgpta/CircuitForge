#include <cstdio>
#include <cuda_runtime.h>

__global__ void HelloKernel() {
    printf("Hello from GPU thread %d\n", threadIdx.x);
}

int main() {
    HelloKernel<<<1, 4>>>();

    cudaError_t Error = cudaDeviceSynchronize();

    if (Error != cudaSuccess) {
        printf("CUDA error: %s\n",
               cudaGetErrorString(Error));
        return 1;
    }

    return 0;
}