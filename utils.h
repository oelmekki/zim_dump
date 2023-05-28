#ifndef UTILS_H
#define UTILS_H

/*
 * Safely allocates memory.
 */
void *xalloc (size_t len);

/*
 * Safely reallocates memory.
 */
void *xrealloc (void *mem, size_t msize);

#endif
