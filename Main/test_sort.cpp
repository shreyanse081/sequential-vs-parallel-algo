#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <vector>
#include <memory>
#include <string>
#include <fstream>

#include <cuda.h>
#include "cuda_runtime.h"
#include "device_launch_parameters.h"

#include "../Utils/data_types_common.h"
#include "../Utils/sort_interface.h"
#include "../Utils/host.h"
#include "../Utils/file.h"
#include "../Utils/generator.h"
#include "../Utils/sort_correct.h"
#include "constants.h"


/*
Folder path to specified distribution.
*/
std::string folderPathDistribution(data_dist_t distribution)
{
    std::string distFolderName(FOLDER_SORT_ROOT);
    distFolderName += strCapitalize(getDistributionName(distribution));
    return distFolderName;
}

/*
Folder path to data type of specified distribution.
*/
std::string folderPathDataType(data_dist_t distribution)
{
    std::string distFolderName = folderPathDistribution(distribution);
    return distFolderName + "/" + strReplace(typeid(data_t).name(), ' ', '_');
}

/*
Creates the folder structure in order to save sort statistics to disc.
*/
void createFolderStructure(std::vector<data_dist_t> distributions)
{
    createFolder(FOLDER_SORT_ROOT);
    createFolder(FOLDER_SORT_TEMP);

    // Creates a folder for every distribution, inside which creates a folder for data type.
    for (std::vector<data_dist_t>::iterator dist = distributions.begin(); dist != distributions.end(); dist++)
    {
        createFolder(folderPathDistribution(*dist));
        createFolder(folderPathDataType(*dist));
        createFolder(folderPathDataType(*dist) + "/" + FOLDER_SORT_TIMERS);
        createFolder(folderPathDataType(*dist) + "/" + FOLDER_SORT_CORRECTNESS);
        createFolder(folderPathDataType(*dist) + "/" + FOLDER_SORT_STABILITY);
    }
}

/*
Checks if sorted values are unique.
Not so trivial in some sorts - for example quicksort.
*/
void checkValuesUniqueness(data_t *values, uint_t arrayLength)
{
    uint_t *uniquenessTable = (uint_t*)malloc(arrayLength * sizeof(*uniquenessTable));

    for (uint_t i = 0; i < arrayLength; i++)
    {
        uniquenessTable[i] = 0;
    }

    for (uint_t i = 0; i < arrayLength; i++)
    {
        if (values[i] < 0 || values[i] > arrayLength - 1)
        {
            printf("Value out of range: %d\n", values[i]);
            getchar();
            exit(EXIT_FAILURE);
        }
        else if (++uniquenessTable[values[i]] > 1)
        {
            printf("Duplicate value: %d\n", values[i]);
            getchar();
            exit(EXIT_FAILURE);
        }
    }

    free(uniquenessTable);
}

/*
Determines if array is sorted in stable manner.
*/
bool isSortStable(data_t *keys, data_t *values, uint_t arrayLength)
{
    if (arrayLength == 1)
    {
        return true;
    }

    // Generally not needed
    // checkValuesUniqueness(values, tableLen);

    for (uint_t i = 1; i < arrayLength; i++)
    {
        // For same keys, values have to be ordered in ascending order.
        if (keys[i - 1] == keys[i] && values[i - 1] > values[i])
        {
            return false;
        }
    }

    return true;
}

/*
Writes number to file. This could be sort time, sort correctness or sort stability.
*/
void writeNumberToFile(
    Sort *sort, data_dist_t distribution, std::string folder, double number, bool isLastTestRepetition
)
{
    std::string folderDataType = folderPathDataType(distribution);
    std::string folderTimers = folderDataType + "/" + folder;
    std::string filePath = folderTimers + strSlugify(sort->getSortName()) + FILE_EXTENSION;
    std::fstream file;
    file.open(filePath, std::fstream::app);

    file << number;
    file << (isLastTestRepetition ? FILE_NEW_LINE : FILE_SEPARATOR_CHAR);

    file.close();
}

/*
Allocates memory, performs sorts and times it with stopwatch, destroys extra memory and returns time.
*/
double stopwatchSort(
    Sort *sort, data_dist_t distribution, data_t *keys, data_t *values, uint_t arrayLength, order_t sortOrder
)
{
    readArrayFromFile(FILE_UNSORTED_ARRAY, keys, arrayLength);

    // Allocates memory key only sort OR key-value sort
    if (sort->getSortType() == SORT_SEQUENTIAL_KEY_ONLY || sort->getSortType() == SORT_PARALLEL_KEY_ONLY)
    {
        sort->memoryAllocate(keys, arrayLength);
    }
    else
    {
        sort->memoryAllocate(keys, values, arrayLength);
    }

    // Memory data transfer for parallel sorts
    if (sort->getSortType() == SORT_PARALLEL_KEY_ONLY)
    {
        sort->memoryCopyHostToDevice(keys, arrayLength);
        cudaDeviceSynchronize();
    }
    else if (sort->getSortType() == SORT_PARALLEL_KEY_VALUE)
    {
        fillArrayValueOnly(values, arrayLength);
        sort->memoryCopyHostToDevice(keys, values, arrayLength);
        cudaDeviceSynchronize();
    }

    LARGE_INTEGER timer;
    startStopwatch(&timer);

    // Sort depending if it sorts only keys or key-value pairs
    if (sort->getSortType() == SORT_SEQUENTIAL_KEY_ONLY || sort->getSortType() == SORT_PARALLEL_KEY_ONLY)
    {
        sort->sort(keys, arrayLength, sortOrder);
    }
    else
    {
        sort->sort(keys, values, arrayLength, sortOrder);
    }

    // Waits for device to finish and transfers result to host
    if (sort->getSortType() == SORT_PARALLEL_KEY_ONLY || sort->getSortType() == SORT_PARALLEL_KEY_VALUE)
    {
        cudaError_t error = cudaDeviceSynchronize();
        checkCudaError(error);
        sort->memoryCopyDeviceToHost(keys, arrayLength);
    }

    double time = endStopwatch(timer);
    sort->memoryDestroy();

    return time;
}

/*
Times sort with stopwatch, checks if sort is stable and checks if sort is ordering data correctly, than saves
this statistics to file.
*/
void writeStatisticsToFile(
    Sort *sort, data_dist_t distribution, data_t *keys, data_t *values, uint_t arrayLength, order_t sortOrder,
    bool isLastTestRepetition
)
{
    double time = stopwatchSort(sort, distribution, keys, values, arrayLength, sortOrder);
    writeNumberToFile(sort, distribution, FOLDER_SORT_TIMERS, time, isLastTestRepetition);

    // Key-value sort has to be tested for stability
    if (sort->getSortType() == SORT_SEQUENTIAL_KEY_VALUE || sort->getSortType() == SORT_PARALLEL_KEY_VALUE)
    {
        bool isStable = isSortStable(keys, values, arrayLength);
        writeNumberToFile(sort, distribution, FOLDER_SORT_STABILITY, isStable, isLastTestRepetition);
    }

    // In order to use less space, array for values is used as container for correctly sorted array
    data_t *correctlySortedKeys = values;
    readArrayFromFile(FILE_SORTED_ARRAY, correctlySortedKeys, arrayLength);
    bool isSortingCorrectly = compareArrays(keys, correctlySortedKeys, arrayLength);
    writeNumberToFile(sort, distribution, FOLDER_SORT_CORRECTNESS, isSortingCorrectly, isLastTestRepetition);
}

/*
Tests all provided sorts for all provided distributions.
*/
void testSorts(
    std::vector<Sort*> sorts, std::vector<data_dist_t> distributions, uint_t arrayLenStart, uint_t arrayLenEnd,
    order_t sortOrder, uint_t testRepetitions, uint_t interval
)
{
    createFolderStructure(distributions);

    for (std::vector<data_dist_t>::iterator dist = distributions.begin(); dist != distributions.end(); dist++)
    {
        for (uint_t arrayLength = arrayLenStart; arrayLength <= arrayLenEnd; arrayLength *= 2)
        {
            data_t *keys = (data_t*)malloc(arrayLength * sizeof(*keys));
            checkMallocError(keys);
            data_t *values = (data_t*)malloc(arrayLength * sizeof(*values));
            checkMallocError(values);

            for (uint_t iter = 0; iter < testRepetitions; iter++)
            {
                printf("> Test iteration: %d\n", iter);
                printf("> Distribution: %s\n", getDistributionName(*dist));
                printf("> Array length: %d\n", arrayLength);
                printf("> Data type: %s\n", typeid(data_t).name());

                // All the sort algorithms have to sort the same array
                fillArrayKeyOnly(keys, arrayLength, interval, *dist);
                writeArrayToFile(FILE_UNSORTED_ARRAY, keys, arrayLength);
                sortCorrect(keys, arrayLength, sortOrder);
                writeArrayToFile(FILE_SORTED_ARRAY, keys, arrayLength);

                for (std::vector<Sort*>::iterator sort = sorts.begin(); sort != sorts.end(); sort++)
                {
                    writeStatisticsToFile(
                        *sort, *dist, keys, values, arrayLength, sortOrder, iter == testRepetitions - 1
                    );
                }

                printf("\n");
            }

            free(keys);
            free(values);
        }

        printf("\n\n");
    }
}


///*
//Prints out table vertical splitter if only keys are sorted.
//*/
//void printTableSplitterKeyOnly()
//{
//    printf("===================================================================================================================\n");
//}
//
///*
//Prints out table header if only keys are sorted.
//*/
//void printTableHeaderKeysOnly()
//{
//    printTableSplitterKeyOnly();
//    printf("||     # ||             PARALLEL              ||             SEQUENTIAL            ||            CORRECT         ||\n");
//    printTableSplitterKeyOnly();
//    printf("||     # ||     time    |      rate    |  ok  ||     time    |      rate    |  ok  ||     time    |      rate    ||\n");
//    printTableSplitterKeyOnly();
//}
//
///*
//Prints out table line with data if only keys are sorted.
//*/
//void printTableLineKeysOnly()
//{
//
//}

///*
//Prints out table vertical splitter if key-value pairs are sorted.
//*/
//void printTableSplitterKeyValue()
//{
//    printf("=====================================================================================================================================\n");
//}
//
///*
//Prints out table header if key-value pairs are sorted.
//*/
//void printTableHeaderKeyValue()
//{
//    printTableSplitterKeyValue();
//    printf("||     # ||                  PARALLEL                  ||                  SEQUENTIAL                ||            CORRECT         ||\n");
//    printTableSplitterKeyValue();
//    printf("||     # ||     time    |      rate    |  ok  | stable ||     time    |      rate    |  ok  | stable ||     time    |      rate    ||\n");
//    printTableSplitterKeyValue();
//}
//
///*
//Prints out table line with data if key-value pairs are sorted.
//*/
//void printTableLineKeyValue(
//    double **timers, uint_t iter, uint_t tableLen, bool areEqualParallel, bool areEqualSequential,
//    bool isStableParallel, bool isStableSequential
//)
//{
//    printf(
//        "|| %5d || %8.2lf ms | %8.2lf M/s | %s  |   %s  || %8.2lf ms | %8.2lf M/s | %s  |   %s  || %8.2lf ms | %8.2lf M/s ||\n",
//        iter + 1,
//        timers[SORT_PARALLEL][iter], tableLen / 1000.0 / timers[SORT_PARALLEL][iter],
//        areEqualParallel ? "YES" : " NO", isStableParallel ? "YES" : " NO",
//        timers[SORT_SEQUENTIAL][iter], tableLen / 1000.0 / timers[SORT_SEQUENTIAL][iter],
//        areEqualSequential ? "YES" : " NO", isStableSequential ? "YES" : " NO",
//        timers[SORT_CORRECT][iter], tableLen / 1000.0 / timers[SORT_CORRECT][iter]
//    );
//}
//
///*
//Prints out statistics of sort if only keys are sorted.
//*/
//void printStatisticsKeysOnly(double *timers, uint_t testRepetitions, uint_t tableLen, bool sortsCorrectly)
//{
//    double timeSum = 0;
//
//    for (uint_t i = 0; i < testRepetitions; i++)
//    {
//        timeSum += timers[i];
//    }
//
//    double avgTime = timeSum / (double)testRepetitions;
//    timeSum = 0;
//
//    for (uint_t i = 0; i < testRepetitions; i++)
//    {
//        timeSum += pow(avgTime - timers[i], 2);
//    }
//
//    double deviationTime = sqrt(timeSum);
//
//    printf("Average sort time:  %8.2lf ms\n", avgTime);
//    printf("Average sort rate:  %8.2lf M/s\n", tableLen / 1000.0 / avgTime);
//    printf("Standard deviation: %8.2lf ms  (%.2lf%%)\n", deviationTime, deviationTime / avgTime * 100);
//    printf("Sorting correctly:  %8s\n", sortsCorrectly ? "YES" : "NO");
//}
//
//void printStatisticsKeyValue(
//    double *timers, uint_t testRepetitions, uint_t tableLen, bool sortsCorrectly, bool isStable
//)
//{
//    printStatisticsKeysOnly(timers, testRepetitions, tableLen, sortsCorrectly);
//    printf("Is sort stable:     %8s\n", isStable ? "YES" : "NO");
//}
//
///*
//Returns a speedup for 2 provided sort types.
//*/
//double getSpeedup(double **timers, sort_type_t sortType1, sort_type_t sortType2, uint_t testRepetitions)
//{
//    double totalTimeSortType1 = 0;
//    double totalTimeSortType2 = 0;
//
//    for (uint_t i = 0; i < testRepetitions; i++)
//    {
//        totalTimeSortType1 += timers[sortType1][i];
//        totalTimeSortType2 += timers[sortType2][i];
//    }
//
//    return totalTimeSortType1 / totalTimeSortType2;
//}
//
///*