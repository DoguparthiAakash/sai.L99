/**
 * @file drivers/virtual/memdev.c
 * @brief Memory "device": named scratch storage over a caller-owned buffer.
 *
 * Useful for tests, for the shell's kv store and for passing data between
 * languages (MicroPython <-> C) without dynamic allocation.
 */
#include <sai/devices2.h>
#include <sai/log.h>
#include <string.h>

#define SAI_MEMDEV_INSTANCES 2

typedef struct {
    sai_device_t dev;
    char         name[12];
    uint8_t     *storage;
    uint32_t     size;
    bool         used;
    sai_mem_ops_t ops;
} memdev_t;

static memdev_t s_memdevs[SAI_MEMDEV_INSTANCES];

static memdev_t *memdev_of(sai_device_t *d)
{
    for (uint32_t i = 0; i < SAI_MEMDEV_INSTANCES; i++) {
        if (&s_memdevs[i].dev == d) {
            return &s_memdevs[i];
        }
    }
    return NULL;
}

static int mem_read(sai_device_t *dev, uint32_t off, uint8_t *buf, uint32_t len)
{
    memdev_t *m = memdev_of(dev);
    if (m == NULL || buf == NULL || off > m->size || len > m->size - off) {
        return SAI_ERR_BOUNDS;
    }
    memcpy(buf, &m->storage[off], len);
    return SAI_OK;
}

static int mem_write(sai_device_t *dev, uint32_t off, const uint8_t *buf, uint32_t len)
{
    memdev_t *m = memdev_of(dev);
    if (m == NULL || buf == NULL || off > m->size || len > m->size - off) {
        return SAI_ERR_BOUNDS;
    }
    memcpy(&m->storage[off], buf, len);
    return SAI_OK;
}


sai_status_t sai_memdev_create(const char *name, void *storage, uint32_t size,
                               sai_device_t **out)
{
    if (name == NULL || storage == NULL || size == 0u || out == NULL) {
        return SAI_ERR_INVAL;
    }
    memdev_t *m = NULL;
    for (uint32_t i = 0; i < SAI_MEMDEV_INSTANCES; i++) {
        if (!s_memdevs[i].used) {
            m = &s_memdevs[i];
            break;
        }
    }
    if (m == NULL) {
        return SAI_ERR_FULL;
    }
    memset(m, 0, sizeof(*m));
    strncpy(m->name, name, sizeof(m->name) - 1u);
    m->storage = (uint8_t *)storage;
    m->size = size;
    m->ops.read = mem_read;
    m->ops.write = mem_write;
    m->ops.size = size;
    sai_status_t rc = sai_device_register(&m->dev, m->name,
                                          SAI_DEVICE_TYPE_MEM, 0, NULL, 0,
                                          (const sai_dev_ops_t *)&m->ops,
                                          NULL);
    if (rc != SAI_OK) {
        return rc;
    }
    m->used = true;
    *out = &m->dev;
    return SAI_OK;
}
