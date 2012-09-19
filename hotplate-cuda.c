#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <cuda.h>

#define NUM_ROWS 1024
#define NUM_COLS 1024

/* Return the current time in seconds, using a double precision number. */
double When()
{
	struct timeval tp;
	gettimeofday(&tp, NULL);
	return ((double) tp.tv_sec + (double) tp.tv_usec * 1e-6);
}

// dr. snell's reduce code
#ifdef __DEVICE_EMULATION__
#define EMUSYNC __syncthreads()
#else
#define EMUSYNC
#endif
__global__ void reduceSum1(int *g_idata, int *g_odata)
{
    extern __shared__ int sdata[];
	
    // perform first level of reduction,
    // reading from global memory, writing to shared memory
    unsigned int tid = threadIdx.x;
    unsigned int i = blockIdx.x*(blockDim.x*2) + threadIdx.x;
    sdata[tid] = g_idata[i] & g_idata[i+blockDim.x];
    __syncthreads();
	
    // do reduction in shared mem
    for(unsigned int s=blockDim.x/2; s>32; s>>=1)
    {
        if (tid < s)
        {
            sdata[tid] &= sdata[tid + s];
        }
        __syncthreads();
    }
	
#ifndef __DEVICE_EMULATION__
    if (tid < 32)
#endif
    {
        sdata[tid] &= sdata[tid + 32]; EMUSYNC;
        sdata[tid] &= sdata[tid + 16]; EMUSYNC;
        sdata[tid] &= sdata[tid +  8]; EMUSYNC;
        sdata[tid] &= sdata[tid +  4]; EMUSYNC;
        sdata[tid] &= sdata[tid +  2]; EMUSYNC;
        sdata[tid] &= sdata[tid +  1]; EMUSYNC;
    }
	
    // write result for this block to global mem
    if (tid == 0) *g_odata = sdata[0];
}





__global__ void initArrays(float *from, float *to, int size) {
	int idx = blockDim.x * blockIdx.x + threadIdx.x;
	
	if (idx < size) {
		
		// inner square is 50s
		if ((blockIdx.x > 0) && (blockIdx.x < NUM_ROWS)){
			if ((threadIdx.x > 0) && (threadIdx.x < NUM_COLS)) {
				from[idx] = 50;
				to[idx] = 50;
			}
		}
		
		// sides are 0
		if ((threadIdx.x == 0) || (threadIdx.x == 1023)) {
			from[idx] = 0;
			to[idx] = 0;
		}
		
		// top is 0
		if (blockIdx.x == 0) {
			from[idx] = 0;
			to[idx] = 0;
		}
		
		// bottom is 100
		if (blockIdx.x == 1023) {
			from[idx] = 100;
			to[idx] = 100;
		}
		
		// special areas
		if (blockIdx.x == 400) {
			if (threadIdx.x < 331) {
				from[idx] = 0;
				to[idx] = 0;
			}
		}
		if ((blockIdx.x == 200) && (threadIdx.x == 500)) {
			from[idx] = 0;
			to[idx] = 0;
		}
		
	}
}

__global__ void resetGData(int *g_idata, int *g_odata) {
	g_idata[threadIdx.x] = 1;
	if (threadIdx.x == 0)
		*g_odata = 0;
}


__global__ void calculate(float *from, float *to, int size, int * g_idata) {
	int idx = blockDim.x * blockIdx.x + threadIdx.x;
	float total;
	float self;
	
	if (idx < size) {
		//to[idx] = from[idx];
		
		if ((from[idx] == 100) || from[idx] == 0) {
			//g_idata[blockIdx.x] = 1;
			return;
		}
		
		total = from[idx - NUM_COLS] + from[idx-1] + from[idx + 1] + from[idx + NUM_COLS];
		self = from[idx];
		
		to[idx] = (total + 4 * self) / 8;
		
		if (!(fabs(self - (total)/4) < 0.1)) {
			g_idata[blockIdx.x] = 0;
		}
		
	}
}



int main(void) {
	double timestart, timefinish, timetaken; // host data
	float *from_d, *to_d;	// device data
	float *temp_d;
	int *g_idata, *g_odata;	// more device data
	int N;
	int nBytes;
//	int	keepGoing;
//	int keep;
	int count;
	int size, blocks, threadsperblock;
	int *steadyState;

	N = NUM_ROWS * NUM_COLS;
	
	size = N;
	blocks = 1024;
	threadsperblock = 1024;
	steadyState = (int*)malloc(sizeof(int));
	*steadyState = 0;

	nBytes = N*sizeof(float);
	cudaMalloc((void **) &from_d, nBytes);
	cudaMalloc((void **) &to_d, nBytes);
	cudaMalloc((void **) &g_idata, blocks * sizeof(int));
	cudaMalloc((void **) &g_odata, sizeof(int));

	initArrays<<<blocks,threadsperblock>>> (from_d, to_d, size);
	
	timestart = When();
	
	cudaError_t error = cudaGetLastError();
	if(error != cudaSuccess) {
		printf("%s\n",cudaGetErrorString(error));
		return 0;
	}
	
	count = 0;
	while (!*steadyState) {
		
		resetGData<<<1,blocks>>> (g_idata, g_odata);
		error = cudaGetLastError();
		if(error != cudaSuccess) {
			printf("%s\n",cudaGetErrorString(error));
			break;
		}
	
		calculate<<<blocks,threadsperblock>>> (from_d, to_d, size, g_idata);
		error = cudaGetLastError();
		if(error != cudaSuccess) {
			printf("%s\n",cudaGetErrorString(error));
			break;
		}
		
		reduceSum1<<<1,blocks, blocks*sizeof(int)>>> (g_idata, g_odata);
		error = cudaGetLastError();
		if(error != cudaSuccess) {
			printf("%s\n",cudaGetErrorString(error));
			break;
		}
	
		cudaMemcpy(steadyState, g_odata, sizeof(int), cudaMemcpyDeviceToHost);
		error = cudaGetLastError();
		if(error != cudaSuccess) {
			printf("%s\n",cudaGetErrorString(error));
			break;
		}
		
		count++;
		temp_d = from_d;
		from_d = to_d;
		to_d = temp_d;
		
		//printf("Steady State? %d\n",*steadyState);
	}
	
/*	if (keepGoing < N)
		keep = 1;
	
	printf("KeepGoing? %d\n",keepGoing);
	printf("Keep: %d\n",keep);
*/
	timefinish = When();

	cudaFree(from_d); cudaFree(to_d);
	timetaken = timefinish - timestart;

	printf("Finished! with count: %d and time taken: %f\n",count,timetaken);

	return 0;
}
