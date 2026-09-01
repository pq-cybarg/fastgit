#include "fastgit/object.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

struct fastgit_blob {
    fastgit_object_t base;
};

fastgit_error_t fastgit_blob_create(const void* data, size_t len, fastgit_object_t** out) {
    fastgit_object_t* obj = calloc(1, sizeof(fastgit_object_t));
    if (!obj) return FASTGIT_ENOMEM;

    obj->type = FASTGIT_OBJ_BLOB;
    obj->size = len;
    obj->data = malloc(len);
    if (!obj->data) {
        free(obj);
        return FASTGIT_ENOMEM;
    }
    memcpy(obj->data, data, len);
    obj->free_data = free;

    *out = obj;
    return FASTGIT_OK;
}

fastgit_error_t fastgit_blob_create_from_file(const char* path, fastgit_object_t** out) {
    if (!path || !out) return FASTGIT_EINVAL;

#if defined(_WIN32)
    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FASTGIT_ENOENT;

    LARGE_INTEGER size;
    if (!GetFileSizeEx(hFile, &size)) {
        CloseHandle(hFile);
        return FASTGIT_EIO;
    }

    if (size.QuadPart > SIZE_MAX) {
        CloseHandle(hFile);
        return FASTGIT_EOVERFLOW;
    }

    size_t len = (size_t)size.QuadPart;
    void* data = malloc(len);
    if (!data) {
        CloseHandle(hFile);
        return FASTGIT_ENOMEM;
    }

    DWORD bytesRead;
    if (!ReadFile(hFile, data, (DWORD)len, &bytesRead, NULL) || bytesRead != len) {
        free(data);
        CloseHandle(hFile);
        return FASTGIT_EIO;
    }

    CloseHandle(hFile);
#else
    int fd = open(path, O_RDONLY);
    if (fd < 0) return FASTGIT_ENOENT;

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return FASTGIT_EIO;
    }

    if ((uint64_t)st.st_size > SIZE_MAX) {
        close(fd);
        return FASTGIT_EOVERFLOW;
    }

    size_t len = (size_t)st.st_size;
    void* data = malloc(len);
    if (!data) {
        close(fd);
        return FASTGIT_ENOMEM;
    }

    ssize_t n = read(fd, data, len);
    close(fd);

    if (n != (ssize_t)len) {
        free(data);
        return FASTGIT_EIO;
    }
#endif

    return fastgit_blob_create(data, len, out);
}

const void* fastgit_blob_data(const fastgit_object_t* obj) {
    if (!obj || obj->type != FASTGIT_OBJ_BLOB) return NULL;
    return obj->data;
}

size_t fastgit_blob_size(const fastgit_object_t* obj) {
    if (!obj || obj->type != FASTGIT_OBJ_BLOB) return 0;
    return obj->size;
}
