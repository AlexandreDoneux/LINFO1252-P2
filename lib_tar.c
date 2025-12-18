#include "lib_tar.h"
#include <unistd.h>   
#include <string.h>   
#include <stdint.h>  
#include <sys/types.h>
#include <stdlib.h>  

#define BLOCKSIZE 512
#define PATHBUF 512

static int is_zero_block(const uint8_t *b) {
    for (int i = 0; i < BLOCKSIZE; i++) {
        if (b[i] != 0) return 0;
    }
    return 1;
}

static unsigned int compute_checksum(const tar_header_t *h) {
    const uint8_t *bytes = (const uint8_t *)h;
    unsigned int sum = 0;

    for (int i = 0; i < BLOCKSIZE; i++) {
        if (i >= 148 && i < 156) sum += (uint8_t)' '; 
        else sum += bytes[i];
    }
    return sum;
}

static off_t round_up_512(off_t n) {
    return (n + 511) & ~((off_t)511);
}

/* Construit le chemin complet dans out :
   - si prefix vide : out = name
   - sinon : out = prefix "/" name
   Retourne 0 si ok, -1 si overflow.
*/
static int header_path(const tar_header_t *h, char out[PATHBUF]) {
    char name[sizeof(h->name) + 1];
    char prefix[sizeof(h->prefix) + 1];

    memcpy(name, h->name, sizeof(h->name));
    name[sizeof(h->name)] = '\0';

    memcpy(prefix, h->prefix, sizeof(h->prefix));
    prefix[sizeof(h->prefix)] = '\0';


    if (prefix[0] == '\0') {
        if (strlen(name) >= PATHBUF) return -1;
        strcpy(out, name);
        return 0;
    } else {
        size_t need = strlen(prefix) + 1 + strlen(name);
        if (need >= PATHBUF) return -1;
        strcpy(out, prefix);
        strcat(out, "/");
        strcat(out, name);
        return 0;
    }
}

/**
 * Checks whether the archive is valid.
 *
 * Each non-null header of a valid archive has:
 *  - a magic value of "ustar" and a null,
 *  - a version value of "00" and no null,
 *  - a correct checksum
 *
 * @param tar_fd A file descriptor pointing to the start of a file supposed to contain a tar archive.
 *
 * @return a zero or positive value if the archive is valid, representing the number of non-null headers in the archive,
 *         -1 if the archive contains a header with an invalid magic value,
 *         -2 if the archive contains a header with an invalid version value,
 *         -3 if the archive contains a header with an invalid checksum value
 */
int check_archive(int tar_fd) {
    // TODO
    if (lseek(tar_fd, 0, SEEK_SET) == (off_t)-1) {
        return -3;
    }

    int count = 0;
    tar_header_t h;

    while (1) {
        ssize_t r = read(tar_fd, &h, sizeof(h));
        if (r != (ssize_t)sizeof(h)) {
            return -3;
        }

        if (is_zero_block((const uint8_t *)&h)) {
            tar_header_t h2;
            ssize_t r2 = read(tar_fd, &h2, sizeof(h2));
            if (r2 != (ssize_t)sizeof(h2)) return -3;
            if (!is_zero_block((const uint8_t *)&h2)) return -3;
            return count;
        }

        if (memcmp(h.magic, TMAGIC, TMAGLEN - 1) != 0 || h.magic[TMAGLEN - 1] != '\0') {
            return -1;
        }

        if (memcmp(h.version, TVERSION, TVERSLEN) != 0) {
            return -2;
        }

        unsigned int stored = (unsigned int)TAR_INT(h.chksum);
        unsigned int expected = compute_checksum(&h);
        if (stored != expected) {
            return -3;
        }

        off_t size = (off_t)TAR_INT(h.size);
        off_t skip = round_up_512(size);
        if (skip > 0 && lseek(tar_fd, skip, SEEK_CUR) == (off_t)-1) {
            return -3;
        }

        count++;
    }
}

/**
 * Checks whether an entry exists in the archive.
 *
 * @param tar_fd A file descriptor pointing to the start of a valid tar archive file.
 * @param path A path to an entry in the archive.
 *
 * @return zero if no entry at the given path exists in the archive,
 *         any other value otherwise.
 */
int exists(int tar_fd, char *path) {
    // TODO
    if (path == NULL) return -1;

    if (lseek(tar_fd, 0, SEEK_SET) == (off_t)-1) return -1;

    tar_header_t h;
    char fullpath[PATHBUF];

    while (1) {
        ssize_t r = read(tar_fd, &h, sizeof(h));
        if (r != (ssize_t)sizeof(h)) {
            return -1;
        }

        if (is_zero_block((const uint8_t *)&h)) {
            return 0;
        }

        if (header_path(&h, fullpath) == -1) {
            return -1;
        }

        if (strcmp(fullpath, path) == 0) {
            return 1;
        }

        off_t size = (off_t)TAR_INT(h.size);
        off_t skip = round_up_512(size);
        if (skip > 0 && lseek(tar_fd, skip, SEEK_CUR) == (off_t)-1) {
            return -1;
        }
    }
}

/**
 * Checks whether an entry exists in the archive and is a directory.
 *
 * @param tar_fd A file descriptor pointing to the start of a valid tar archive file.
 * @param path A path to an entry in the archive.
 *
 * @return zero if no entry at the given path exists in the archive or the entry is not a directory,
 *         any other value otherwise.
 */
int is_dir(int tar_fd, char *path) {
    // TODO
    return 0;
}

/**
 * Checks whether an entry exists in the archive and is a file.
 *
 * @param tar_fd A file descriptor pointing to the start of a valid tar archive file.
 * @param path A path to an entry in the archive.
 *
 * @return zero if no entry at the given path exists in the archive or the entry is not a file,
 *         any other value otherwise.
 */
int is_file(int tar_fd, char *path) {
    // TODO
    return 0;
}

/**
 * Checks whether an entry exists in the archive and is a symlink.
 *
 * @param tar_fd A file descriptor pointing to the start of a valid tar archive file.
 * @param path A path to an entry in the archive.
 * @return zero if no entry at the given path exists in the archive or the entry is not symlink,
 *         any other value otherwise.
 */
int is_symlink(int tar_fd, char *path) {
    // TODO
    return 0;
}

/**
 * Lists the entries at a given path in the archive.
 * list() does *not* recurse into the directories listed at the given path.
 * If the path is NULL, it lists the entries at the root of the archive.
 *
 * Example:
 *  dir/          list(..., "dir/", ...) lists "dir/a", "dir/b", "dir/c/" and "dir/e/"
 *   ├── a
 *   ├── b
 *   ├── c/
 *   │   └── d
 *   └── e/
 *
 * @param tar_fd A file descriptor pointing to the start of a valid tar archive file.
 * @param path A path to an entry in the archive. If the entry is a symlink, it must be resolved to its linked-to entry.
 * @param entries An array of char arrays, each one is long enough to contain a tar entry path.
 * @param no_entries An in-out argument.
 *                   The caller set it to the number of entries in `entries`.
 *                   The callee set it to the number of entries listed.
 *
 * @return zero if no directory at the given path exists in the archive,
 *         1 in case of success,
 *         -1 in case of error.
 */
int list(int tar_fd, char *path, char **entries, size_t *no_entries) {
    // TODO
    return 0;
}

/**
 * Adds a file at the end of the archive, at the archive's root level.
 * The archive's metadata must be updated accordingly.
 * For the file header, only the name, size, typeflag, magic value (to "ustar"), version value (to "00") and checksum fields need to be correctly set.
 *
 * @param tar_fd A file descriptor pointing to the start of a valid tar archive file.
 * @param filename The name of the file to add. If an entry already exists with the same name, the file is not written, and the function returns -1.
 * @param src A source buffer containing the file content to add.
 * @param len The length of the source buffer.
 *
 * @return 0 if the file was added successfully,
 *         -1 if the archive already contains an entry at the given path,
 *         -2 if an error occurred
 */
int add_file(int tar_fd, char *filename, uint8_t *src, size_t len) {
    // TODO
    return 0;
}
