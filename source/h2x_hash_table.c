#include <h2x_hash_table.h>

#include <assert.h>
#include <stdlib.h>

struct h2x_hash_table* h2x_hash_table_init(uint32_t buckets, uint32_t (*hash_function)(void*))
{
    assert(buckets > 0);

    struct h2x_hash_table* new_table = malloc(sizeof(struct h2x_hash_table));
    new_table->bucket_count = buckets;
    new_table->hash_function = hash_function;
    new_table->buckets = calloc(buckets, sizeof(struct h2x_hash_entry*));

    return new_table;
}

static uint32_t hash_key_to_bucket(struct h2x_hash_table* table, uint32_t key)
{
    return key % table->bucket_count;
}

bool h2x_hash_table_add(struct h2x_hash_table* table, void* data)
{
    uint32_t hash_key = (table->hash_function)(data);
    uint32_t bucket = hash_key_to_bucket(table, hash_key);

    struct h2x_hash_entry** entry_ptr = &table->buckets[bucket];
    while(*entry_ptr != NULL)
    {
        uint32_t existing_hash_key = (table->hash_function)((*entry_ptr)->data);
        if(existing_hash_key == hash_key)
        {
            return false;   // keys should be unique
        }

        entry_ptr = &((*entry_ptr)->next);
    }

    *entry_ptr = malloc(sizeof(struct h2x_hash_entry));
    (*entry_ptr)->data = data;
    (*entry_ptr)->next = NULL;
}

bool h2x_hash_table_remove(struct h2x_hash_table* table, uint32_t key)
{
    uint32_t bucket = hash_key_to_bucket(table, key);
    struct h2x_hash_entry** entry_ptr = &table->buckets[bucket];
    while(*entry_ptr != NULL)
    {
        uint32_t existing_hash_key = (table->hash_function)((*entry_ptr)->data);
        if(existing_hash_key == key)
        {
            struct h2x_hash_entry* remove = *entry_ptr;
            *entry_ptr = (*entry_ptr)->next;
            free(remove);
            return true;
        }

        entry_ptr = &((*entry_ptr)->next);
    }

    return false;
}

void* h2x_hash_table_find(struct h2x_hash_table* table, uint32_t key)
{
    uint32_t bucket = hash_key_to_bucket(table, key);
    struct h2x_hash_entry* entry_ptr = table->buckets[bucket];
    while(entry_ptr != NULL)
    {
        uint32_t existing_hash_key = (table->hash_function)(entry_ptr->data);
        if(existing_hash_key == key)
        {
            return entry_ptr;
        }
    }

    return NULL;
}

static void free_bucket(struct h2x_hash_entry* entry)
{
    while(entry)
    {
        struct h2x_hash_entry* next_entry = entry->next;
        free(entry);
        entry = next_entry;
    }
}

void h2x_hash_table_cleanup(struct h2x_hash_table *table)
{
    for(uint32_t i = 0; i < table->bucket_count; ++i)
    {
        free_bucket(table->buckets[i]);
    }
    free(table->buckets);
    free(table);
}

void h2x_hash_table_visit(struct h2x_hash_table *table, void (*visit_function)(void *))
{
    for(uint32_t i = 0; i < table->bucket_count; ++i)
    {
        struct h2x_hash_entry* entry = table->buckets[i];
        while(entry)
        {
            (*visit_function)(entry->data);
            entry = entry->next;
        }
    }
}
