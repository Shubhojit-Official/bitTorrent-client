#ifndef PARSER_H
#define PARSER_H

// Bencode Data Types
typedef enum BE_TYPE{
    BE_STRING,
    BE_INTEGER,
    BE_LIST,
    BE_DICT
}BE_TYPE;

// Stores the Type and the data which could be str,int or other bencode data
typedef struct Bencode
{
    BE_TYPE type;

    union value{
        char* string;
        long integer;
        struct Bencode** list;
        struct Bencode** dict;
    }value;

    size_t len;

}Bencode;

Bencode* parse_bencode(const char **data);



#endif