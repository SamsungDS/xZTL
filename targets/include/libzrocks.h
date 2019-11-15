#ifndef LIBZROCKS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

#define ZNS_ALIGMENT 4096

/**
 * Initialize zrocks library
 */
int zrocks_init (void);

/**
 * Close zrocks library
 */
int zrocks_exit (void);

/**
 * Allocate an aligned buffer for DMA
 */
void *zrocks_alloc (size_t size);

/**
 * Free a buffer allocated by zrocks_alloc
 */
void zrocks_free (void *ptr);

/**
 * Create a new variable-sized object belonging to a certain level
 */
int zrocks_new (uint64_t id, void *buf, uint32_t size, uint8_t level);

/**
 * Delete an object
 */
int zrocks_delete (uint64_t id);

/**
 * Read an offset within an object */
int zrocks_read_obj (uint64_t id, uint64_t offset, void *buf, uint32_t size);

/**
 * Write to ZNS device and return the physical offset list
 * This function is used if the application is responsible for recovery
 */
int zrocks_write (void *buf, uint32_t size, uint8_t level, uint64_t *addr);

/**
 * Read from ZNS device from a physical offset
 * This fuinction is used if the application is responsible for recovery
 */
int zrocks_read (uint64_t offset, void *buf, uint64_t size);

#ifdef __cplusplus
}; // closing brace for extern "C"
#endif

#endif // LIBZROCKS_H
