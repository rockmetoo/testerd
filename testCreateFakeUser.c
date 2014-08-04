/*
 * example.c
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "threadPool.h"
#include "helper.h"


#define ARR_SIZE 100


void createFakeUser(void *ptr)
{
	chdir("/home/tanvir/kanda/test/");
	//int status = commandExecute("NODE_ENV=local node createFakeUser.js");
	system("NODE_ENV=local node createFakeUser.js");
}

void* createFakeUser1(void *ptr)
{
	chdir("/home/tanvir/kanda/test/");
	//int status = commandExecute("NODE_ENV=local node createFakeUser.js");
	system("NODE_ENV=local node createFakeUser.js");

	pthread_exit(NULL);
}

int main(int argc, char **argv)
{
	struct threadpool *pool;
	int i, ret, failed_count = 0;
	pthread_t threads[ARR_SIZE];

	/* Create a threadpool of 200 thread workers. */
	/*if((pool = threadpoolInit(200)) == NULL)
	{
		printf("Error! Failed to create a thread pool struct.\n");
		exit(EXIT_FAILURE);
	}*/

	for(i = 0; i < ARR_SIZE; i++)
	{

		pthread_create(&threads[i], NULL, createFakeUser1, NULL);



		/* non blocking if 0. */
		/*ret = threadpoolAddTask(pool, createFakeUser, NULL, 0);

		if(ret == -1)
		{
			printf("An error had occurred while adding a task.");
			exit(EXIT_FAILURE);
		}

		if(ret == -2)
		{
			failed_count++;
		}*/
	}

	/* Stop the pool. */
	//threadpoolFree(pool, 1);

	printf("Example ended.\n");
	pthread_exit(NULL);
	return 0;
}
