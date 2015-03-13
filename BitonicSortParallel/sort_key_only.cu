#include <stdio.h>
#include <math.h>

#include <cuda.h>
#include "cuda_runtime.h"
#include "device_launch_parameters.h"

#include "../Utils/data_types_common.h"
#include "../Utils/host.h"
#include "constants.h"
#include "sort.h"
#include "kernels_key_only.h"


/*
Sorts sub-blocks of input data with bitonic sort.
*/
template <order_t sortOrder, uint_t threadsBitonicSort, uint_t elemsThreadBitonicSort>
void BitonicSortParallel::runBitoicSortKernel(data_t *d_keys, uint_t arrayLength)
{
    uint_t elemsPerThreadBlock = threadsBitonicSort * elemsThreadBitonicSort;
    uint_t sharedMemSize = elemsPerThreadBlock * sizeof(*d_keys);

    dim3 dimGrid((arrayLength - 1) / elemsPerThreadBlock + 1, 1, 1);
    dim3 dimBlock(threadsBitonicSort, 1, 1);

    bitonicSortKernel<sortOrder, threadsBitonicSort, elemsThreadBitonicSort><<<dimGrid, dimBlock, sharedMemSize>>>(
        d_keys, arrayLength
    );
}

/*
Merges array, if data blocks are larger than shared memory size. It executes only of STEP on PHASE per
kernel launch.
*/
template <order_t sortOrder, uint_t threadsMerge, uint_t elemsThreadMerge>
void BitonicSortParallel::runBitonicMergeGlobalKernel(
    data_t *d_keys, uint_t arrayLength, uint_t phase, uint_t step
)
{
    uint_t elemsPerThreadBlock = threadsMerge * elemsThreadMerge;
    dim3 dimGrid((arrayLength - 1) / elemsPerThreadBlock + 1, 1, 1);
    dim3 dimBlock(threadsMerge, 1, 1);

    bool isFirstStepOfPhase = phase == step;

    if (isFirstStepOfPhase)
    {
        bitonicMergeGlobalKernel<sortOrder, true, threadsMerge, elemsThreadMerge><<<dimGrid, dimBlock>>>(
            d_keys, arrayLength, step
        );
    }
    else
    {
        bitonicMergeGlobalKernel<sortOrder, false, threadsMerge, elemsThreadMerge><<<dimGrid, dimBlock>>>(
            d_keys, arrayLength, step
        );
    }
}

/*
Merges array when stride is lower than shared memory size. It executes all remaining STEPS of current PHASE.
*/
template <order_t sortOrder, uint_t threadsMerge, uint_t elemsThreadMerge>
void BitonicSortParallel::runBitoicMergeLocalKernel(
    data_t *d_keys, uint_t arrayLength, uint_t phase, uint_t step
)
{
    // Every thread loads and sorts 2 elements
    uint_t elemsPerThreadBlock = threadsMerge * elemsThreadMerge;
    uint_t sharedMemSize = elemsPerThreadBlock * sizeof(*d_keys);
    dim3 dimGrid((arrayLength - 1) / elemsPerThreadBlock + 1, 1, 1);
    dim3 dimBlock(threadsMerge, 1, 1);

    bool isFirstStepOfPhase = phase == step;


    if (isFirstStepOfPhase)
    {
        bitonicMergeLocalKernel
            <sortOrder, true, threadsMerge, elemsThreadMerge><<<dimGrid, dimBlock, sharedMemSize>>>(
            d_keys, arrayLength, step
        );
    }
    else
    {
        bitonicMergeLocalKernel
            <sortOrder, false, threadsMerge, elemsThreadMerge><<<dimGrid, dimBlock, sharedMemSize>>>(
            d_keys, arrayLength, step
        );
    }
}

/*
Sorts data with parallel NORMALIZED BITONIC SORT.
*/
template <order_t sortOrder>
void BitonicSortParallel::bitonicSortParallel(data_t *d_keys, uint_t arrayLength)
{
    uint_t arrayLenPower2 = nextPowerOf2(arrayLength);
    uint_t elemsPerBlockBitonicSort = THREADS_BITONIC_SORT_KO_BSP * ELEMS_THREAD_BITONIC_SORT_KO_BSP;
    uint_t elemsPerBlockMergeLocal = THREADS_LOCAL_MERGE_KO_BSP * ELEMS_THREAD_LOCAL_MERGE_KO_BSP;

    // Number of phases, which can be executed in shared memory (stride is lower than shared memory size)
    uint_t phasesBitonicSort = log2((double)min(arrayLenPower2, elemsPerBlockBitonicSort));
    uint_t phasesMergeLocal = log2((double)min(arrayLenPower2, elemsPerBlockMergeLocal));
    uint_t phasesAll = log2((double)arrayLenPower2);

    runBitoicSortKernel<sortOrder, THREADS_BITONIC_SORT_KO_BSP, ELEMS_THREAD_BITONIC_SORT_KO_BSP>(
        d_keys, arrayLength
    );

    // Bitonic merge
    for (uint_t phase = phasesBitonicSort + 1; phase <= phasesAll; phase++)
    {
        uint_t step = phase;
        while (step > phasesMergeLocal)
        {
            runBitonicMergeGlobalKernel<sortOrder, THREADS_GLOBAL_MERGE_KO_BSP, ELEMS_THREAD_GLOBAL_MERGE_KO_BSP>(
                d_keys, arrayLength, phase, step
            );
            step--;
        }

        runBitoicMergeLocalKernel<sortOrder, THREADS_LOCAL_MERGE_KO_BSP, ELEMS_THREAD_LOCAL_MERGE_KO_BSP>(
            d_keys, arrayLength, phase, step
        );
    }
}

/*
Wrapper for bitonic sort method.
The code runs faster if arguments are passed to method. If members are accessed directly, code runs slower.
*/
void BitonicSortParallel::sortKeyOnly()
{
    if (_sortOrder == ORDER_ASC)
    {
        bitonicSortParallel<ORDER_ASC>(_d_keys, _arrayLength);
    }
    else
    {
        bitonicSortParallel<ORDER_DESC>(_d_keys, _arrayLength);
    }
}
