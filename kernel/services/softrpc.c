/**
 * @file kernel/services/softrpc.c
 * @brief SoftRPC: name -> (method id, handler) dispatch registry.
 *
 * Methods are statically registered descriptors; sai_softrpc_call() looks
 * the handler up and invokes it in the caller's thread.  A transport
 * (e.g. the shell or a UART line protocol) can sit on top of the same
 * registry.
 */
#include <sai/services.h>
#include <string.h>

static const sai_softrpc_method_t *s_methods[SAI_SOFTRPC_MAX_METHODS];
static uint32_t s_method_count;

sai_status_t sai_softrpc_register(const sai_softrpc_method_t *method)
{
    if (method == NULL || method->name == NULL || method->handler == NULL) {
        return SAI_ERR_INVAL;
    }
    if (strlen(method->name) >= SAI_SOFTRPC_NAME_MAX) {
        return SAI_ERR_BOUNDS;
    }
    for (uint32_t i = 0; i < s_method_count; i++) {
        if (strcmp(s_methods[i]->name, method->name) == 0) {
            return SAI_ERR_NOENT;       /* duplicate name */
        }
    }
    if (s_method_count >= SAI_SOFTRPC_MAX_METHODS) {
        return SAI_ERR_FULL;
    }
    s_methods[s_method_count++] = method;
    return SAI_OK;
}

const sai_softrpc_method_t *sai_softrpc_find(const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    for (uint32_t i = 0; i < s_method_count; i++) {
        if (strcmp(s_methods[i]->name, name) == 0) {
            return s_methods[i];
        }
    }
    return NULL;
}

sai_status_t sai_softrpc_call(const char *name,
                              uint32_t arg0, uint32_t arg1, int32_t *ret)
{
    const sai_softrpc_method_t *m = sai_softrpc_find(name);
    if (m == NULL) {
        return SAI_ERR_NOENT;
    }
    return m->handler(arg0, arg1, ret);
}

uint32_t sai_softrpc_count(void)
{
    return s_method_count;
}
