#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <pthread.h>
#include <mpi.h>

#define NUM_ROWS 1024
#define NUM_COLS 1024


/* Return the current time in seconds, using a double precision number. */
double When()
{
	struct timeval tp;
	gettimeofday(&tp, NULL);
	return ((double) tp.tv_sec + (double) tp.tv_usec * 1e-6);
}

int initArrays(float* ptrFrom, float* ptrTo)
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


int main(int argc, char* argv[])
{
	double starttime, finishtime, runtime;
	
	float* ptrFrom, *ptrTo;
	float* tempPtr;
	float* flFrom, *flTo;
	int rowStart, i, j, start, roof;
	float total, self;
	int nproc, iproc;
	int steadyState, iterCount, cells50;
	int sendInt, receiveInt;
	MPI_Request req1, req2;
	MPI_Status stat1, stat2;
	//float arrFrom[NUM_ROWS * NUM_COLS];
	//float arrTo[NUM_ROWS * NUM_COLS];
	float bufSend[NUM_COLS];
	float bufRecv[NUM_COLS];
			
	starttime = When();
	
	// putting these on the stack murders mpirun
	//ptrFrom = &arrFrom[0];
	//ptrTo = &arrTo[0];
	ptrFrom = (float*)malloc(NUM_ROWS * NUM_COLS * sizeof(float));
	ptrTo = (float*)malloc(NUM_ROWS * NUM_COLS * sizeof(float));
	
	
	initArrays(ptrFrom,ptrTo);
	steadyState = 0;
	
	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &nproc);
	MPI_Comm_rank(MPI_COMM_WORLD, &iproc);
	
	start = ((float)NUM_ROWS / (float)nproc) * (float)iproc;      // NUM_ROWS / numThreads is size of chunk work
    roof = start + ((float)NUM_ROWS / (float)nproc);
    
    //fprintf(stderr,"Proc(%d) Start: %d, Roof: %d\n", iproc, start, roof);
    
    if (start <= 0)     // skip first row
        start = 1;
    
    if (roof >= NUM_ROWS)   // skip last row
        roof = NUM_ROWS - 1;
	
	//fprintf(stderr,"Proc(%d) Revised - Start: %d, Roof: %d\n", iproc, start, roof);
	
	while (!steadyState)
	{
		steadyState = 1;
		
		if (start > 1) {
			rowStart = start * NUM_COLS;
			for (i = 0; i < NUM_COLS; i++) {
				bufSend[i] = ptrFrom[i + rowStart];
			}
			MPI_Isend(&bufSend,NUM_COLS,MPI_FLOAT,iproc - 1,0,MPI_COMM_WORLD,&req1);
			MPI_Recv(&bufRecv,NUM_COLS,MPI_FLOAT,iproc - 1,0,MPI_COMM_WORLD,&stat1);
			rowStart -= NUM_COLS;
			
			for (i = 0; i < NUM_COLS; i++) {
				ptrFrom[i + rowStart] = bufRecv[i];
			}

		}
		if (roof < (NUM_ROWS - 1)) {
			rowStart = (roof - 1) * NUM_COLS;		// potential hazard here for algorithm correctness
			for (i = 0; i < NUM_COLS; i++) {
				bufSend[i] = ptrFrom[i + rowStart];
			}
			MPI_Isend(&bufSend,NUM_COLS,MPI_FLOAT,iproc + 1,0,MPI_COMM_WORLD,&req2);
			MPI_Recv(&bufRecv,NUM_COLS,MPI_FLOAT,iproc + 1,0,MPI_COMM_WORLD,&stat2);
			rowStart += NUM_COLS;
			for (i = 0; i < NUM_COLS; i++) {
				ptrFrom[i + rowStart] = bufRecv[i];
			}
		}
		
					
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
					steadyState = 0;
					
			}
		}
		
		//MPI_Reduce(); with min (because 0 is important, not 1)
		sendInt = steadyState;
		MPI_Allreduce ( &sendInt, &receiveInt, 1,
					   MPI_INT, MPI_MIN, MPI_COMM_WORLD);
		steadyState = receiveInt;
		iterCount++;
		tempPtr = ptrFrom;
		ptrFrom = ptrTo;
		ptrTo = tempPtr;
	}	
	
		
	// my local count
	cells50 = 0;
	
	for (i = start; i < roof; i++)
	{
		flTo = ptrTo + i * NUM_COLS;
		
		for (j = 0; j < NUM_COLS; j++)
		{
			if (*(flTo + j) > 50)
				cells50++;
		}
			
	}

	sendInt = cells50;
	MPI_Allreduce ( &sendInt, &receiveInt, 1,
				   MPI_INT, MPI_SUM, MPI_COMM_WORLD);
	cells50 = receiveInt;
	
		
	
	if (iproc == 0) {
		finishtime = When();
		runtime = finishtime - starttime;
	
		fprintf(stdout,"%d says:\n",iproc);
		fprintf(stdout,"Number of iterations: %d\n", iterCount);
		fprintf(stdout,"Time taken: %f\n", runtime);
		fprintf(stdout,"Number of cells w/ temp greater than 50: %d\n", cells50);
		
	}
	
	//pthread_exit(NULL);
	MPI_Finalize();
}



















