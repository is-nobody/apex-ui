// source/common/apex_ui_value.h
// Self-contained nan-boxed value runtime for the Apex UI library
// https://github.com/is-nobody/apex-ui
// MIT license

#ifndef APEXUI_VALUE_H
#define APEXUI_VALUE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

// public value type of the apex ui api; identical to the internal Value below
typedef uint64_t apex_ui_value;

// host callback used by the batched run loop; declared here so the internal
// headers see it, and re-declared identically in apex_ui.h for hosts
typedef apex_ui_value (*apex_ui_run_fn)(void* user, apex_ui_value event);

// nan boxing value representation
// uses ieee 754 double nan space to encode type tags in the mantissa bits
// quiet nan has bits 51 set, we use bits 48-50 for type tags
#define QNAN            ((uint64_t)0x7FF8000000000000ULL)

// type tags stored in bits 48-50 of the nan mantissa
#define TAG_NONE        ((uint64_t)0)
#define TAG_BOOL        ((uint64_t)2)
#define TAG_STRING      ((uint64_t)3)
#define TAG_TABLE       ((uint64_t)4)
#define TAG_NAN         ((uint64_t)6)

// mask for extracting type tag (bits 48-50)
#define TAG_MASK        ((uint64_t)0x7)
#define TAG_SHIFT       ((uint64_t)48)

// helper macros for constructing tagged values
#define MAKE_QNAN(tag)       (QNAN | ((uint64_t)(tag) << TAG_SHIFT))
#define MAKE_STRING(p)       (MAKE_QNAN(TAG_STRING) | ((uint64_t)(uintptr_t)(p) & ((uint64_t)0x0000FFFFFFFFFFFFULL)))
#define MAKE_TABLE(p)        (MAKE_QNAN(TAG_TABLE)  | ((uint64_t)(uintptr_t)(p) & ((uint64_t)0x0000FFFFFFFFFFFFULL)))
#define MAKE_NONE()          (MAKE_QNAN(TAG_NONE))
#define MAKE_BOOL(b)         (MAKE_QNAN(TAG_BOOL) | ((b) ? ((uint64_t)1) : ((uint64_t)0)))
#define MAKE_NUMBER(n) ({ \
    double _n = (n); \
    uint64_t _u; \
    if (isnan(_n)) { \
        uint64_t _raw; \
        memcpy(&_raw, &_n, 8); \
        _u = MAKE_QNAN(TAG_NAN) | ((_raw >> 63) & 1); \
    } else { \
        memcpy(&_u, &_n, 8); \
    } \
    _u; \
})

// extraction macros
#define AS_NUMBER(v) (IS_NAN(v) ? \
    ({ uint64_t _bits = 0x7FF8000000000000ULL | (((uint64_t)(v) & 1) << 63); \
       double _d; memcpy(&_d, &_bits, 8); _d; }) \
    : ({ uint64_t _v = (v); double _d; memcpy(&_d, &_v, 8); _d; }))
#define AS_STRING(v)         ((StringObject*)(uintptr_t)((v) & ((uint64_t)0x0000FFFFFFFFFFFFULL)))
#define AS_TABLE(v)          ((Table*)(uintptr_t)((v) & ((uint64_t)0x0000FFFFFFFFFFFFULL)))
#define AS_BOOL(v)           (((v) & 1) != 0)

// type check macros
#define IS_NUMBER(v)         (((v) & QNAN) != QNAN)
#define IS_NAN(v)            (((v) & (QNAN | (TAG_MASK << TAG_SHIFT))) == MAKE_QNAN(TAG_NAN))
#define IS_NONE(v)           ((v) == MAKE_QNAN(TAG_NONE))
#define IS_BOOL(v)           (((v) & (QNAN | (TAG_MASK << TAG_SHIFT))) == MAKE_QNAN(TAG_BOOL))
#define IS_STRING(v)         (((v) & (QNAN | (TAG_MASK << TAG_SHIFT))) == MAKE_QNAN(TAG_STRING))
#define IS_TABLE(v)          (((v) & (QNAN | (TAG_MASK << TAG_SHIFT))) == MAKE_QNAN(TAG_TABLE))

// true when v holds a plain ieee 754 double (not a nan-box tag)
#define IS_PLAIN(v) (((v) & QNAN) != QNAN)

// value is a single 64-bit integer using nan boxing; same bit layout as
// the public apex_ui_value type, so values can cross the API boundary as-is
typedef uint64_t Value;

// value type enum
typedef enum {
    VAL_NUMBER,          // double-precision floating point (unboxed)
    VAL_STRING,          // refcounted string object (pointer tagged in nan)
    VAL_NONE,            // special none tag
    VAL_BOOL,            // boolean true or false (tagged nan)
    VAL_TABLE,           // hash table / array (pointer tagged in nan)
} ApexUIValueType;

// reference counting header for garbage-collected objects
typedef struct {
    int ref_count;              // number of references to this object
    ApexUIValueType type;       // discriminator for the object type
} ApexUIRefHeader;

// string object with refcounted storage, hash cache, and flexible array
typedef struct StringObject {
    ApexUIRefHeader header;     // reference counting header
    uint32_t hash;              // cached hash for faster comparisons
    bool hash_computed;         // whether the hash has been calculated
    int length;                 // string length in bytes
    char chars[];               // flexible array member for the actual data
} StringObject;

// hash table entry with chaining for collisions
typedef struct TableEntry {
    Value key;                  // string or number key
    uint32_t hash;              // precomputed hash for fast comparison
    Value value;                // associated value
    struct TableEntry* next;    // next entry in the chain
} TableEntry;

// fast table with separate array part for integer keys (o(1) direct access)
typedef struct Table {
    ApexUIRefHeader header;     // reference counting header
    TableEntry** entries;       // hash buckets for string keys
    int capacity;               // hash table capacity
    int hash_count;             // number of entries in hash part
    Value* array_part;          // dense array for integer keys starting from 1
    int array_capacity;         // allocated size of array_part
    int array_count;            // number of valid entries in array part
    uint32_t generation;        // bumped on every structural mutation
} Table;

// table load factor for hash part resizing
#define TABLE_MAX_LOAD 0.75
#define TABLE_ARRAY_INIT 64

// string intern table configuration
#define INTERN_INITIAL_SIZE 4096
#define INTERN_MAX_LOAD 0.75

// string interning table for deduplication and fast equality
typedef struct {
    StringObject** buckets;     // hash buckets for interned strings
    int capacity;               // total number of buckets
    int count;                  // number of interned strings stored
} StringInternTable;

// increments the reference count of a reference-counted value
void uival_value_incref(Value v);

// decrements the reference count and frees the object when it reaches zero
void uival_value_decref(Value v);

// creates a new string object (not interned, caller owns the reference)
StringObject* uival_string_create(const char* chars, int length);

// interns a string, returns a canonical stringobject pointer
StringObject* uival_string_intern(StringInternTable* it, const char* chars, int length);

// initializes the string intern table; returns false on allocation failure
bool uival_string_intern_table_init(StringInternTable* it);

// frees the string intern table
void uival_string_intern_table_free(StringInternTable* it);

// creates a new table with the given initial hash capacity
Table* uival_table_create(int capacity);

// frees a table and all its contents
void uival_table_destroy(Table* table);

// sets a key-keyed value in the table, returns true on success
bool uival_table_set(Table* table, Value key, Value value);

// gets a key-keyed value from the table, returns true if found
bool uival_table_get(Table* table, Value key, Value* out_value);

// removes a key-value pair from the table
void uival_table_remove(Table* table, Value key);

// sets a value by integer index (uses array part if possible)
bool uival_table_set_int(Table* table, int index, Value value);

// returns the total number of entries in the table
int uival_table_size(Table* table);

// returns a malloc'd array of all keys in the table; caller frees the array
Value* uival_table_keys(Table* table, int* out_count);

// returns current wall-clock time in seconds
double apex_ui_now_seconds(void);

#endif // APEXUI_VALUE_H