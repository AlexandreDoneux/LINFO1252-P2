#include "lib_tar.h"
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <libgen.h>

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

/*
 * Vérifie si "file" est un enfant direct de "dir".
 *
 * Renvoie 1 si c'est le cas, 0 sinon.
 */
/*
int is_direct_child(const char *file, const char *dir)
{
    size_t dlen = strlen(dir);
    //printf("dir : %s, dlen: %zu\n", dir, dlen);
    //printf("file : %s\n", file);

    // si dir root ("")
    if (dlen == 0) {
        return strchr(file, '/') == NULL; // vérifie qu'il n'y a pas de "/" dans path de file => sans "/" le fichier est à la racine
    }

    // if file path does not start with dir path -> not good
    if (strncmp(file, dir, dlen) != 0)
        return 0;

    // If file equals dir -> not good
    if (file[dlen] == '\0')
        return 0;

    // vérifie qu'il n'y a pas de "/" après
    return strchr(file + dlen, '/') == NULL;
}
*/
// better implement that handles "/" better
int is_direct_child(const char *file, const char *dir)
{
    size_t dlen = strlen(dir);

    if (dlen == 0) {
        char *slash = strchr(file, '/');
        return slash == NULL || slash[1] == '\0';
    }

    if (strncmp(file, dir, dlen) != 0)
        return 0;

    const char *rest = file + dlen;
    if (*rest == '\0')
        return 0;

    char *slash = strchr(rest, '/');
    return slash == NULL || slash[1] == '\0';
}


static int find_entry(int tar_fd, const char *path, tar_header_t *out) {
    if (!path) return -1;
    if (lseek(tar_fd, 0, SEEK_SET) == (off_t)-1) return -1;

    tar_header_t h;
    char fullpath[512];

    while (1) {
        ssize_t r = read(tar_fd, &h, sizeof(h));
        if (r != (ssize_t)sizeof(h)) return -1;

        if (is_zero_block((const uint8_t *)&h)) return 0;

        if (header_path(&h, fullpath) == -1) return -1;

        // if path + "/" is equal to fullpath, we might have found a symlink directory
        // so we check if h is a symlink and if so, we resolve it to its target
        size_t flen = strlen(fullpath);
        if (flen + 1 < PATHBUF) {
            char fullpath_slash[PATHBUF];
            memcpy(fullpath_slash, fullpath, flen);
            fullpath_slash[flen] = '/';
            fullpath_slash[flen + 1] = '\0';

            //printf("comparing with symlink dir: %s\n", fullpath_slash);
            //printf("comparing with dir: %s\n", path);

            if (strcmp(fullpath_slash, path) == 0) {
                //printf("passes\n");
                if (h.typeflag == SYMTYPE) {
                    //printf("passes2\n");
                    /* copy and null-terminate link target safely */
                    char link_target[PATHBUF];
                    size_t ln_sz = sizeof(h.linkname);
                    size_t cp = ln_sz < (PATHBUF - 1) ? ln_sz : (PATHBUF - 1);
                    memcpy(link_target, h.linkname, cp);
                    link_target[cp] = '\0';

                    /* try resolving the symlink to its target entry */
                    int res = find_entry(tar_fd, link_target, out);
                    //printf("passes2bis\n");
                    if (res == 1) return 1;
                    if (res == -1) return -1;

                    /* if direct target not found, try appending a trailing slash to the link target */
                    size_t lt_len = strlen(link_target);
                    if (lt_len + 1 < PATHBUF) {
                        //printf("passes3\n");
                        link_target[lt_len] = '/';
                        link_target[lt_len + 1] = '\0';
                        res = find_entry(tar_fd, link_target, out);
                        //printf("res after adding slash: %d\n", res);
                        //printf("out path after adding slash: ");
                        /*
                        if (res == 1 && out) {
                            char out_path[PATHBUF];
                            header_path(out, out_path);
                            printf("%s\n", out_path); // works well, finds the correct path for the symlink target
                        }
                        */
                        //printf("before return after adding slash\n");
                        return res;
                    }
                    //printf("passes4\n");
                    return 0;
                } else {
                    //printf("passes5\n");
                    /* header represents the directory (with trailing slash in comparison) */
                    if (out) *out = h;
                    return 1;
                }
            }
        }


        if (strcmp(fullpath, path) == 0) {
            if (out) *out = h;
            return 1;
        }

        off_t size = (off_t)TAR_INT(h.size);
        off_t skip = round_up_512(size);
        if (skip > 0 && lseek(tar_fd, skip, SEEK_CUR) == (off_t)-1) return -1;
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
    tar_header_t h;
    int r = find_entry(tar_fd, path, &h);
    if (r <= 0) return 0;
    return (h.typeflag == DIRTYPE) ? 1 : 0;
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
    tar_header_t h;
    int r = find_entry(tar_fd, path, &h);
    if (r <= 0) return 0;
    return (h.typeflag == REGTYPE || h.typeflag == AREGTYPE) ? 1 : 0;
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
    tar_header_t h;
    int r = find_entry(tar_fd, path, &h);
    if (r <= 0) return 0;
    return (h.typeflag == SYMTYPE) ? 1 : 0;
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
    // pas besoin de checker l'archive, on suppose qu'elle est valide

    if (no_entries == NULL || entries == NULL) return -1;


    // check symlink here + resolve to linked-to entry
    if (path != NULL && strcmp(path, "") != 0 && is_symlink(tar_fd, path)) {
        tar_header_t h;
        int r = find_entry(tar_fd, path, &h); // find_entry() toujours un problème car quand on récupères le path d'un symlink, on n'a pas le "/" à la fin
        //find_entry(tar_fd, path, &h); // find_entry() toujours un problème car quand on récupères le path d'un symlink, on n'a pas le "/" à la fin


        // find_entry works and seems to give the correct real path for the symlink target, but despite the return valuea being 1,
        // the prints here don't print and the list() function returns 1 despite giving nothing in the entries array
        //printf(" r from find_entry: %d\n", r);
        if (r == -1) return -1;

        //printf("hey\n");

        header_path(&h, path);
        //printf("resolved symlink path: %s\n", path);

        /*
        if (r == -1) return -1;
        if (r == 1 && h.typeflag == SYMTYPE) {
            // resolve symlink
            char link_target[PATHBUF];
            memcpy(link_target, h.linkname, sizeof(h.linkname));
            link_target[sizeof(h.linkname)] = '\0';

            // update path to linked-to entry
            path = link_target;
            printf("paht is symlink, resolved to: %s\n", path);
        }
        */
    }

    // needs to return 0 if no directory at the given path exists in the archive
    if (path != NULL && strcmp(path, "") != 0 && is_dir(tar_fd, path) == 0) {
        //printf("no path, not a dir or not root \n");
        *no_entries = 0;
        return 0;
    }



    // if path is NULL, set it to the tar root
    char *list_path;
    char normalized_path[PATHBUF]; // path with trailing slash

    if (!path || strcmp(path, "") == 0) {
        list_path = "";          // tar root
    } else {
        strncpy(normalized_path, path, sizeof(normalized_path) - 1);
        normalized_path[sizeof(normalized_path) - 1] = '\0';

        // être sûr d'avoir un slash à la fin -> ok de juste rajouter un caratère ?
        size_t len = strlen(normalized_path);
        if (normalized_path[len - 1] != '/') {
            normalized_path[len] = '/';
            normalized_path[len + 1] = '\0';
        }

        list_path = normalized_path;
    }

    //printf("list path: %s\n", list_path);


    if (lseek(tar_fd, 0, SEEK_SET) == (off_t)-1) return -1; // moves the file descriptor to the beginning of the tar => really needed ?

    tar_header_t header;
    char file_path[PATHBUF];
    int number_of_entries = 0;
    int max_number_of_entries = (int)(*no_entries);

    while (1) {
        // checks if we reached max entries
        if (number_of_entries >= max_number_of_entries) {
            return 1;
        }

        const ssize_t r = read(tar_fd, &header, sizeof(header));
        if (r != (ssize_t)sizeof(header)) {
            return -1;
        }
        if (is_zero_block((const uint8_t *)&header)) {
            return 1; // needs to return 1 if success
        }
        // get path
        if (header_path(&header, file_path) == -1) {
            return -1;
        }

        // vérification ici !


        // add to entries if match
        if (is_direct_child(file_path, list_path)) {
            //entries[number_of_entries] = file_path; // wrong ! Revient à faire pointer l'entrée vers la même zone mémoire à chaque fois
            //strncpy(entries[number_of_entries], file_path, PATHBUF); // copier le contenu -> PROBLEM HERE
            strncpy(entries[number_of_entries], file_path, strlen(file_path) + 1);

            number_of_entries++;
        }

        *no_entries = number_of_entries;

        // moves file descriptor to next header by skipping file content
        off_t size = (off_t)TAR_INT(header.size);
        off_t skip = round_up_512(size);
        if (skip > 0 && lseek(tar_fd, skip, SEEK_CUR) == (off_t)-1) {
            return -1;
        }
    }


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

    if (!filename || !src) return -2;


    // if entry already exists -> error
    if (exists(tar_fd, filename)) {
        perror("add_file () : file already exists");
        return -1;
    }


    // find end of archive (first zero block)
    if (lseek(tar_fd, 0, SEEK_SET) == (off_t)-1) return -2;

    tar_header_t h;
    while (1) {
        ssize_t r = read(tar_fd, &h, sizeof(h));
        if (r != (ssize_t)sizeof(h)) return -2;

        if (is_zero_block((const uint8_t *)&h)) {
            tar_header_t h2;
            ssize_t r2 = read(tar_fd, &h2, sizeof(h2));
            if (r2 != (ssize_t)sizeof(h2)) return -2;

            if (is_zero_block((const uint8_t *)&h2)) {
                /* two consecutive zero blocks -> rewind to start of the first zero block */
                if (lseek(tar_fd, -(off_t)(2 * BLOCKSIZE), SEEK_CUR) == (off_t)-1)
                    return -2;
                break;
            } else {
                /* false alarm: position back at start of h2 so next loop will process it */
                if (lseek(tar_fd, -(off_t)BLOCKSIZE, SEEK_CUR) == (off_t)-1)
                    return -2;
                continue;
            }
        }

        off_t size = (off_t)TAR_INT(h.size);
        off_t skip = round_up_512(size);
        if (skip && lseek(tar_fd, skip, SEEK_CUR) == (off_t)-1)
            return -2;
    }

    // build new header
    tar_header_t newh;
    memset(&newh, 0, sizeof(newh));

    // name
    strncpy(newh.name, filename, sizeof(newh.name));

    // size (octal)
    snprintf(newh.size, sizeof(newh.size), "%011o", (unsigned int)len);

    // type, magic et version
    newh.typeflag = REGTYPE;
    memcpy(newh.magic, TMAGIC, TMAGLEN);
    memcpy(newh.version, TVERSION, TVERSLEN);

    // checksum: fill with spaces first
    memset(newh.chksum, ' ', sizeof(newh.chksum));
    unsigned int cksum = compute_checksum(&newh);
    snprintf(newh.chksum, sizeof(newh.chksum), "%06o", cksum);
    newh.chksum[6] = '\0';
    newh.chksum[7] = ' ';


    // write header
    if (write(tar_fd, &newh, sizeof(newh)) != sizeof(newh)){
        return -2;
    }



    // write file data
    if (len > 0 && write(tar_fd, src, len) != (ssize_t)len){
        return -2;
    }


    // pad file data to 512 bytes
    size_t padding = round_up_512(len) - len;
    if (padding) {
        uint8_t pad[BLOCKSIZE] = {0};
        if (write(tar_fd, pad, padding) != (ssize_t)padding)
            return -2;
    }

    // write two zero blocks (end of archive)
    uint8_t zero[BLOCKSIZE] = {0};
    if (write(tar_fd, zero, BLOCKSIZE) != BLOCKSIZE) return -2;
    if (write(tar_fd, zero, BLOCKSIZE) != BLOCKSIZE) return -2;

    return 0;
}
