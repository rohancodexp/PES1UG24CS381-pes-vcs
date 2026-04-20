#include "index.h"
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

static int compare_entries(const void *a, const void *b) {
    return strcmp(((IndexEntry*)a)->path, ((IndexEntry*)b)->path);
}

int index_load(Index *index) {
    index->count = 0;
    FILE *f = fopen(INDEX_PATH, "r");
    if (!f) return 0;

    char hex[HASH_HEX_SIZE + 1];
    while (index->count < MAX_INDEX_ENTRIES) {
        IndexEntry *e = &index->entries[index->count];
        if (fscanf(f, "%o %64s %ld %ld %1023s\n", 
                   &e->mode, hex, &e->mtime_sec, &e->size, e->path) != 5) {
            break;
        }
        hex_to_hash(hex, &e->id);
        index->count++;
    }
    fclose(f);
    return 0;
}

int index_save(const Index *index) {
    qsort((void*)index->entries, index->count, sizeof(IndexEntry), compare_entries);
    char temp_path[] = INDEX_PATH ".tmp";
    FILE *f = fopen(temp_path, "w");
    if (!f) return -1;

    for (int i = 0; i < index->count; i++) {
        char hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&index->entries[i].id, hex);
        fprintf(f, "%o %s %ld %ld %s\n",
                index->entries[i].mode, hex, 
                index->entries[i].mtime_sec, 
                index->entries[i].size, 
                index->entries[i].path);
    }
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    if (rename(temp_path, INDEX_PATH) < 0) {
        unlink(temp_path);
        return -1;
    }
    return 0;
}

int index_add(Index *index, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint8_t *data = malloc(st.st_size);
    if (fread(data, 1, st.st_size, f) != (size_t)st.st_size) {
        fclose(f); free(data); return -1;
    }
    fclose(f);

    ObjectID id;
    if (object_write(OBJ_BLOB, data, st.st_size, &id) != 0) {
        free(data); return -1;
    }
    free(data);

    IndexEntry *e = index_find(index, path);
    if (!e) {
        if (index->count >= MAX_INDEX_ENTRIES) return -1;
        e = &index->entries[index->count++];
        strncpy(e->path, path, sizeof(e->path) - 1);
    }
    e->mode = st.st_mode; e->id = id; e->mtime_sec = st.st_mtime; e->size = st.st_size;
    return index_save(index);
}