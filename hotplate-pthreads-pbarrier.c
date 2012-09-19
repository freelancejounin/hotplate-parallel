#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <pthread.h>

#define NUM_ROWS 1024
#define NUM_COLS 1024

float arrFrom[NUM_ROWS * NUM_COLS];
float arrTo[NUM_ROWS * NUM_COLS];
float* ptrFrom;
float* ptrTo;

int steadyState, iterCount, cells50, numThreads;

pthread_barrier_t barrier_first;
pthread_barrier_t barrier_second;
pthread_mutex_t critical_count;
pthread_mutex_t critical_steady;

/* Return the current time in seconds, using a double precision number. */
double When()
{
	struct timeval tp;
	gettimeofday(&tp, NULL);
	return ((double) tp.tv_sec + (double) tp.tv_usec * 1e-6);
}

int initArrays()
{
	int i, j, rowStart;
	
	// inner square is 50s
	for (i = 1; i < NUM_ROWS - 1; i++)
	{
		rowStart = i * NUM_COLS;
		for (j = 1; j < NUM_COLS - 1; j++)
		{
			ptrFrom[rowStart + j] = 50;
			ptrTo[rowStart + j] = 50;
		}
	}
	//sides are 0
	for (i = 0; i < NUM_ROWS; i++)
	{
		rowStart = i * NUM_COLS;
		ptrFrom[rowStart] = 0; // row + 0 = 0
		ptrFrom[rowStart + NUM_COLS - 1] = 0;
		ptrTo[rowStart] = 0; // row + 0 = 0
		ptrTo[rowStart + NUM_COLS - 1] = 0;
	}
	// bottom is 100, top is 0
	rowStart = (NUM_ROWS - 1) * NUM_COLS;
	for (j = 0; j < NUM_COLS; j++)
	{
		ptrFrom[j] = 100; // 0th row, jth col
		ptrFrom[rowStart + j] = 0;
		ptrTo[j] = 100; // 0th row, jth col
		ptrTo[rowStart + j] = 0;
	}
	
	// special areas
	for (j = 0; j < 331; j++)
	{
		ptrFrom[400 * NUM_COLS + j] = 100;
		ptrTo[400 * NUM_COLS + j] = 100;
	}
	ptrFrom[200 * NUM_COLS + 500] = 100;
	ptrTo[200 * NUM_COLS + 500] = 100;
	
	return 0;
}

void* iterOverMyRows(void* myNum)
{
	// passed-in number will be starting i
	// go over row, change steadyState if necessary
	// take another row unless over NUM_ROWS
	// i += numprocs
	int rowStart, i, j, start, roof;
	float* flFrom, *flTo;
	float total, self;
	long t = *((long*)myNum);
    
    start = NUM_ROWS / numThreads * t;      // NUM_ROWS / numThreads is size of chunk work
    roof = start + (NUM_ROWS / numThreads);
    
    //printf("\nProc(%ld) Start: %d, Roof: %d", t, start, roof);
    
    if (start == 0)     // skip first row
        start = 1;
    
    if (roof == NUM_ROWS)   // skip last row
        roof = NUM_ROWS - 1;
 
    //printf("\nProc(%ld) Revised Start: %d, Roof: %d", t, start, roof);
    
	while (1)
	{
		pthread_barrier_wait(&barrier_first);
		
		for (i = start; i < roof; i++)
		{
			rowStart = i * NUM_COLS;
			flFrom = ptrFrom + rowStart;
			flTo = ptrTo + rowStart;

			//printf("I'm thread %ld, on row %d",t,i);
		
			for (j = 1; j < NUM_COLS - 1; j++)
			{
				if (*(flFrom + j) == 100)
					continue;

				total = *(flFrom - NUM_COLS + j) + *(flFrom + j-1) + *(flFrom + j+1) + *(flFrom + NUM_COLS + j);
				self = *(flFrom + j);
			
				*(flTo + j) = (total + 4 * self) / 8;

				if (steadyState && !(fabs(self - (total)/4) < 0.1))
				{
					pthread_mutex_lock(&critical_steady);
					steadyState = 0;
					pthread_mutex_unlock(&critical_steady);
				
				}

			}
		}

		pthread_barrier_wait(&barrier_second);
		
	}

	pthread_exit(NULL);
}


void * countCells50 (void* arg)
{
	long t = *((long*)arg);
	int myCells50;
	float* flTo;
	int i, j;
	
	// my local count
	myCells50 = 0;
	
	for (i = (int)t - 1; i < NUM_ROWS; i+=numThreads)
	{
		flTo = ptrTo + i * NUM_COLS;
		
		for (j = 0; j < NUM_COLS; j++)
		{
			if (*(flTo + j) > 50)
				myCells50++;
		}
		
	}
	
	// add my count to global count
	pthread_mutex_lock(&critical_count);
	cells50 += myCells50;
	pthread_mutex_unlock(&critical_count);
	
}

int main(int argc, char* argv[])
{
	double starttime, finishtime, runtime;
	// i know it doesn't scale, but double pointers are ugly
	long *taskids[32];
	pthread_t threads[32];
	int rc;
	long t;
	float* tempPtr;
		
	starttime = When();
	
	numThreads = atoi(argv[1]);
	
	if (numThreads > 32)
	{
		printf("\nCurrent code won't scale above 32 threads.");
	}
	
	ptrFrom = &arrFrom[0];
	ptrTo = &arrTo[0];
	
	
	initArrays();
	steadyState = 0;
	
	pthread_mutex_init(&critical_steady, NULL);
	pthread_barrier_init(&barrier_first,NULL,numThreads+1);
	pthread_barrier_init(&barrier_second,NULL,numThreads+1);
	
	for(t=0; t<numThreads; t++)
	{
		taskids[t] = (long *) malloc(sizeof(long));
		*taskids[t] = t;
		//printf("Creating thread %ld\n", t+1);
		rc = pthread_create(&threads[t], NULL, iterOverMyRows, (void *) taskids[t]);
	}
	
	while (!steadyState)
	{
		steadyState = 1;
    
		
		pthread_barrier_wait(&barrier_first);
		
		
		pthread_barrier_wait(&barrier_second);
		
		iterCount++;
		tempPtr = ptrFrom;
		ptrFrom = ptrTo;
		ptrTo = tempPtr;


	}	
	
	// start global count
	cells50 = 0;
	
	pthread_mutex_init(&critical_count, NULL);
	
	for(t=0; t<numThreads; t++)
	{
		taskids[t] = (long *) malloc(sizeof(long));
		*taskids[t] = t+1;
		//printf("Creating thread %ld\n", t+1);
		rc = pthread_create(&threads[t], NULL, countCells50, (void *) taskids[t]);
	}

	for(t=0;t<numThreads;t++)
	{
		rc = pthread_join(threads[t],NULL);
	}
	
	
	finishtime = When();
	
	printf("\nNumber of iterations: %d", iterCount);
	printf("\nNumber of cells w/ temp greater than 50: %d", cells50);
	
	runtime = finishtime - starttime;
	
	printf("\nTime taken: %f\n", runtime);
	
	//pthread_exit(NULL);
}



















