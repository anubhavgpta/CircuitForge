#include <cuda_runtime.h>

#include <iostream>
#include <vector>
#include <cstdlib>

// Check CUDA API calls for errors.
#define CudaCheck(Call)                                            \
    do {                                                           \
        cudaError_t Error = (Call);                                \
        if (Error != cudaSuccess) {                                \
            std::cerr << "CUDA error: "                             \
                      << cudaGetErrorString(Error)                 \
                      << " at line " << __LINE__ << '\n';           \
            std::exit(EXIT_FAILURE);                               \
        }                                                          \
    } while (0)

// GPU kernel: Y = (A AND B) XOR C
__global__ void LogicKernel(
    const int* InputA,
    const int* InputB,
    const int* InputC,
    int* Output,
    int Count
) {
    int Index = blockIdx.x * blockDim.x + threadIdx.x;

    // Prevent threads from accessing invalid memory.
    if (Index >= Count) {
        return;
    }

    int AndResult = InputA[Index] & InputB[Index];
    Output[Index] = AndResult ^ InputC[Index];
}

// CPU equivalent for correctness verification.
int EvaluateCpu(int A, int B, int C) {
    return (A & B) ^ C;
}

int main() {
    constexpr int Count = 8;

    std::vector<int> HostA(Count);
    std::vector<int> HostB(Count);
    std::vector<int> HostC(Count);
    std::vector<int> HostOutput(Count);

    // Generate all eight binary input combinations.
    for (int Index = 0; Index < Count; ++Index) {
        HostA[Index] = (Index >> 2) & 1;
        HostB[Index] = (Index >> 1) & 1;
        HostC[Index] = Index & 1;
    }

    // GPU memory pointers.
    int* DeviceA = nullptr;
    int* DeviceB = nullptr;
    int* DeviceC = nullptr;
    int* DeviceOutput = nullptr;

    const size_t Bytes = Count * sizeof(int);

    // Allocate GPU memory.
    CudaCheck(cudaMalloc(
        reinterpret_cast<void**>(&DeviceA), Bytes
    ));

    CudaCheck(cudaMalloc(
        reinterpret_cast<void**>(&DeviceB), Bytes
    ));

    CudaCheck(cudaMalloc(
        reinterpret_cast<void**>(&DeviceC), Bytes
    ));

    CudaCheck(cudaMalloc(
        reinterpret_cast<void**>(&DeviceOutput), Bytes
    ));

    // Copy input arrays from CPU to GPU.
    CudaCheck(cudaMemcpy(
        DeviceA,
        HostA.data(),
        Bytes,
        cudaMemcpyHostToDevice
    ));

    CudaCheck(cudaMemcpy(
        DeviceB,
        HostB.data(),
        Bytes,
        cudaMemcpyHostToDevice
    ));

    CudaCheck(cudaMemcpy(
        DeviceC,
        HostC.data(),
        Bytes,
        cudaMemcpyHostToDevice
    ));

    // Configure GPU execution.
    constexpr int ThreadsPerBlock = 256;

    const int BlocksPerGrid =
        (Count + ThreadsPerBlock - 1) / ThreadsPerBlock;

    std::cout << "Launching CUDA kernel...\n";
    std::cout << "Blocks: " << BlocksPerGrid << '\n';
    std::cout << "Threads per block: " << ThreadsPerBlock << '\n';

    // Execute logic simulation on the GPU.
    LogicKernel<<<BlocksPerGrid, ThreadsPerBlock>>>(
        DeviceA,
        DeviceB,
        DeviceC,
        DeviceOutput,
        Count
    );

    // Check launch errors and wait for GPU completion.
    CudaCheck(cudaGetLastError());
    CudaCheck(cudaDeviceSynchronize());

    // Copy GPU results back to CPU memory.
    CudaCheck(cudaMemcpy(
        HostOutput.data(),
        DeviceOutput,
        Bytes,
        cudaMemcpyDeviceToHost
    ));

    std::cout << "\nA B C | GPU CPU | Status\n";
    std::cout << "------------------------\n";

    bool AllPassed = true;

    // Compare GPU results with CPU results.
    for (int Index = 0; Index < Count; ++Index) {
        int CpuOutput = EvaluateCpu(
            HostA[Index],
            HostB[Index],
            HostC[Index]
        );

        bool Passed = (HostOutput[Index] == CpuOutput);

        std::cout << HostA[Index] << " "
                  << HostB[Index] << " "
                  << HostC[Index] << " | "
                  << HostOutput[Index] << "   "
                  << CpuOutput << "   | "
                  << (Passed ? "PASS" : "FAIL")
                  << '\n';

        if (!Passed) {
            AllPassed = false;
        }
    }

    // Free GPU memory.
    CudaCheck(cudaFree(DeviceA));
    CudaCheck(cudaFree(DeviceB));
    CudaCheck(cudaFree(DeviceC));
    CudaCheck(cudaFree(DeviceOutput));

    if (!AllPassed) {
        std::cerr << "\nVerification FAILED!\n";
        return EXIT_FAILURE;
    }

    std::cout << "\nAll 8 test patterns passed!\n";
    std::cout << "CircuitForge simulation completed.\n";

    return EXIT_SUCCESS;
}
