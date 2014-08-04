//=============================================================================
//	File name: hashtable.h
//	Description: gcc -O3 -J4 -c -o hashtable.o hashtable.c
//  Created on: October 26, 2010
//  Author: rockmetoo <tanvir@everconnect.biz>
//-----------------------------------------------------------------------------
#ifndef __hashtable_h__
#define __hashtable_h__

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "log.h"

struct hashtable;
struct key
{
	uint32_t		index;
};

struct entry
{
	void* 			k;
	void* 			v;
	unsigned int	h;
	struct entry*	srNext;
};

struct hashtable
{
	unsigned int	tablelength;
	struct entry**	table;
	unsigned int	entrycount;
	unsigned int	loadlimit;
	unsigned int	primeindex;
	unsigned int	(*hashfn)(void* k);
	int				(*eqfn)(void* k1, void* k2);
};

struct hashtable_itr
{
    struct hashtable*	h;
    struct entry*		e;
    struct entry*		parent;
    unsigned int		index;
};

extern	struct hashtable* 		createHashtable(
			unsigned int minsize, unsigned int (*hashfunction) (void*)
			, int (*key_eq_fn) (void*, void*)
		);
extern	struct hashtable*		createHash(unsigned int minsize);
extern	int						hashTableExpand(struct hashtable* h);
extern	struct key* 			hashTableKeyInsert(struct key* srKey, const char* pszIndex);
extern	int						hashTableInsert(struct hashtable* h, void* k, void* v);
extern	void*					hashTableSearch(struct hashtable* h, void* k);
extern	void*					hashTableRemove(struct hashtable* h, void* k);
extern	unsigned int			hashTableCount(struct hashtable* h);
extern	void					hashTableDestroy(struct hashtable *h, int free_values);
extern	struct hashtable_itr*	hashTableIterator(struct hashtable* h);
extern	void*					hashTableIteratorKey(struct hashtable_itr* i);
extern	void*					hashTableIteratorValue(struct hashtable_itr* i);
extern	int						hashTableIteratorAdvance(struct hashtable_itr* itr);
extern	int						hashTableIteratorRemove(struct hashtable_itr* itr);
extern	int						hashTableIteratorSearch(struct hashtable_itr* itr, struct hashtable* h, void* k);
#define DEFINE_HASHTABLE_ITERATOR_SEARCH(fnname, keytype) \
			int fnname (struct hashtable_itr *i, struct hashtable *h, keytype *k) \
			{ \
				return (hashTableIteratorSearch(i,h,k)); \
			}
extern	int						hashTableChange(struct hashtable* h, void* k, void* v);

#ifdef __cplusplus
}
#endif
#endif
//=============================================================================
//End of program
//-----------------------------------------------------------------------------
