/**
 * @file kernel/services/lang_registry.c
 * @brief Language-runtime registry: maps payload language -> backend.
 *
 * The registry is a singly linked list of statically declared backends.
 * The MicroPython backend registers itself at init (lang/mp_backend.c);
 * C/C++/asm/Rust go through the "native" backend (lang/native_backend.c).
 */
#include <sai/services.h>
#include <string.h>

static sai_lang_backend_t *s_backends;

static const char *s_lang_names[SAI_LANG_COUNT] = {
    "micropython", "c", "cpp", "asm", "rust",
};

sai_status_t sai_lang_register(sai_lang_backend_t *backend)
{
    if (backend == NULL || backend->name == NULL ||
        backend->run == NULL || backend->lang >= SAI_LANG_COUNT) {
        return SAI_ERR_INVAL;
    }
    for (sai_lang_backend_t *b = s_backends; b != NULL; b = b->next) {
        if (b == backend || b->lang == backend->lang ||
            strcmp(b->name, backend->name) == 0) {
            return SAI_ERR_NOENT;      /* duplicate */
        }
    }
    backend->next = s_backends;
    s_backends = backend;
    return SAI_OK;
}

sai_lang_backend_t *sai_lang_get(sai_lang_t lang)
{
    for (sai_lang_backend_t *b = s_backends; b != NULL; b = b->next) {
        if (b->lang == lang) {
            return b;
        }
    }
    return NULL;
}

sai_lang_backend_t *sai_lang_get_by_name(const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    for (sai_lang_backend_t *b = s_backends; b != NULL; b = b->next) {
        if (strcmp(b->name, name) == 0) {
            return b;
        }
    }
    return NULL;
}

sai_lang_t sai_lang_from_name(const char *name)
{
    for (int i = 0; i < (int)SAI_LANG_COUNT; i++) {
        if (name != NULL && strcmp(name, s_lang_names[i]) == 0) {
            return (sai_lang_t)i;
        }
    }
    return SAI_LANG_MICROPYTHON;
}

const char *sai_lang_name(sai_lang_t lang)
{
    if ((int)lang < 0 || lang >= SAI_LANG_COUNT) {
        return "?";
    }
    return s_lang_names[lang];
}
