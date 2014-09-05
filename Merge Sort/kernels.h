#ifndef KERNELS_H
#define KERNELS_H

__global__ void mergeSortKernel(el_t *input, el_t *output, bool orderAsc);
__global__ void generateSamplesKernel(el_t *table, el_t *samples, uint_t sortedBlockSize, bool orderAsc);
__global__ void generateRanksKernel(el_t* table, el_t *samples, uint_t *ranksEven, uint_t *ranksOdd,
                                    uint_t sortedBlockSize, bool orderAsc);
__global__ void mergeKernel(el_t* input, el_t* output, uint_t *ranksEven, uint_t *ranksOdd, uint_t tableLen,
                            uint_t sortedBlockSize, uint_t subBlockSize);

#endif
