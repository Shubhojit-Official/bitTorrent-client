#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/bencode.h"

Bencode* __parse_int(const char** data){ 
    (*data)++; // skip "i"
    long value = strtol(*data,(char**)data,10);
    (*data)++; //skip "e"

    // Allocate a Bencode Node
    Bencode *b = (Bencode*)malloc(sizeof(Bencode));
    b->type = BE_INTEGER;
    b->value.integer = value;
    b->len = 0;

    return b;    
}

Bencode* __parse_string(const char**data){
    long length = strtol(*data,(char **)data,10);
    (*data)++; //skip ":"
    
    // Allocate memory of size length+1, +1 for null-terminator
    char* str = (char*)malloc(length+1);
    strncpy(str,*data,length); // copy only n char
    str[length] = '\0';  
    *data += length;  // Move the data pointer

    // Allocate a Bencode Node
    Bencode *b = (Bencode*)malloc(sizeof(Bencode));
    b->type = BE_STRING;
    b->value.string = str;
    b->len = length;

    return b;
}


Bencode* __parse_list(const char** data){
    (*data)++; //skips "l"
    Bencode **list =  NULL;
    size_t count = 0; // for the count of elements

    while(**data !='e'){
        list = realloc(list, (count + 1)* sizeof(Bencode*));
        list[count++] = parse_bencode(data);
    }
    (*data)++; //skips "e"

    // Allocate a Bencode Node
    Bencode *b =  (Bencode*)malloc(sizeof(Bencode));
    b->type = BE_LIST;
    b->value.list = list;
    b->len = count;

    return b;
}

Bencode* __parse_dict(const char **data) {
    (*data)++;  // Skip the 'd'

    Bencode **dict = NULL;
    size_t count = 0;

    while (**data != 'e') {
        dict = realloc(dict, (count + 2) * sizeof(Bencode *));
        dict[count++] = parse_bencode(data);  // Parse the key (a string)
        dict[count++] = parse_bencode(data);  // Parse the value
    }

    (*data)++;  // Skip the 'e'

    Bencode *b = malloc(sizeof(Bencode));
    b->type = BE_DICT;
    b->value.dict = dict;
    b->len = count;

    return b;
}

/*
    @param char**data : Pointer to the bencoded string
    @return Bencode*:  A pointer to the Bencode Struct
 */
Bencode* parse_bencode(const char **data) {
    if (**data == 'i') {
        return __parse_int(data);
    } else if (**data >= '0' && **data <= '9') {
        return __parse_string(data);
    } else if (**data == 'l') {
        return __parse_list(data);
    } else if (**data == 'd') {
        return __parse_dict(data);
    }
    return NULL;  // Invalid bencode data
}



char* read_file(const char *filename){
    // Open File
    FILE *file = fopen(filename, "rb");
    if(file == NULL){
        perror("Unable to open file");
        return NULL;
    }

    //Calculates File size
    fseek(file,0,SEEK_END);
    long fileSize = ftell(file);
    rewind(file);

    //Allocate memory
    char *buffer = (char*)malloc(fileSize + 1); // +1 for the null terminator
    if(buffer == NULL){
        perror("Error occured while allocating memory");
        fclose(file);
        return NULL;
    }

    // Read the File into buffer
    fread(buffer, 1, fileSize, file);
    buffer[fileSize] = '\0';

    fclose(file);

    return buffer;
}

// use only for debugging purposes
void __print_parsed_data(Bencode* data){
    switch (data->type)
    {
        case BE_INTEGER:
            printf("%ld", data->value.integer);
            break;
        case BE_STRING:
            printf("%s", data->value.string);
            break;
        case BE_LIST:
            printf("[");
            for (size_t i = 0; i < data->len; i++)
            {
                Bencode* item = (data->value.list)[i];
                __print_parsed_data(item);
                i < data->len - 1 ? printf(", ") : printf("]"); // dont print "," at the end of the list
            }
            break;
        case BE_DICT:
            printf("{");
            for (size_t i = 0; i < data->len; i+=2) 
            {
                Bencode* key  = (data->value.dict)[i];
                __print_parsed_data(key);
                printf(":");
                Bencode* value = (data->value.dict)[i+1];
                __print_parsed_data(value);
                i < data->len - 2 ? printf(", ") : printf("}"); 
            }
            break;

        default:
            perror("Invalid BE_TYPE");
            break;
    }
}

Bencode** get_all_keys(Bencode* be_node){
    if (be_node->type != BE_DICT) return NULL;
    
    Bencode **keys = malloc(be_node->len * sizeof(Bencode*));
    
    for (size_t i = 0, j = 0; i < be_node->len + 1; i+=2, j++){
        keys[j] = (be_node->value.dict)[i];
    }

    return keys;
}

/*
@param Bencode* b: A pointer to a Bencode Object
*/
void free_be(Bencode*b){
    if(b->type == BE_STRING){
        free(b->value.string);
    }
    else if(b->type == BE_DICT || b->type == BE_LIST){
        for (size_t i = 0; i < b->len; i++)
        {
            free_be(b->value.list[i]);  //value.list and value.dict reffers to the same memory space as it is a union
        }
        free(b->value.list);
    }
    free(b);
}


int main(int argc, char const *argv[]){   

    // const char* bencode_data = "di42e4:spami21e5:Helloli69ei420eed2:Hi3:supee";
    const char* bencode_data = "d8:announce2:Hi5:filesdi1e2:Hiee";

    Bencode* parsed = parse_bencode(&bencode_data);
    __print_parsed_data(parsed);
    free_be(parsed);

    return 0;
}
