ZRocks: RocksDB ZTL Target
==========================

This library contains the essential functions for RocksDB integration with ZNS devices.

Initialization / Shutdown
```
int zrocks_init (const char *dev_name);
int zrocks_exit (void);
```

Memory allocation
```
void *zrocks_alloc (size_t size);
void zrocks_free (void *ptr);
```

Object I/O interface
```
int zrocks_new (uint64_t id, void *buf, uint32_t size, uint8_t level);
int zrocks_delete (uint64_t id);
int zrocks_read_obj (uint64_t id, uint64_t offset, void *buf, uint32_t size);
```

Write / read interface
```
int zrocks_write (void *buf, uint32_t size, uint8_t level, uint64_t *addr);
int zrocks_read (uint64_t offset, void *buf, uint64_t size);
```
