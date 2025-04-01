#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Function to read a file into a buffer
unsigned char* read_file(const char* filename, size_t* size) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Error: Could not open file %s\n", filename);
        return NULL;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    *size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Allocate buffer
    unsigned char* buffer = (unsigned char*)malloc(*size);
    if (!buffer) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        fclose(file);
        return NULL;
    }
    
    // Read file into buffer
    size_t bytes_read = fread(buffer, 1, *size, file);
    fclose(file);
    
    if (bytes_read != *size) {
        fprintf(stderr, "Error: Failed to read entire file %s\n", filename);
        free(buffer);
        return NULL;
    }
    
    return buffer;
}

// Function to search for a pattern and replace it
int search_and_replace(unsigned char* source, size_t source_size,
                       unsigned char* find_pattern, size_t find_size,
                       unsigned char* replace_pattern, size_t replace_size,
                       const char* output_filename) {
    
    FILE* output_file = fopen(output_filename, "wb");
    if (!output_file) {
        fprintf(stderr, "Error: Could not create output file %s\n", output_filename);
        return -1;
    }
    
    size_t pos = 0;
    int replacements = 0;
    
    while (pos <= source_size - find_size) {
        // Check if pattern matches at current position
        if (memcmp(source + pos, find_pattern, find_size) == 0) {
            // Write replacement pattern
            fwrite(replace_pattern, 1, replace_size, output_file);
            pos += find_size;
            replacements++;
        } else {
            // Write current byte
            fwrite(&source[pos], 1, 1, output_file);
            pos++;
        }
    }
    
    // Write remaining bytes if any
    if (pos < source_size) {
        fwrite(&source[pos], 1, source_size - pos, output_file);
    }
    
    fclose(output_file);
    return replacements;
}

int main(int argc, char* argv[]) {
    const char* source_filename;
    const char* find_pattern_filename;
    const char* replace_pattern_filename;
    const char* output_filename;
    int in_place = 0;
    
    // Parse command line arguments
    if (argc == 4) {
        // In-place replacement
        source_filename = argv[1];
        find_pattern_filename = argv[2];
        replace_pattern_filename = argv[3];
        output_filename = NULL;  // Will be set to a temporary file
        in_place = 1;
    } else if (argc == 5) {
        // Replacement with output file
        source_filename = argv[1];
        find_pattern_filename = argv[2];
        replace_pattern_filename = argv[3];
        output_filename = argv[4];
    } else {
        fprintf(stderr, "Usage: %s source.bin find_pattern.bin replace_pattern.bin [output.bin]\n", argv[0]);
        fprintf(stderr, "If output file is not specified, the source file will be modified in place.\n");
        return 1;
    }
    
    size_t source_size, find_size, replace_size;
    
    // Read source file
    unsigned char* source = read_file(source_filename, &source_size);
    if (!source) return 1;
    
    // Read find pattern
    unsigned char* find_pattern = read_file(find_pattern_filename, &find_size);
    if (!find_pattern) {
        free(source);
        return 1;
    }
    
    // Read replace pattern
    unsigned char* replace_pattern = read_file(replace_pattern_filename, &replace_size);
    if (!replace_pattern) {
        free(source);
        free(find_pattern);
        return 1;
    }
    
    // For in-place replacement, use a temporary file
    char* temp_filename = NULL;
    if (in_place) {
        temp_filename = malloc(strlen(source_filename) + 5);
        if (!temp_filename) {
            fprintf(stderr, "Error: Memory allocation failed\n");
            free(source);
            free(find_pattern);
            free(replace_pattern);
            return 1;
        }
        sprintf(temp_filename, "%s.tmp", source_filename);
        output_filename = temp_filename;
    }
    
    // Perform search and replace
    int replacements = search_and_replace(source, source_size, 
                                         find_pattern, find_size, 
                                         replace_pattern, replace_size, 
                                         output_filename);
    
    // If in-place replacement and successful, replace the original file
    if (in_place && replacements >= 0) {
        if (remove(source_filename) != 0) {
            fprintf(stderr, "Error: Failed to remove original file %s\n", source_filename);
            free(source);
            free(find_pattern);
            free(replace_pattern);
            free(temp_filename);
            return 1;
        }
        if (rename(temp_filename, source_filename) != 0) {
            fprintf(stderr, "Error: Failed to rename temporary file to %s\n", source_filename);
            fprintf(stderr, "Your data is saved in %s\n", temp_filename);
            free(source);
            free(find_pattern);
            free(replace_pattern);
            free(temp_filename);
            return 1;
        }
    }
    
    if (replacements >= 0) {
        printf("Replacement complete. %d pattern(s) replaced.\n", replacements);
    }
    
    // Clean up
    free(source);
    free(find_pattern);
    free(replace_pattern);
    if (temp_filename) {
        free(temp_filename);
    }
    
    return (replacements >= 0) ? 0 : 1;
}
