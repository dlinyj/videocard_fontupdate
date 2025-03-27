#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <sys/stat.h>

#define NORMALIZE   0
#define MIXING      1

#define OUTPUT_FILENAME "out.bin"

void help_getopt(void) {
    printf(
        "Encode - utility for working with ISA VGA card ROM BIOS\n\n"
        "Usage:\n"
        "  encode [options] <file>\n\n"
        "Options:\n"
        "  -n <file>      Normalization (from ROM format to sequential)\n"
        "  -m <file>      Mixing (from sequential to ROM format)\n"
        "  -o <file>      Output filename (default \"out.bin\")\n"
        "  -h             Show this help\n\n"
        "Examples:\n"
        "  ./encode -n read_ROM_hard.bin -o norm.bin\n"
        "  ./encode -m norm.bin -o font_to_rom.bin\n"
    );
    exit(0);
}

int main(int argc, char *argv[]) {
    int rez = 0;
    int type_oper = 0;
    char * filename = NULL;
    char * output_filename = OUTPUT_FILENAME;
    int in_fd, out_fd;
    
    int filesize = 0;
    
    char * in_file_memory, *out_file_memory;
    
    if (argc <=1) {
        help_getopt();
    }
    
    while ((rez = getopt(argc, argv, "n:m:o:h")) != -1) {
        switch (rez) {
        case 'n': 
            type_oper = NORMALIZE;
            filename = optarg;
            printf("Normalization file %s\n", filename);
            break;
        case 'm': 
            type_oper = MIXING;
            filename = optarg;
            printf("Mixing file %s\n", filename);
            break;
        case 'o': 
            output_filename = optarg;
            printf("Output filename %s\n", output_filename);
            break;
        case 'h':
        default:
            help_getopt();
        } // switch
    } // while
    
    if (filename == NULL) {
        fprintf(stderr, "Error: Input file not specified\n");
        help_getopt();
    }
    
    struct stat st;
    if (stat(filename, &st) != 0) {
        perror("Error accessing input file");
        exit(1);
    }
    filesize = st.st_size;
    
    printf("Filesize = %d bytes\n", filesize);
    
    in_fd = open(filename, O_RDWR, S_IRUSR | S_IWUSR);
    if (in_fd == -1) {
        perror("Error opening input file");
        exit(1);
    }
    
    out_fd = open(output_filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (out_fd == -1) {
        perror("Error creating output file");
        close(in_fd);
        exit(1);
    }
    
    lseek(out_fd, filesize-1, SEEK_SET);
    write(out_fd, "\0", 1);
    lseek(out_fd, 0, SEEK_SET);

    in_file_memory = mmap(0, filesize, PROT_READ | PROT_WRITE, MAP_SHARED, in_fd, 0);
    if (in_file_memory == MAP_FAILED) {
        perror("Error mapping input file to memory");
        close(in_fd);
        close(out_fd);
        exit(1);
    }
    
    out_file_memory = mmap(0, filesize, PROT_WRITE, MAP_SHARED, out_fd, 0);
    if (out_file_memory == MAP_FAILED) {
        perror("Error mapping output file to memory");
        munmap(in_file_memory, filesize);
        close(in_fd);
        close(out_fd);
        exit(1);
    }

    // Process the data
    if (type_oper == NORMALIZE) {
        printf("Processing: Converting ROM format to sequential format...\n");
        for (int i = 0; i < filesize; i++) {
            if (i % 2) {
                out_file_memory[i] = in_file_memory[i/2 + 0x4000];
            } else {
                out_file_memory[i] = in_file_memory[i/2];
            }
        }
    } else {
        printf("Processing: Converting sequential format to ROM format...\n");
        // Ensure we don't access memory beyond the mapped region
        int half_size = filesize / 2;
        for (int i = 0; i < half_size; i++) {
            out_file_memory[i] = in_file_memory[i*2];
            if (i + 0x4000 < filesize && i*2 + 1 < filesize) {
                out_file_memory[i + 0x4000] = in_file_memory[i*2 + 1];
            }
        }
    }

    munmap(in_file_memory, filesize);
    munmap(out_file_memory, filesize);
    close(in_fd);
    close(out_fd);
    
    printf("Operation completed successfully.\n");
    printf("Result saved to file: %s\n", output_filename);
    
    return 0;
} // main
