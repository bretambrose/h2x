
#ifndef H2X_HASH_TABLE_H
#define H2X_HASH_TABLE_H

#include <stdbool.h>
#include <stdint.h>

struct h2x_hash_entry {
    void* data;
    struct h2x_hash_entry* next;
};

struct h2x_hash_table {
    struct h2x_hash_entry** buckets;
    uint32_t bucket_count;
    uint32_t (*hash_function)(void*);
};

void h2x_hash_table_init(struct h2x_hash_table* hash_table, uint32_t buckets, uint32_t (*hash_function)(void*));
void h2x_hash_table_cleanup(struct h2x_hash_table *table);

bool h2x_hash_table_add(struct h2x_hash_table* table, void* data);
bool h2x_hash_table_remove(struct h2x_hash_table* table, uint32_t key);
void* h2x_hash_table_find(struct h2x_hash_table* table, uint32_t key);
void h2x_hash_table_visit(struct h2x_hash_table *table, void (*visit_function)(void *, void*), void* context);

#endif // H2X_HASH_TABLE_H
