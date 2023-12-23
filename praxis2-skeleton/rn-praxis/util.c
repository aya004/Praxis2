#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


char* memstr(char* haystack, size_t n, string needle) {
    char* end = haystack + n;

    // Iterate through the memory (haystack)
    while ((haystack = memchr(haystack, needle[0], end - haystack)) != NULL) {
        if (strncmp(haystack, needle, strlen(needle)) == 0) {
            return haystack;
        }
    }

    return NULL;
}


uint16_t safe_strtoul(const char *restrict nptr, char **restrict endptr, int base, const string message) {
    errno = 0;
    uint16_t result = strtoul(nptr, endptr, base); // Convert string to unsigned int

    if (errno != 0) {
        fprintf(stderr, "%s\n", message);
        exit(EXIT_FAILURE);
    }
    return result;
}
