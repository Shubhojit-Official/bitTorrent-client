#ifndef BENCODE_H
#define BENCODE_H

#include <stddef.h>

// Bencode Data Types
typedef enum BE_TYPE
{
    BE_STRING,
    BE_INTEGER,
    BE_LIST,
    BE_DICT
} BE_TYPE;

// Stores the Type and the data which could be str,int or other bencode data
typedef struct Bencode
{
    BE_TYPE type;

    union value
    {
        char *string;
        long long integer;
        struct Bencode **list;
        struct Bencode **dict;
    } value;

    size_t len;
    const char *raw_start; // pointer into the original buffer
    const char *raw_end;   // one past the last byte of this node

} Bencode;

// Internal Parsing Functions
Bencode *__parse_int(const char **data);
Bencode *__parse_string(const char **data);
Bencode *__parse_list(const char **data);
Bencode *__parse_dict(const char **data);
void __print_parsed_data(Bencode *data); // use it to print the parsed data

//---API---
Bencode *parse_bencode(const char **data);
Bencode **get_all_keys(Bencode *be_node);
void free_be(Bencode *b);
char *read_file(const char *filename);
#endif