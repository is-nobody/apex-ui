// source/common/apexui_value.c
// Self-contained nan-boxed value runtime for the Apex UI library
// https://github.com/is-nobody/apex-ui
// MIT license

#include "apexui_value.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#ifdef _WIN32
#include <sys/timeb.h>
#else
#include <sys/time.h>
#endif

// djb2 hash function for string interning
static unsigned int intern_hash(const char* chars, int length) {
    unsigned int hash = 5381;                    // djb2 initial seed
    for (int i = 0; i < length; i++) {
        hash = ((hash << 5) + hash) + (unsigned char)chars[i];  // hash * 33 + char
    }
    return hash;                                 // return computed hash
}

// initializes the string intern table with a fixed size
bool uival_string_intern_table_init(StringInternTable* it) {
    it->capacity = INTERN_INITIAL_SIZE;          // set initial bucket count
    it->count = 0;                               // no entries yet
    it->buckets = calloc(INTERN_INITIAL_SIZE, sizeof(StringObject*));  // allocate zeroed bucket array
    if (!it->buckets) {                          // allocation failed
        it->capacity = 0;                        // leave the table unusable but consistent
        return false;                            // caller must abort
    }
    return true;                                 // ready for use
}

// frees all interned strings and the table itself
void uival_string_intern_table_free(StringInternTable* it) {
    for (int i = 0; i < it->capacity; i++) {
        if (it->buckets[i]) {
            free(it->buckets[i]);                // free each interned string
        }
    }
    free(it->buckets);                           // free bucket array
    it->buckets = NULL;                          // clear dangling pointer
    it->capacity = 0;                            // reset capacity
    it->count = 0;                               // reset count
}

// resizes the intern table when load factor exceeds the threshold
static bool intern_table_resize(StringInternTable* it, int new_capacity) {
    StringObject** old_buckets = it->buckets;    // save old bucket array
    int old_capacity = it->capacity;             // save old capacity
    StringObject** new_buckets = calloc(new_capacity, sizeof(StringObject*));  // allocate new zeroed bucket array
    if (!new_buckets) return false;              // allocation failed: keep old table intact
    it->buckets = new_buckets;                   // install only on success
    it->capacity = new_capacity;                 // update capacity
    it->count = 0;                               // reset count, will recount during rehash
    for (int i = 0; i < old_capacity; i++) {
        StringObject* str = old_buckets[i];      // fetch string from old bucket
        if (str) {
            unsigned int idx = intern_hash(str->chars, str->length) % new_capacity;  // compute new bucket index
            while (it->buckets[idx] != NULL) {   // linear probe until empty slot
                idx = (idx + 1) % new_capacity;  // wrap around if needed
            }
            it->buckets[idx] = str;              // insert string into new bucket
            it->count++;                         // increment entry count
        }
    }
    free(old_buckets);                           // free old bucket array
    return true;                                 // resize succeeded
}

// interns a string, returning a canonical object with linear probing
StringObject* uival_string_intern(StringInternTable* it, const char* chars, int length) {
    if (!it || !chars) return NULL;                  // guard against null params
    if (it->buckets == NULL) return NULL;            // table never initialised: report failure
    if ((double)it->count / it->capacity > INTERN_MAX_LOAD) {      // check load factor
        if (!intern_table_resize(it, it->capacity * 2)) return NULL;  // resize failed
    }
    unsigned int hash = intern_hash(chars, length);  // compute hash of input string
    unsigned int idx = hash % it->capacity;          // initial bucket index
    for (int i = 0; i < it->capacity; i++) {
        unsigned int probe_idx = (idx + i) % it->capacity;         // linear probe offset
        StringObject* existing = it->buckets[probe_idx];           // fetch existing string at slot
        if (existing == NULL) {                      // empty slot, insert new string
            StringObject* new_str = uival_string_create(chars, length);  // allocate new string object
            if (!new_str) return NULL;               // allocation failed
            new_str->hash = hash;                    // store precomputed hash
            new_str->hash_computed = true;           // mark hash as computed
            new_str->header.ref_count = INT_MAX;     // interned strings are immortal
            it->buckets[probe_idx] = new_str;        // insert into bucket
            it->count++;                             // increment entry count
            return new_str;                          // return newly interned string
        }
        if (existing->length == length &&            // length matches
            memcmp(existing->chars, chars, length) == 0) {         // content matches
            return existing;                         // string already interned, return existing
        }
    }
    return NULL;                                     // table is full, should never happen
}

// allocates a new string object with refcount and flexible array
StringObject* uival_string_create(const char* chars, int length) {
    StringObject* str = (StringObject*)malloc(sizeof(StringObject) + length + 1);  // alloc struct + chars + null
    if (!str) return NULL;                       // allocation failed
    str->header.ref_count = 1;                   // fresh object starts with refcount 1
    str->header.type = VAL_STRING;               // mark type as string
    str->length = length;                        // store length
    str->hash_computed = false;                  // hash not yet computed (lazy)
    str->hash = 0;                               // clear hash field
    memcpy(str->chars, chars, length);           // copy character data
    str->chars[length] = '\0';                   // null terminate
    return str;                                  // return new string object
}

// increments the reference count of a reference-counted value
void uival_value_incref(Value v) {
    if (IS_STRING(v)) {
        StringObject* str = AS_STRING(v);               // unwrap string pointer
        if (str && str->header.ref_count != INT_MAX) {  // not interned (immortal)
            str->header.ref_count++;                    // bump refcount
        }
    } else if (IS_TABLE(v)) {
        Table* table = AS_TABLE(v);                     // unwrap table pointer
        if (table) table->header.ref_count++;           // bump refcount
    }
}

// decrements the reference count and frees the object when it reaches zero
void uival_value_decref(Value v) {
    if ((v & QNAN) != QNAN) return;                    // unboxed immediate, no refcount needed

    if (IS_STRING(v)) {
        StringObject* str = AS_STRING(v);              // unwrap string pointer
        if (str->header.ref_count == INT_MAX) return;  // interned string, never freed
        if (--str->header.ref_count == 0) {            // decrement and check if dead
            free(str);                                 // free string memory
        }
    } else if (IS_TABLE(v)) {
        Table* table = AS_TABLE(v);                    // unwrap table pointer
        if (--table->header.ref_count == 0) {          // decrement and check if dead
            uival_table_destroy(table);                      // destroy table and all entries
        }
    }
}

// creates a new hash table with separate array part for integer keys
Table* uival_table_create(int capacity) {
    Table* table = (Table*)malloc(sizeof(Table));   // allocate table struct
    if (!table) return NULL;                        // allocation failed
    table->header.ref_count = 1;                    // fresh object starts with refcount 1
    table->header.type = VAL_TABLE;                 // mark type as table
    table->capacity = capacity < 8 ? 8 : capacity;  // ensure minimum capacity of 8
    table->hash_count = 0;                          // no hash entries yet
    table->entries = NULL;                          // allocate zeroed bucket array
    table->array_capacity = 0;                      // array part not yet allocated (lazy)
    table->array_part = NULL;                       // no array part yet
    table->array_count = 0;                         // no array elements yet
    table->generation = 0;                          // no mutations yet
    return table;                                   // return new table
}

// destroys a table and all its entries
void uival_table_destroy(Table* table) {
    if (!table) return;                          // guard against null

    if (table->entries) {
        for (int i = 0; i < table->capacity; i++) {
            TableEntry* entry = table->entries[i];   // get head of bucket chain
            while (entry) {
                TableEntry* next = entry->next;      // save next pointer before freeing
                uival_value_decref(entry->key);            // release key
                uival_value_decref(entry->value);          // release value
                free(entry);                         // free entry struct
                entry = next;                        // advance to next entry
            }
        }
        free(table->entries);                        // free bucket array
    }
    if (table->array_part) {
        for (int i = 0; i < table->array_count; i++) {
            uival_value_decref(table->array_part[i]);  // release each array element
        }
        free(table->array_part);                 // free array part
    }
    free(table);                                 // free table struct
}

// grow array part to fit needed_index, doubling until large enough
static void array_part_grow(Table* table, int needed_index) {
    if (table->array_part == NULL) {                 // array part not yet allocated
        table->array_capacity = TABLE_ARRAY_INIT;    // start with default size
        while (table->array_capacity <= needed_index) {
            table->array_capacity *= 2;              // double until index fits
        }
        table->array_part = (Value*)malloc(table->array_capacity * sizeof(Value));  // allocate array
        for (int i = 0; i < table->array_capacity; i++) {
            table->array_part[i] = MAKE_NONE();       // fill with none (empty slot marker)
        }
        return;                                       // done, array was just created
    }
    int new_capacity = table->array_capacity;         // start from current capacity
    while (new_capacity <= needed_index) {
        new_capacity *= 2;                            // double until index fits
    }
    Value* new_array = (Value*)malloc(new_capacity * sizeof(Value));  // allocate new larger array
    for (int i = 0; i < table->array_count; i++) {
        new_array[i] = table->array_part[i];          // copy existing elements
    }
    for (int i = table->array_count; i < new_capacity; i++) {
        new_array[i] = MAKE_NONE();                   // fill remaining slots with none
    }
    free(table->array_part);                          // free old array
    table->array_part = new_array;                    // point to new array
    table->array_capacity = new_capacity;             // update capacity
}

// set value at integer index in array part, growing if needed
bool uival_table_set_int(Table* table, int index, Value value) {
    if (index < 0) return false;                 // negative indices not allowed
    if (table->array_part == NULL || index >= table->array_capacity) {
        array_part_grow(table, index);           // grow array to fit index
    }
    uival_value_decref(table->array_part[index]);      // release old value at slot
    table->array_part[index] = value;            // store new value
    uival_value_incref(table->array_part[index]);      // bump refcount for stored value
    if (index >= table->array_count) {
        table->array_count = index + 1;          // update array count if extending
    }
    return true;                                 // success
}

// compute hash for a value used as table key
static uint32_t hash_value_key(Value key) {
    if (IS_STRING(key)) {
        StringObject* str = AS_STRING(key);                   // unwrap string pointer
        if (!str->hash_computed) {                            // compute hash lazily
            uint32_t h = 5381;                                // djb2 initial seed
            for (int i = 0; i < str->length; i++) {
                h = ((h << 5) + h) + (uint8_t)str->chars[i];  // hash * 33 + char
            }
            str->hash = h;                                    // store computed hash
            str->hash_computed = true;                        // mark as computed
        }
        return str->hash;                                     // return string hash
    } else if (IS_NUMBER(key)) {
        double num = AS_NUMBER(key);                          // unwrap number
        if (num == 0.0) num = 0.0;                            // normalize negative zero to positive zero
        union { double d; uint64_t u; } u;
        u.d = num;                                            // reinterpret double bits as uint64
        uint32_t hash = 2166136261u;                          // fnv offset basis
        hash ^= (uint32_t)(u.u & 0xFFFFFFFF);                 // xor low 32 bits
        hash *= 16777619u;                                    // fnv prime
        hash ^= (uint32_t)(u.u >> 32);                        // xor high 32 bits
        hash *= 16777619u;                                    // fnv prime
        return hash;                                          // return number hash
    }
    return 0;                                                 // fallback for unhashable types
}

// compare two values for key equality
static bool key_equal(Value a, Value b) {
    if (IS_NAN(a) && IS_NAN(b)) return false;                  // nan never equals nan for keys
    if (IS_NAN(a) || IS_NAN(b)) return false;                  // one is nan, not equal
    if (IS_STRING(a) && IS_STRING(b)) {
        StringObject* sa = AS_STRING(a);                       // unwrap first string
        StringObject* sb = AS_STRING(b);                       // unwrap second string
        if (sa == sb) return true;                             // same pointer, definitely equal
        if (sa->length != sb->length) return false;            // different lengths, not equal
        return memcmp(sa->chars, sb->chars, sa->length) == 0;  // compare content byte by byte
    }
    if (IS_NUMBER(a) && IS_NUMBER(b)) {
        return AS_NUMBER(a) == AS_NUMBER(b);                   // compare numeric values
    }
    if (a == b) return true;                                   // bitwise identical tagged values
    return false;                                              // different types or values
}

// sets a value in the table by key, with auto-resizing and duplicate detection
bool uival_table_set(Table* table, Value key, Value value) {
    if (IS_NUMBER(key)) {                             // try array part for integer keys
        double num = AS_NUMBER(key);                  // unwrap number
        if (num >= 1 && num <= 2147483647.0 && (int)num == num) {  // positive integer
            int idx = (int)num - 1;                   // convert to 0-based index
            if (table->array_part == NULL) {
                if (idx < TABLE_ARRAY_INIT * 2) return uival_table_set_int(table, idx, value);  // small index, use array
            } else if (idx < table->array_capacity) {
                return uival_table_set_int(table, idx, value);  // fits in current array, use it
            } else {
                int gap = idx - table->array_count;       // gap between requested index and current end
                if (gap <= table->array_count * 2 && gap <= 1024) return uival_table_set_int(table, idx, value);  // small gap, extend array
            }
        }
    }
    if (table->entries == NULL) {
        table->entries = (TableEntry**)calloc(table->capacity, sizeof(TableEntry*));
    }
    if ((double)(table->hash_count + 1) / table->capacity > TABLE_MAX_LOAD) {  // check load factor, resize if needed
        int old_capacity = table->capacity;           // save old capacity
        TableEntry** old_entries = table->entries;    // save old bucket array
        table->capacity = old_capacity * 2;           // double capacity
        table->entries = (TableEntry**)calloc(table->capacity, sizeof(TableEntry*));  // allocate new zeroed buckets
        table->hash_count = 0;                        // reset count, will recount during rehash
        for (int i = 0; i < old_capacity; i++) {
            TableEntry* entry = old_entries[i];       // get head of old bucket chain
            while (entry) {
                TableEntry* next = entry->next;       // save next before re-linking
                uint32_t idx = entry->hash % table->capacity;      // compute new bucket index
                entry->next = table->entries[idx];    // prepend to new bucket chain
                table->entries[idx] = entry;          // update new bucket head
                table->hash_count++;                  // increment count
                entry = next;                         // advance to next old entry
            }
        }
        free(old_entries);                            // free old bucket array
        table->generation++;                          // structural change: invalidate iterators
    }
    uint32_t hash = hash_value_key(key);              // compute hash for key
    uint32_t index = hash % table->capacity;          // get bucket index
    TableEntry* entry = table->entries[index];        // start of bucket chain
    while (entry) {
        if (entry->hash == hash && key_equal(entry->key, key)) {  // found existing key
            uival_value_decref(entry->value);               // release old value
            entry->value = value;                     // store new value
            uival_value_incref(entry->value);               // bump refcount for new value
            return true;                              // updated existing entry
        }
        entry = entry->next;                          // advance to next in chain
    }
    entry = (TableEntry*)malloc(sizeof(TableEntry));  // allocate new entry
    if (!entry) return false;                         // allocation failed
    entry->key = key;                                 // store key
    uival_value_incref(entry->key);                         // bump refcount for stored key
    entry->hash = hash;                               // store hash for fast comparison
    entry->value = value;                             // store value
    uival_value_incref(entry->value);                       // bump refcount for stored value
    entry->next = table->entries[index];              // prepend to bucket chain
    table->entries[index] = entry;                    // update bucket head
    table->hash_count++;                              // increment hash entry count
    return true;                                      // inserted new entry
}

// retrieves a value from the table by key, returns true if found
bool uival_table_get(Table* table, Value key, Value* out_value) {
    if (!table) return false;                      // guard against null
    if (IS_NUMBER(key)) {                          // try array part for integer keys
        double num = AS_NUMBER(key);               // unwrap number
        if (num >= 1 && num <= 2147483647.0 && (int)num == num) {  // positive integer
            int idx = (int)num - 1;                // convert to 0-based index
            if (table->array_part != NULL && idx < table->array_count) {  // within array bounds
                if (!IS_NONE(table->array_part[idx])) {                   // slot is occupied
                    if (out_value) {
                        *out_value = table->array_part[idx];              // copy value to output
                        uival_value_incref(*out_value);  // bump refcount for caller
                    }
                    return true;                   // found in array part
                }
            }
        }
    }
    if (table->entries == NULL) return false;
    uint32_t hash = hash_value_key(key);           // compute hash for key
    uint32_t index = hash % table->capacity;       // get bucket index
    TableEntry* entry = table->entries[index];     // start of bucket chain
    while (entry) {
        if (entry->hash == hash && key_equal(entry->key, key)) {  // found matching entry
            if (out_value) {
                *out_value = entry->value;         // copy value to output
                uival_value_incref(*out_value);          // bump refcount for caller
            }
            return true;                           // found in hash part
        }
        entry = entry->next;                       // advance to next in chain
    }
    return false;                                  // key not found
}

// removes a key-value pair from the table
void uival_table_remove(Table* table, Value key) {
    if (!table) return;                                  // guard against null
    if (IS_NUMBER(key)) {                                // try array part for integer keys
        double num = AS_NUMBER(key);                     // unwrap number
        if (num >= 1 && num == (int)num && table->array_part != NULL) {  // positive integer with existing array
            int idx = (int)num - 1;                      // convert to 0-based index
            if (idx < table->array_count) {              // within filled array bounds
                uival_value_decref(table->array_part[idx]);    // release the slot value
                table->array_part[idx] = MAKE_NONE();    // clear the slot
                table->generation++;                     // structural change: invalidate iterators
                return;                                  // removed from array part
            }
        }
    }
    if (table->entries == NULL) return;                  // nothing in the hash part yet
    uint32_t hash = hash_value_key(key);                 // compute hash for key
    uint32_t index = hash % table->capacity;             // get bucket index
    TableEntry** pp = &table->entries[index];            // pointer to bucket head slot
    while (*pp) {
        TableEntry* entry = *pp;
        if (entry->hash == hash && key_equal(entry->key, key)) {  // found matching entry
            *pp = entry->next;                           // splice the entry out
            uival_value_decref(entry->key);                    // release key
            uival_value_decref(entry->value);                  // release value
            free(entry);                                 // free the node
            table->hash_count--;                         // one fewer hash entry
            table->generation++;                         // structural change
            return;                                      // removed
        }
        pp = &entry->next;                               // advance to next link
    }
}

// returns the total number of entries in the table
int uival_table_size(Table* table) {
    if (!table) return 0;                            // guard against null
    int n = table->hash_count;                       // count hash part entries
    for (int i = 0; i < table->array_count; i++) {   // count occupied array slots
        if (!IS_NONE(table->array_part[i])) n++;
    }
    return n;                                        // total live entries
}

// returns a malloc'd array of all keys in the table; caller frees the array
Value* uival_table_keys(Table* table, int* out_count) {
    if (!table || !out_count) { if (out_count) *out_count = 0; return NULL; }
    int cap = table->hash_count + table->array_count;    // upper bound on live keys
    if (cap == 0) { *out_count = 0; return NULL; }
    Value* keys = (Value*)malloc(sizeof(Value) * cap);   // allocate key snapshot
    if (!keys) { *out_count = 0; return NULL; }
    int n = 0;
    for (int i = 0; i < table->array_count; i++) {   // array part first, 1-based indices
        if (!IS_NONE(table->array_part[i])) {
            keys[n] = MAKE_NUMBER((double)(i + 1));
            n++;
        }
    }
    if (table->entries) {                            // then hash part, bucket order
        for (int i = 0; i < table->capacity; i++) {
            for (TableEntry* e = table->entries[i]; e; e = e->next) {
                keys[n] = e->key;
                uival_value_incref(keys[n]);               // snapshot holds a reference
                n++;
            }
        }
    }
    *out_count = n;
    return keys;                                     // caller releases keys and each key value
}

// returns current wall-clock time in seconds
double apexui_now_seconds(void) {
#ifdef _WIN32
    struct _timeb tb;                                   // windows timeb
    _ftime(&tb);                                        // read wall clock
    return (double)tb.time + (double)tb.millitm / 1000.0;  // combine into seconds
#else
    struct timeval tv;                                  // timeval buffer
    gettimeofday(&tv, NULL);                            // read wall clock
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;  // combine into seconds
#endif
}