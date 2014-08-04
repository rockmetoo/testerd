//=============================================================================
//	File name: hashtable.c
//  Created on: October 26, 2010
//  Author: rockmetoo <tanvir@everconnect.biz>
//-----------------------------------------------------------------------------
#include "hashtable.h"

//=============================================================================
//	Credit for primes table: Aaron Krowne
//	http://br.endernet.org/~akrowne/
//	http://planetmath.org/encyclopedia/GoodHashTablePrimes.html
//-----------------------------------------------------------------------------
const	unsigned int primes[] = {
	53, 97, 193, 389,
	769, 1543, 3079, 6151,
	12289, 24593, 49157, 98317,
	196613, 393241, 786433, 1572869,
	3145739, 6291469, 12582917, 25165843,
	50331653, 100663319, 201326611, 402653189,
	805306457, 1610612741
};
const	unsigned int prime_table_length = sizeof(primes)/sizeof(primes[0]);
const	float max_load_factor = 0.65;
//=============================================================================
//	Local functions prototype
//-----------------------------------------------------------------------------
static		unsigned int	hashfromkey(void* ky);
static		int				equalkeys(void* k1, void* k2);
static		unsigned int	hash(struct hashtable* h, void* k);
static		unsigned int	indexFor(unsigned int tablelength, unsigned int hashvalue);
//=============================================================================
//
//-----------------------------------------------------------------------------
static unsigned int hashfromkey(void* ky){
    struct key* k = (struct key*)ky;
    return ((((uint32_t)k->index << 17) | ((uint32_t)k->index >> 15)));

}
//=============================================================================
//
//-----------------------------------------------------------------------------
static int equalkeys(void* k1, void* k2){
    return (0 == memcmp(k1, k2, sizeof(struct key)));
}
//=============================================================================
//
//-----------------------------------------------------------------------------
static unsigned int hash(struct hashtable* h, void* k){
    //Aim to protect against poor hash functions by adding logic here
    //logic taken from java 1.4 hashtable source
    unsigned int i = h->hashfn(k);
    i += ~(i << 9);
    i ^=  ((i >> 14) | (i << 18));
    i +=  (i << 4);
    i ^=  ((i >> 10) | (i << 22));
    return i;
}
//=============================================================================
//
//-----------------------------------------------------------------------------
static unsigned int indexFor(unsigned int tablelength, unsigned int hashvalue){
    return (hashvalue % tablelength);
}
//=============================================================================
//
//-----------------------------------------------------------------------------
struct hashtable* createHashTable(unsigned int minsize, unsigned int (*hashf) (void*), int (*eqf) (void*, void*)){
    struct hashtable* h;
    unsigned int pindex, size = primes[0];
    //Check requested hashtable isn't too large
    if(minsize > (1u << 30)) return NULL;
    //Enforce size as prime
    for(pindex=0; pindex < prime_table_length; pindex++){
        if(primes[pindex] > minsize) { size = primes[pindex]; break; }
    }
    h = (struct hashtable*)malloc(sizeof(struct hashtable));
    if(NULL == h) return NULL; //oom
    h->table = (struct entry**)malloc(sizeof(struct entry*) * size);
    if (NULL == h->table) { free(h); return NULL; } //oom
    memset(h->table, 0, size * sizeof(struct entry *));
    h->tablelength  = size;
    h->primeindex   = pindex;
    h->entrycount   = 0;
    h->hashfn       = hashf;
    h->eqfn         = eqf;
    h->loadlimit    = (unsigned int) ceil(size * max_load_factor);
    return h;
}
//=============================================================================
//
//-----------------------------------------------------------------------------
struct hashtable* createHash(unsigned int minsize){
	return createHashTable(minsize, hashfromkey, equalkeys);
}
//=============================================================================
//
//-----------------------------------------------------------------------------
int hashTableExpand(struct hashtable* h){
    //Double the size of the table to accomodate more entries
    struct entry** newtable;
    struct entry* e;
    struct entry** pE;
    unsigned int newsize, i, index;
    //Check we're not hitting max capacity
    if(h->primeindex == (prime_table_length - 1)) return 0;
    newsize = primes[++(h->primeindex)];

    newtable = (struct entry**)malloc(sizeof(struct entry*) * newsize);
    if(NULL != newtable){
        memset(newtable, 0, newsize * sizeof(struct entry*));
        //This algorithm is not 'stable'. ie. it reverses the list
        //when it transfers entries between the tables
        for(i = 0; i < h->tablelength; i++){
            while(NULL != (e = h->table[i])){
                h->table[i] = e->srNext;
                index = indexFor(newsize,e->h);
                e->srNext = newtable[index];
                newtable[index] = e;
            }
        }
        free(h->table);
        h->table = newtable;
    }
    //Plan B: realloc instead
    else{
        newtable = (struct entry**)realloc(h->table, newsize * sizeof(struct entry*));
        if(NULL == newtable) { (h->primeindex)--; return 0; }
        h->table = newtable;
        memset(newtable[h->tablelength], 0, newsize - h->tablelength);
        for(i = 0; i < h->tablelength; i++){
            for(pE = &(newtable[i]), e = *pE; e != NULL; e = *pE){
                index = indexFor(newsize,e->h);
                if(index == i){
                    pE = &(e->srNext);
                }else{
                    *pE = e->srNext;
                    e->srNext = newtable[index];
                    newtable[index] = e;
                }
            }
        }
    }
    h->tablelength = newsize;
    h->loadlimit = (unsigned int) ceil(newsize * max_load_factor);
    return -1;
}
//=============================================================================
//
//-----------------------------------------------------------------------------
struct key* hashTableKeyInsert(struct key* srKey, const char* pszIndex){
	srKey = (struct key*)malloc(sizeof(struct key));
    if(NULL == srKey){
        logError("hashTableKeyInsert: ran out of memory allocating a key in %s at %d\n", __FILE__, __LINE__);
        return (srKey);
    }
    srKey->index = string2Int((uint8_t*)pszIndex, strlen(pszIndex));
    return (srKey);
}
//=============================================================================
//
//-----------------------------------------------------------------------------
unsigned int hashTableCount(struct hashtable* h){
    return h->entrycount;
}
//=============================================================================
//
//-----------------------------------------------------------------------------
int hashTableInsert(struct hashtable* h, void* k, void* v){
    //This method allows duplicate keys - but they shouldn't be used
    unsigned int index;
    struct entry* e;
    if(++(h->entrycount) > h->loadlimit){
        //Ignore the return value. If expand fails, we should
        //still try cramming just this value into the existing table
        //we may not have memory for a larger table, but one more
        //element may be ok. Next time we insert, we'll try expanding again.
        hashTableExpand(h);
    }
    e = (struct entry*)malloc(sizeof(struct entry));
    if(NULL == e) { --(h->entrycount); return 0; } //oom
    e->h = hash(h,k);
    index = indexFor(h->tablelength, e->h);
    e->k = k;
    e->v = v;
    e->srNext = h->table[index];
    h->table[index] = e;
    return -1;
}
//=============================================================================
//
//-----------------------------------------------------------------------------
void* hashTableSearch(struct hashtable* h, void* k){
    struct entry* e;
    unsigned int hashvalue, index;
    hashvalue = hash(h, k);
    index = indexFor(h->tablelength, hashvalue);
    e = h->table[index];
    while(NULL != e){
        //Check hash value to short circuit heavier comparison
        if((hashvalue == e->h) && (h->eqfn(k, e->k))) return e->v;
        e = e->srNext;
    }
    return NULL;
}
//=============================================================================
//
//-----------------------------------------------------------------------------
void* hashTableRemove(struct hashtable* h, void* k){
    // TODO: consider compacting the table when the load factor drops enough,
    // *       or provide a 'compact' method.
    struct entry* e;
    struct entry** pE;
    void* v;
    unsigned int hashvalue, index;
    hashvalue = hash(h,k);
    index = indexFor(h->tablelength,hash(h,k));
    pE = &(h->table[index]);
    e = *pE;
    while(NULL != e){
        //Check hash value to short circuit heavier comparison
        if((hashvalue == e->h) && (h->eqfn(k, e->k))){
            *pE = e->srNext;
            h->entrycount--;
            v = e->v;
            FREE(e->k);
            FREE(e);
            return v;
        }
        pE = &(e->srNext);
        e = e->srNext;
    }
    return NULL;
}
//=============================================================================
//	hashtable_destroy
//-----------------------------------------------------------------------------
void hashTableDestroy(struct hashtable* h, int free_values){
    unsigned int i;
    struct entry* e;
    struct entry* f;
    struct entry** table = h->table;
    if(free_values){
        for(i = 0; i < h->tablelength; i++){
            e = table[i];
            while(NULL != e){
            	f = e; e = e->srNext;
            	FREE(f->k); FREE(f->v);
            	FREE(f);
            }
        }
    }else{
        for(i = 0; i < h->tablelength; i++){
            e = table[i];
            while(NULL != e){
            	f = e; e = e->srNext;
            	FREE(f->k); FREE(f);
            }
        }
    }
    FREE(h->table);
    FREE(h);
}
//=============================================================================
//hashTableIterator - iterator constructor
//-----------------------------------------------------------------------------
struct hashtable_itr* hashTableIterator(struct hashtable* h){
	unsigned int i, tablelength;
	struct hashtable_itr* itr;
    itr = (struct hashtable_itr *)malloc(sizeof(struct hashtable_itr));
    if(NULL == itr) return NULL;
    itr->h = h;
    itr->e = NULL;
    itr->parent = NULL;
    tablelength = h->tablelength;
    itr->index = tablelength;
    if(0 == h->entrycount) return itr;
    for (i = 0; i < tablelength; i++){
        if(NULL != h->table[i]){
            itr->e = h->table[i];
            itr->index = i;
            break;
        }
    }
    return itr;
}
//=============================================================================
//	hashTableIteratorKey - return the key of the (key,value)
//	pair at the current position
//-----------------------------------------------------------------------------
void* hashTableIteratorKey(struct hashtable_itr* i){
	return i->e->k;
}
//=============================================================================
//	hashTableIteratorValue‌‍ - return the value of the (key,value)
//	pair at the current position
//-----------------------------------------------------------------------------
void* hashTableIteratorValue(struct hashtable_itr* i){
	return i->e->v;
}
//=============================================================================
//	hashtableIteratorAdvance - advance the iterator to the next element
//	returns zero if advanced to end of table
//-----------------------------------------------------------------------------
int hashTableIteratorAdvance(struct hashtable_itr* itr){
    unsigned int j,tablelength;
    struct entry** table;
    struct entry* srNext;
    if(NULL == itr->e) return 0; //stupidity check
    srNext = itr->e->srNext;
    if(NULL != srNext){
        itr->parent = itr->e;
        itr->e = srNext;
        return -1;
    }
    tablelength = itr->h->tablelength;
    itr->parent = NULL;
    if(tablelength <= (j = ++(itr->index))){
        itr->e = NULL;
        return 0;
    }
    table = itr->h->table;
    while (NULL == (srNext = table[j])){
        if(++j >= tablelength){
            itr->index = tablelength;
            itr->e = NULL;
            return 0;
        }
    }
    itr->index = j;
    itr->e = srNext;
    return -1;
}
//=============================================================================
//	hashtableIteratorRemove - remove the entry at the current iterator position
//	and advance the iterator, if there is a successive
//	element.
//	If you want the value, read it before you remove:
//	beware memory leaks if you don't.
//	Returns zero if end of iteration.
//-----------------------------------------------------------------------------
int hashTableIteratorRemove(struct hashtable_itr* itr){
    struct entry *remember_e, *remember_parent;
    int ret;
    //Do the removal
    if(NULL == (itr->parent)){
        //element is head of a chain
        itr->h->table[itr->index] = itr->e->srNext;
    }else{
        //element is mid-chain
        itr->parent->srNext = itr->e->srNext;
    }
    //itr->e is now outside the hashtable
    remember_e = itr->e;
    itr->h->entrycount--;
    FREE(remember_e->k);
    //Advance the iterator, correcting the parent
    remember_parent = itr->parent;
    ret = hashTableIteratorAdvance(itr);
    if (itr->parent == remember_e) { itr->parent = remember_parent; }
    FREE(remember_e);
    return ret;
}
//=============================================================================
//
//-----------------------------------------------------------------------------
int hashTableIteratorSearch(struct hashtable_itr* itr, struct hashtable* h, void* k){
    struct entry *e, *parent;
    unsigned int hashvalue, index;
    hashvalue = hash(h,k);
    index = indexFor(h->tablelength, hashvalue);
    e = h->table[index];
    parent = NULL;
    while(NULL != e){
        //Check hash value to short circuit heavier comparison
        if((hashvalue == e->h) && (h->eqfn(k, e->k))){
            itr->index = index;
            itr->e = e;
            itr->parent = parent;
            itr->h = h;
            return -1;
        }
        parent = e;
        e = e->srNext;
    }
    return 0;
}
//=============================================================================
//	hashTableChange
//	function to change the value associated with a key, where there already
//	exists a value bound to the key in the hashtable.
//	Source due to Holger Schemel.
//-----------------------------------------------------------------------------
int hashTableChange(struct hashtable* h, void* k, void* v){
    struct entry* e;
    unsigned int hashvalue, index;
    hashvalue = hash(h,k);
    index = indexFor(h->tablelength, hashvalue);
    e = h->table[index];
    while(NULL != e){
        //Check hash value to short circuit heavier comparison
        if((hashvalue == e->h) && (h->eqfn(k, e->k))){
            FREE(e->v);
            e->v = v;
            return -1;
        }
        e = e->srNext;
    }
    return 0;
}
//=============================================================================
//End of program
//-----------------------------------------------------------------------------
