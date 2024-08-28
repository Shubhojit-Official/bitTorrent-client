#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/parser.h"

Bencode* parseInt(const char** data){
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

Bencode* parseString(const char**data){
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


Bencode *parseList(const char** data){
    (*data)++; //skips "l"
    Bencode **list =  NULL;
    size_t count = 0; // for the count of elements

    while(**data !='e'){
        list = realloc(list, (count + 1)* sizeof(Bencode*));
        list[count++] = parseBencode(data);
    }
    (*data)++; //skips "e"

    // Allocate a Bencode Node
    Bencode *b =  (Bencode*)malloc(sizeof(Bencode));
    b->type = BE_LIST;
    b->value.list = list;
    b->len = count;

    return b;
}

Bencode *parseDict(const char **data) {
    (*data)++;  // Skip the 'd'

    Bencode **dict = NULL;
    size_t count = 0;

    while (**data != 'e') {
        dict = realloc(dict, (count + 2) * sizeof(Bencode *));
        dict[count++] = parseBencode(data);  // Parse the key (a string)
        dict[count++] = parseBencode(data);  // Parse the value
    }

    (*data)++;  // Skip the 'e'

    Bencode *b = malloc(sizeof(Bencode));
    b->type = BE_DICT;
    b->value.dict = dict;
    b->len = count;

    return b;
}

Bencode* parseBencode(const char **data) {
    if (**data == 'i') {
        return parseInt(data);
    } else if (**data >= '0' && **data <= '9') {
        return parseString(data);
    } else if (**data == 'l') {
        return parseList(data);
    } else if (**data == 'd') {
        return parseDict(data);
    }
    return NULL;  // Invalid bencode data
}



char* readFile(const char *filename){
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


int main(int argc, char const *argv[]){   

    const char* bencode_data = "li42e5:spamse";
    Bencode* parsed = parseBencode(&bencode_data);
    if(parsed->type == BE_LIST){
        for (size_t i = 0; i < parsed->len; i++)
        {
            if ((parsed->value.list)[i]->type == BE_INTEGER)
            {
                printf("%ld\n", (parsed->value.list)[i]->value.integer);
            }
            else if((parsed->value.list)[i]->type == BE_STRING){
                printf("%s\n", (parsed->value.list)[i]->value.string);
            }
            
        }
        
    }
    return 0;
}
