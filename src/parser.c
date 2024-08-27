#include <stdio.h>
#include <stdlib.h>


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

    return 0;
}
