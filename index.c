#include "index.h"
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

// Forward declaration
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

#ifndef INDEX_PATH
#define INDEX_PATH ".pes/index"
#endif

// ─── PROVIDED FUNCTIONS ──────────────────────────────────────────────────────

IndexEntry* index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0)
            return &index->entries[i];
    }
    return NULL;
}

int index_remove(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            int remaining = index->count - i - 1;
            if (remaining > 0)
                memmove(&index->entries[i], &index->entries[i + 1],
                        remaining * sizeof(IndexEntry));
            index->count--;
            return index_save(index);
        }
    }
    fprintf(stderr, "error: '%s' is not in the index\n", path);
    return -1;
}

int index_status(const Index *index) {
    printf("Staged changes:\n");
    int staged_count = 0;
    for (int i = 0; i < index->count; i++) {
        printf("  staged:     %s\n", index->entries[i].path);
        staged_count++;
    }
    if (staged_count == 0) printf("  (nothing to show)\n");
    printf("\n");

    printf("Unstaged changes:\n");
    int unstaged_count = 0;
    for (int i = 0; i < index->count; i++) {
        struct stat st;
        if (stat(index->entries[i].path, &st) != 0) {
            printf("  deleted:    %s\n", index->entries[i].path);
            unstaged_count++;
        } else {
            if (st.st_mtime != (time_t)index->entries[i].mtime_sec || st.st_size != (off_t)index->entries[i].size) {
                printf("  modified:   %s\n", index->entries[i].path);
                unstaged_count++;
            }
        }
    }
    if (unstaged_count == 0) printf("  (nothing to show)\n");
    printf("\n");

    printf("Untracked files:\n");
    int untracked_count = 0;
    DIR *dir = opendir(".");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            if (strcmp(ent->d_name, ".pes") == 0 || strcmp(ent->d_name, "pes") == 0) continue;
            if (strstr(ent->d_name, ".o") != NULL) continue;

            int is_tracked = 0;
            for (int i = 0; i < index->count; i++) {
                if (strcmp(index->entries[i].path, ent->d_name) == 0) {
                    is_tracked = 1; break;
                }
            }
            if (!is_tracked) {
                struct stat st;
                if (stat(ent->d_name, &st) == 0 && S_ISREG(st.st_mode)) {
                    printf("  untracked:  %s\n", ent->d_name);
                    untracked_count++;
                }
            }
        }
        closedir(dir);
    }
    if (untracked_count == 0) printf("  (nothing to show)\n");
    printf("\n");
    return 0;
}

// ─── IMPLEMENTED TODOs ───────────────────────────────────────────────────────

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
        if (fscanf(f, "%o %64s %lu %u %511s\n", 
                   &e->mode, hex, &e->mtime_sec, &e->size, e->path) != 5) break;
        hex_to_hash(hex, &e->hash);
        index->count++;
    }
    fclose(f);
    return 0;
}

int index_save(const Index *index) {
    qsort((void*)index->entries, index->count, sizeof(IndexEntry), compare_entries);
    char temp_path[512];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", INDEX_PATH);
    FILE *f = fopen(temp_path, "w");
    if (!f) return -1;
    for (int i = 0; i < index->count; i++) {
        char hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&index->entries[i].hash, hex);
        fprintf(f, "%o %s %lu %u %s\n",
                index->entries[i].mode, hex, 
                index->entries[i].mtime_sec, index->entries[i].size, index->entries[i].path);
    }
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    if (rename(temp_path, INDEX_PATH) < 0) { unlink(temp_path); return -1; }
    return 0;
}

int index_add(Index *index, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint8_t *data = malloc(st.st_size);
    if (st.st_size > 0 && !data) { fclose(f); return -1; }
    if (st.st_size > 0 && fread(data, 1, st.st_size, f) != (size_t)st.st_size) {
        fclose(f); free(data); return -1;
    }
    fclose(f);
    ObjectID blob_hash;
    if (object_write(OBJ_BLOB, data, st.st_size, &blob_hash) != 0) {
        free(data); return -1;
    }
    free(data);
    IndexEntry *e = index_find(index, path);
    if (!e) {
        if (index->count >= MAX_INDEX_ENTRIES) return -1;
        e = &index->entries[index->count++];
        strncpy(e->path, path, sizeof(e->path) - 1);
        e->path[sizeof(e->path) - 1] = '\0';
    }
    e->mode = st.st_mode; e->hash = blob_hash;
    e->mtime_sec = (uint64_t)st.st_mtime; e->size = (uint32_t)st.st_size;
    return index_save(index);
}