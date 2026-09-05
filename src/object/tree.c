#include "fastgit/object.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct fastgit_tree {
    fastgit_object_t base;
    fastgit_tree_entry_t* entries;
    size_t count;
    size_t capacity;
};

static int tree_entry_cmp(const void* a, const void* b) {
    const fastgit_tree_entry_t* ea = (const fastgit_tree_entry_t*)a;
    const fastgit_tree_entry_t* eb = (const fastgit_tree_entry_t*)b;
    return strcmp(ea->path, eb->path);
}

static void tree_entry_free(fastgit_tree_entry_t* entry) {
    if (entry) {
        free(entry->path);
    }
}

static void tree_data_free(void* p) {
    struct fastgit_tree* tree = (struct fastgit_tree*)p;
    if (!tree) return;
    for (size_t i = 0; i < tree->count; i++) free(tree->entries[i].path);
    free(tree->entries);
    free(tree);
}

fastgit_error_t fastgit_tree_create(fastgit_object_t** out) {
    fastgit_object_t* obj = calloc(1, sizeof(fastgit_object_t));
    if (!obj) return FASTGIT_ENOMEM;
    struct fastgit_tree* tree = calloc(1, sizeof(struct fastgit_tree));
    if (!tree) { free(obj); return FASTGIT_ENOMEM; }
    tree->entries = NULL;
    tree->count = 0;
    tree->capacity = 0;
    obj->type = FASTGIT_OBJ_TREE;
    obj->size = 0;
    obj->data = tree;
    obj->free_data = tree_data_free;
    *out = obj;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_tree_add_entry(fastgit_object_t* tree_obj, const fastgit_tree_entry_t* entry) {
    if (!tree_obj || tree_obj->type != FASTGIT_OBJ_TREE || !entry) return FASTGIT_EINVAL;

    struct fastgit_tree* tree = (struct fastgit_tree*)tree_obj->data;

    if (tree->count >= tree->capacity) {
        size_t new_cap = tree->capacity ? tree->capacity * 2 : 16;
        fastgit_tree_entry_t* new_entries = realloc(tree->entries, new_cap * sizeof(fastgit_tree_entry_t));
        if (!new_entries) return FASTGIT_ENOMEM;
        tree->entries = new_entries;
        tree->capacity = new_cap;
    }

    fastgit_tree_entry_t* e = &tree->entries[tree->count++];
    e->oid = entry->oid;
    e->mode = entry->mode;
    e->path = strdup(entry->path);
    if (!e->path) {
        tree->count--;
        return FASTGIT_ENOMEM;
    }

    qsort(tree->entries, tree->count, sizeof(fastgit_tree_entry_t), tree_entry_cmp);
    return FASTGIT_OK;
}

fastgit_error_t fastgit_tree_remove_entry(fastgit_object_t* tree_obj, const char* path) {
    if (!tree_obj || tree_obj->type != FASTGIT_OBJ_TREE || !path) return FASTGIT_EINVAL;

    struct fastgit_tree* tree = (struct fastgit_tree*)tree_obj->data;

    for (size_t i = 0; i < tree->count; i++) {
        if (strcmp(tree->entries[i].path, path) == 0) {
            tree_entry_free(&tree->entries[i]);
            memmove(&tree->entries[i], &tree->entries[i + 1], (tree->count - i - 1) * sizeof(fastgit_tree_entry_t));
            tree->count--;
            return FASTGIT_OK;
        }
    }
    return FASTGIT_ENOENT;
}

size_t fastgit_tree_entry_count(const fastgit_object_t* tree_obj) {
    if (!tree_obj || tree_obj->type != FASTGIT_OBJ_TREE) return 0;
    struct fastgit_tree* tree = (struct fastgit_tree*)tree_obj->data;
    return tree->count;
}

const fastgit_tree_entry_t* fastgit_tree_entry_by_index(const fastgit_object_t* tree_obj, size_t index) {
    if (!tree_obj || tree_obj->type != FASTGIT_OBJ_TREE) return NULL;
    struct fastgit_tree* tree = (struct fastgit_tree*)tree_obj->data;
    if (index >= tree->count) return NULL;
    return &tree->entries[index];
}

const fastgit_tree_entry_t* fastgit_tree_entry_by_name(const fastgit_object_t* tree_obj, const char* path) {
    if (!tree_obj || tree_obj->type != FASTGIT_OBJ_TREE || !path) return NULL;
    struct fastgit_tree* tree = (struct fastgit_tree*)tree_obj->data;

    fastgit_tree_entry_t key = { .path = (char*)path };
    return bsearch(&key, tree->entries, tree->count, sizeof(fastgit_tree_entry_t), tree_entry_cmp);
}

static size_t tree_serialize_size(const fastgit_tree_entry_t* entries, size_t count) {
    size_t size = 0;
    for (size_t i = 0; i < count; i++) {
        char mode_str[8];
        int mode_len = snprintf(mode_str, sizeof(mode_str), "%o", entries[i].mode);
        size += mode_len + 1 + strlen(entries[i].path) + 1 + entries[i].oid.len;
    }
    return size;
}

static void tree_serialize_write(const fastgit_tree_entry_t* entries, size_t count, uint8_t* buf, size_t* pos) {
    for (size_t i = 0; i < count; i++) {
        char mode_str[8];
        int mode_len = snprintf(mode_str, sizeof(mode_str), "%o", entries[i].mode);
        memcpy(buf + *pos, mode_str, mode_len);
        *pos += mode_len;
        buf[(*pos)++] = ' ';
        size_t path_len = strlen(entries[i].path);
        memcpy(buf + *pos, entries[i].path, path_len);
        *pos += path_len;
        buf[(*pos)++] = '\0';
        memcpy(buf + *pos, entries[i].oid.hash, entries[i].oid.len);
        *pos += entries[i].oid.len;
    }
}

size_t fastgit_tree_content_size(const struct fastgit_tree* t) {
    if (!t) return 0;
    return tree_serialize_size(t->entries, t->count);
}
void fastgit_tree_content_write(const struct fastgit_tree* t, uint8_t* buf, size_t* pos) {
    if (!t || !buf || !pos) return;
    tree_serialize_write(t->entries, t->count, buf, pos);
}
