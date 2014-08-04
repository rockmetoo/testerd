/**
 * hooker pool
 *
 */

#ifndef __hook_h__
#define __hook_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "log.h"
#include <pthread.h>
#include <stdbool.h>
#include <sys/time.h>

typedef struct work
{
	void			(*routine)();
	void*			arg;
	struct work*	next;
}WORK;

struct HOOK
{
	int				size;
	int				maxsize;
	int				cursize;
	int				total;
	WORK*			head;
	WORK*			tail;
	bool			block;
	bool			closed;
	bool			shutdown;
	pthread_t*		threads;
	pthread_mutex_t	lock;
	pthread_cond_t	not_empty;
	pthread_cond_t	not_full;
	pthread_cond_t	empty;
};

typedef struct HOOK* HOOKER;

HOOKER	newHooker(int size, int maxsize, bool block);

bool	hookerAdd(HOOKER it, void (*routine)(void*), void* arg);
bool	hookerCancel(HOOKER it);
bool	hookerJoin(HOOKER it, bool finish, void** payload);
void	hookerDestroy(HOOKER it);

int		hookerGetSize(HOOKER it);
int		hookerGetTotal(HOOKER it);
bool	hookerGetShutdown(HOOKER it);

#ifdef __cplusplus
}
#endif
#endif
