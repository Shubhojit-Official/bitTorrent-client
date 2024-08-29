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

// Internal Parsing Functions
Bencode* __parse_int(const char** data);
Bencode* __parse_string(const char**data);
Bencode* __parse_list(const char**data);
Bencode* __parse_dict(const char**data);
void __print_parsed_data(Bencode* data); // use it to print the parsed data

//---API---
/*
    @param const char **data : Pointer to the bencoded string
    @return Bencode* : A pointer to the Bencode Struct
 */
Bencode* parse_bencode(const char **data);
// @param Bencode* be_node: Pointer to a *Parsed* Bencoded Dictionary
Bencode** get_all_keys(Bencode* be_node);





#endif