/**
 * hooker pool
 */

#include "hook.h"

static void* hookerThread(void*);
static void	hookerSetShutdown(HOOKER it, bool shutdown);


HOOKER newHooker(int size, int maxsize, bool block)
{
	int		x;
	int		c;
	HOOKER	this;

	if((this = calloc(sizeof(*this),1)) == NULL) return NULL;

	if((this->threads = (pthread_t *)malloc(sizeof(pthread_t)*size)) == NULL) return NULL;

	this->size     = size;
	this->maxsize  = maxsize;
	this->cursize  = 0;
	this->total    = 0;
	this->block    = block;
	this->head     = NULL;
	this->tail     = NULL;
	this->closed   = FALSE;
	this->shutdown = FALSE;

	if((c = pthread_mutex_init(&(this->lock), NULL)) != 0)		return NULL;
	if((c = pthread_cond_init(&(this->not_empty), NULL )) != 0)	return NULL;
	if((c = pthread_cond_init(&(this->not_full), NULL )) != 0)	return NULL;
	if((c = pthread_cond_init(&(this->empty), NULL)) != 0)		return NULL;

	for(x = 0; x != size; x++)
	{
		if((c = pthread_create(&(this->threads[x]), NULL, hookerThread, (void *)this)) != 0)
		return NULL;
	}

	return this;
}

static void* hookerThread(void* hooker)
{
	int		c;
	WORK*	workptr;
	HOOKER	this = (HOOKER) hooker;

	while(TRUE)
	{
		if((c = pthread_mutex_lock(&(this->lock))) != 0)
		{
			logCritical("mutex lock at file: %s, line:%d\n", __FILE__, __LINE__);
		}
		while((this->cursize == 0) && (!this->shutdown))
		{
			if((c = pthread_cond_wait(&(this->not_empty), &(this->lock))) != 0)
			{
				logCritical("pthread wait at file: %s, line:%d\n", __FILE__, __LINE__);
			}
		}

		if(this->shutdown == TRUE)
		{
			if((c = pthread_mutex_unlock(&(this->lock))) != 0)
			{
				logCritical("mutex unlock at file: %s, line:%d\n", __FILE__, __LINE__);
			}
			pthread_exit(NULL);
		}

		workptr = this->head;
		this->cursize--;

		if(this->cursize == 0)
		{
			this->head = this->tail = NULL;
		}
		else
		{
			this->head = workptr->next;
		}

		if((this->block) && (this->cursize == (this->maxsize - 1)))
		{
			if((c = pthread_cond_broadcast(&(this->not_full))) != 0)
			{
				logCritical("pthread broadcast at file: %s, line:%d\n", __FILE__, __LINE__);
			}
		}

		if(this->cursize == 0)
		{
			if((c = pthread_cond_signal(&(this->empty))) != 0)
			{
				logCritical("pthread signal at file: %s, line:%d\n", __FILE__, __LINE__);
			}
		}

		if((c = pthread_mutex_unlock(&(this->lock))) != 0)
		{
			logCritical("pthread unlock at file: %s, line:%d\n", __FILE__, __LINE__);
		}

		(*(workptr->routine))(workptr->arg);

		if(workptr != (void*) NULL)
		{
			free(workptr); workptr = (void*) NULL;
		}
	}

	return NULL;
}

bool hookerAdd(HOOKER hooker, void (*routine)(void*), void* arg)
{
	int c;
	WORK* workptr;

	if((c = pthread_mutex_lock(&(hooker->lock))) != 0)
	{
		logCritical("pthread lock at file: %s, line:%d\n", __FILE__, __LINE__);
	}

	if((hooker->cursize == hooker->maxsize) && !hooker->block )
	{
		if((c = pthread_mutex_unlock(&(hooker->lock))) != 0)
		{
			logCritical("pthread unlock at file: %s, line:%d\n", __FILE__, __LINE__);
		}

		return FALSE;
	}

	while((hooker->cursize == hooker->maxsize ) && (!(hooker->shutdown || hooker->closed)))
	{
		if((c = pthread_cond_wait(&(hooker->not_full), &(hooker->lock))) != 0)
		{
			logCritical("pthread wait at file: %s, line:%d\n", __FILE__, __LINE__);
		}
	}

	if(hooker->shutdown || hooker->closed)
	{
		if((c = pthread_mutex_unlock(&(hooker->lock))) != 0)
		{
			logCritical("pthread unlock at file: %s, line:%d\n", __FILE__, __LINE__);
		}

		return FALSE;
	}

	if((workptr = (WORK*) malloc(sizeof(WORK))) == NULL)
	{
		logCritical("out of memory at file: %s, line:%d\n", __FILE__, __LINE__);
	}

	workptr->routine = routine;
	workptr->arg     = arg;
	workptr->next    = NULL;

	if(hooker->cursize == 0)
	{
		hooker->tail = hooker->head = workptr;
		if((c = pthread_cond_broadcast(&(hooker->not_empty))) != 0)
		{
			logCritical("pthread signal at file: %s, line:%d\n", __FILE__, __LINE__);
		}
	}
	else
	{
		hooker->tail->next = workptr;
		hooker->tail       = workptr;
	}

	hooker->cursize++;
	hooker->total++;

	if((c = pthread_mutex_unlock(&(hooker->lock))) != 0)
	{
		logCritical("pthread unlock at file: %s, line:%d\n", __FILE__, __LINE__);
	}

	return TRUE;
}

bool hookerCancel(HOOKER this)
{
	int x;
	int size;

	/* XXX we store the size in a local
	variable because crew->size gets
	whacked when we cancel threads  */

	size = this->size;

	hookerSetShutdown(this, TRUE);

	for(x = 0; x < size; x++)
	{
		pthread_cancel(this->threads[x]);
	}

	return TRUE;
}

bool hookerJoin(HOOKER hooker, bool finish, void** payload)
{
	int    x;
	int    c;

	if((c = pthread_mutex_lock(&(hooker->lock))) != 0)
	{
		logCritical("pthread lock at file: %s, line:%d\n", __FILE__, __LINE__);
	}

	if(hooker->closed || hooker->shutdown)
	{
		if((c = pthread_mutex_unlock(&(hooker->lock))) != 0)
		{
			logCritical("pthread unlock at file: %s, line:%d\n", __FILE__, __LINE__);
		}

		return FALSE;
	}

	hooker->closed = TRUE;

	if(finish == TRUE)
	{
		while((hooker->cursize != 0) && (!hooker->shutdown))
		{
			int rc;
			struct timespec ts;
			struct timeval tp;

			rc = gettimeofday(&tp,NULL);

			if(rc != 0) logError("gettimeofday at file: %s, line:%d\n", __FILE__, __LINE__);

			ts.tv_sec	= tp.tv_sec + 60;
			ts.tv_nsec	= tp.tv_usec * 1000;

			rc = pthread_cond_timedwait(&(hooker->empty), &(hooker->lock), &ts );

			if(rc == ETIMEDOUT)
			{
				pthread_mutex_unlock(&hooker->lock);
			}

			if(rc != 0)
			{
				logCritical("pthread wait at file: %s, line:%d\n", __FILE__, __LINE__);
			}
		}
	}

	hooker->shutdown = TRUE;

	if((c = pthread_mutex_unlock(&(hooker->lock))) != 0)
	{
		logCritical("pthread_mutex_unlock at file: %s, line:%d\n", __FILE__, __LINE__);
	}

	if((c = pthread_cond_broadcast(&(hooker->not_empty))) != 0)
	{
		logCritical("pthread broadcast at file: %s, line:%d\n", __FILE__, __LINE__);
	}

	if((c = pthread_cond_broadcast(&(hooker->not_full))) != 0)
	{
		logCritical("pthread broadcast at file: %s, line:%d\n", __FILE__, __LINE__);
	}

	for(x = 0; x < hooker->size; x++)
	{
		if((c = pthread_join(hooker->threads[x], payload)) != 0)
		{
			logCritical("pthread_join at file: %s, line:%d\n", __FILE__, __LINE__);
		}
	}

	return TRUE;
}

void hookerDestroy(HOOKER hooker)
{
	WORK* workptr;

	if(hooker->threads != (void*) NULL)
	{
		free(hooker->threads); hooker->threads = (void*) NULL;
	}

	while(hooker->head != NULL)
	{
		workptr 		= hooker->head;
		hooker->head	= hooker->head->next;

		if(workptr != (void*) NULL)
		{
			free(workptr); workptr = (void*) NULL;
		}
	}

	if(hooker != (void*) NULL)
	{
		free(hooker); hooker = (void*) NULL;
	}
}

/**
 * getters and setters
 */
static void hookerSetShutdown(HOOKER this, bool shutdown)
{
	//pthread_mutex_lock(&this->lock);
	this->shutdown = shutdown;
	//pthread_mutex_unlock(&this->lock);

	pthread_cond_broadcast(&this->not_empty);
	pthread_cond_broadcast(&this->not_full);
	pthread_cond_broadcast(&this->empty);

	return;
}

int hookerGetSize(HOOKER this)
{
	return this->size;
}

int hookerGetTotal(HOOKER this)
{
	return this->total;
}

bool hookerGetShutdown(HOOKER this)
{
	return this->shutdown;
}
