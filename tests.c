#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#include "lib_tar.h"

#define MAX_ENTRIES 128
#define PATHBUF 512

/**
 * You are free to use this file to write tests for your implementation
 */

void debug_dump(const uint8_t *bytes, size_t len) {
    for (int i = 0; i < len;) {
        printf("%04x:  ", (int) i);

        for (int j = 0; j < 16 && i + j < len; j++) {
            printf("%02x ", bytes[i + j]);
        }
        printf("\t");
        for (int j = 0; j < 16 && i < len; j++, i++) {
            printf("%c ", bytes[i]);
        }
        printf("\n");
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s tar_file\n", argv[0]);
        return -1;
    }

    int fd = open(argv[1] , O_RDONLY);
    if (fd == -1) {
        perror("open(tar_file)");
        return -1;
    }

    // --- CHECK ARCHIVE TEST ---

    printf("\n--- CHECK ARCHIVE TEST ---\n");

    int ret = check_archive(fd);
    printf("check_archive returned %d\n", ret);


    // --- EXISTS TESTS ----

    printf("\n--- EXISTS TESTS ---\n");

    char *test_paths[] = {
        "test1.txt",
        "test2.txt",
        "test3.txt",
        "dir1/test1.txt",
        "dir1/test2.txt",
        "dir1/test3.txt", // non
        "dir2/", // non
        "dir2/test1.txt", // non
        "nonexistent.txt" // non
    };

    for (size_t i = 0; i < sizeof(test_paths)/sizeof(test_paths[0]); ++i) {
        int exists_ret = exists(fd, test_paths[i]);
        printf("exists(%s) returned %d\n", test_paths[i], exists_ret);
    }

    // --- IS_DIR TESTS ----

    printf("\n--- IS_DIR TESTS ---\n");

    char *dir_paths[] = {
        "dir1/",     
        "dir2/",     // non
        "test1.txt", // existe mais ce n'est pas un dossier -> 0
    };

    for (size_t i = 0; i < sizeof(dir_paths)/sizeof(dir_paths[0]); ++i) {
        int d = is_dir(fd, dir_paths[i]);
        printf("is_dir(%s) returned %d\n", dir_paths[i], d);
    }

    // --- IS_FILE TESTS ----

    printf("\n--- IS_FILE TESTS ---\n");

    char *file_paths[] = {
        "test1.txt",        // oui
        "test2.txt",        // oui
        "test3.txt",        // oui
        "dir1/test1.txt",   // oui
        "dir1/test2.txt",   // oui
        "dir1/",            // non (dossier)
        "dir2/test1.txt",   // non
        "nonexistent.txt"   // non
    };

    for (size_t i = 0; i < sizeof(file_paths)/sizeof(file_paths[0]); ++i) {
        int r = is_file(fd, file_paths[i]);
        printf("is_file(%s) returned %d\n", file_paths[i], r);
    }

    // --- LIST TESTS ----

    printf("\n--- LIST TESTS ---\n");

    size_t no_entries = MAX_ENTRIES;

    char *entries[no_entries];
    for (size_t i = 0; i < MAX_ENTRIES; ++i) {
        entries[i] = malloc(PATHBUF);
    }



    int ret2 = list(fd, "dir1", entries, &no_entries);
    printf("list returned %d\n", ret2);
    for (size_t i = 0; i < no_entries; ++i) {
        printf("entry %zu: %s\n", i, entries[i]);
    }

    int ret3 = list(fd, NULL, entries, &no_entries);
    printf("list returned %d\n", ret3);
    for (size_t i = 0; i < no_entries; ++i) {
        printf("entry %zu: %s\n", i, entries[i]);
    }

    // problème : renvoie le dir actuel comme étant un enfant direct de lui-même
    

    close(fd);


    return 0;
}