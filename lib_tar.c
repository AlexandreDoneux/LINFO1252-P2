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
int is_direct_child(const char *file, const char *dir)
{
    size_t dlen = strlen(dir);

    // si dir root ("")
    if (dlen == 0) {
        return strchr(file, '/') == NULL; // vérifie qu'il n'y a pas de "/" dans path de file => sans "/" le fichier est à la racine
    }

    // commence pas avec dir
    if (strncmp(file, dir, dlen) != 0)
        return 0;

    // vérifie qu'il n'y a pas de "/" après
    return strchr(file + dlen, '/') == NULL;
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

    // pas besoin de checker l'archive, on suppose qu'elle est valide

    // vérification si path null -> définit le path comme étant la racine
    // Si path donné est un symlink -> traduction du path

    // but des paramètres entries et no_entries :
    //      -> remplir entries avec les entrées trouvées dans le répertoire path
    //      -> no_entries est un nombre :
    //          - en entrée : nombre max d'entrées que peut contenir entries
    //          - en sortie : nombre d'entrées effectivement trouvées et écrites dans entries => doit être <= no_entries en entrée (-1 si au delà ?)





    // vérification si path null -> définit le path comme étant la racine
    // Si path donné est un symlink -> traduction du path (utilisation de is_symlink pour vérifier)
    //

    // Comme pour exists on peut lire les headers un par un et vérifier si ils sont dans le répertoire path

    //      -> on vérifie si symlink avec is_symlink + traduction ??? => pas sur => pense pas
    //      -> on construit le path complet avec header_path
    //      -> on vérifie si le path complet est dans le répertoire path (un seul fichier dir, pas de récursivité)
    //      -> si oui on l'ajoute dans entries (si on n'a pas dépassé no_entries en entrée)
    //      -> on incrémente un compteur pour no_entries en sortie

    // ATTENTION : on ne peut pas utiliser realpath() car il résout les symlinks du système de fichiers, or on travaille sur une archive tar

    if (no_entries == NULL || entries == NULL) return -1;

    // if path is NULL, set it to the tar root
    //char list_path[PATHBUF];
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

    printf("4\n");

    printf("list path: %s\n", list_path);



    if (lseek(tar_fd, 0, SEEK_SET) == (off_t)-1) return -1; // moves the file descriptor to the beginning of the tar => really needed ?

    tar_header_t header;
    char file_path[PATHBUF];

    while (1) {
        const ssize_t r = read(tar_fd, &header, sizeof(header));
        if (r != (ssize_t)sizeof(header)) {
            return -1;
        }
        if (is_zero_block((const uint8_t *)&header)) {
            return 0;
        }
        if (header_path(&header, file_path) == -1) {
            return -1;
        }

        // vérification ici !

        // add to entries if match
        if (is_direct_child(file_path, list_path)) {
            printf("direct child found: %s\n", file_path);
        }

        // ok mais affiche aussi le dir comme étant un enfant direct de lui-même => à corriger

        // moves file descriptor to next header by skipping file content
        off_t size = (off_t)TAR_INT(header.size);
        off_t skip = round_up_512(size);
        if (skip > 0 && lseek(tar_fd, skip, SEEK_CUR) == (off_t)-1) {
            return -1;
        }
    }







    printf("list absolute path: %s\n", list_path);




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

    // pas besoin de checker l'archive, on suppose qu'elle est valide

    // écriture dans des fichiers abordés dans le syllabus
    return 0;
}
