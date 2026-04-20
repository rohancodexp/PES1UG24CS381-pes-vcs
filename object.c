#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <errno.h>

// ─── PROVIDED FUNCTIONS (Do not remove) ──────────────────────────────────────

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        id_out->hash[i] = (uint8_t)byte;
    }
    return 0;
}

void compute_hash(const void *data, size_t len, ObjectID *id_out) {
    unsigned int hash_len;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, id_out->hash, &hash_len);
    EVP_MD_CTX_free(ctx);
}

void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
}

int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

// ─── IMPLEMENTED TODOs ──────────────────────────────────────────────────────

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    char header[64];
    const char *type_str;

    switch (type) {
        case OBJ_BLOB:   type_str = "blob"; break;
        case OBJ_TREE:   type_str = "tree"; break;
        case OBJ_COMMIT: type_str = "commit"; break;
        default: return -1;
    }

    int header_len = sprintf(header, "%s %zu", type_str, len) + 1;
    size_t full_len = header_len + len;
    uint8_t *full_obj = malloc(full_len);
    if (!full_obj) return -1;

    memcpy(full_obj, header, header_len);
    memcpy(full_obj + header_len, data, len);

    compute_hash(full_obj, full_len, id_out);

    if (object_exists(id_out)) {
        free(full_obj);
        return 0;
    }

    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id_out, hex);
    
    char shard_dir[512];
    snprintf(shard_dir, sizeof(shard_dir), "%s/%.2s", OBJECTS_DIR, hex);
    mkdir(shard_dir, 0755);

    char path[512];
    object_path(id_out, path, sizeof(path));

    char temp_path[524]; // Slightly larger to avoid truncation warnings
    snprintf(temp_path, sizeof(temp_path), "%s/temp_XXXXXX", shard_dir);
    
    int fd = mkstemp(temp_path);
    if (fd < 0) {
        free(full_obj);
        return -1;
    }

    if (write(fd, full_obj, full_len) != (ssize_t)full_len) {
        close(fd);
        unlink(temp_path);
        free(full_obj);
        return -1;
    }

    fsync(fd);
    close(fd);

    if (rename(temp_path, path) < 0) {
        unlink(temp_path);
        free(full_obj);
        return -1;
    }

    free(full_obj);
    return 0;
}

int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    char path[512];
    object_path(id, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *full_data = malloc(size);
    if (fread(full_data, 1, size, f) != (size_t)size) {
        fclose(f);
        free(full_data);
        return -1;
    }
    fclose(f);

    ObjectID actual_id;
    compute_hash(full_data, size, &actual_id);
    if (memcmp(id->hash, actual_id.hash, HASH_SIZE) != 0) {
        free(full_data);
        return -1;
    }

    uint8_t *null_ptr = memchr(full_data, '\0', size);
    if (!null_ptr) {
        free(full_data);
        return -1;
    }

    char *header = (char *)full_data;
    char type_name[16];
    size_t obj_size;
    sscanf(header, "%15s %zu", type_name, &obj_size);

    if (strcmp(type_name, "blob") == 0) *type_out = OBJ_BLOB;
    else if (strcmp(type_name, "tree") == 0) *type_out = OBJ_TREE;
    else if (strcmp(type_name, "commit") == 0) *type_out = OBJ_COMMIT;

    size_t header_len = (null_ptr - full_data) + 1;
    *len_out = size - header_len;
    *data_out = malloc(*len_out);
    memcpy(*data_out, full_data + header_len, *len_out);

    free(full_data);
    return 0;
}