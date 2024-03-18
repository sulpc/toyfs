#include "toyfs_utils.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>

#include "toyfs.h"

void util_dump(uint8_t* block, int size) {
#if 0
    int res = size & 0x0F;
    size    = size - res;

    for (int i = 0; i < size; i += 16) {
        for (int j = 0; j < 16; j++) {
            printf("%02X ", *block++);
        }
        printf("\n");
    }

    if (res) {
        for (int j = 0; j < res; j++) {
            printf("%02X ", *block++);
        }
        printf("\n");
    }
#endif
}

bool str_startwith(const char* s1, const char* s2) {
    if (s1 == nullptr)
        return false;
    if (s2 == nullptr)
        return true;
    if (s1 == s2)
        return true;

    while (*s1 != 0 && *s2 != 0 && *s1 == *s2) {
        s1++;
        s2++;
    }

    return *s2 == 0 ? true : false;
}

bool str_endwith(const char* s1, const char* s2) {
    // todo
    return false;
}

// if name not accord with 8dot3, the sfn will be wrong
void util_name2sfn(const char* name, char* sfn) {
    memset(sfn, ' ', 11);
    sfn[11] = '\0';

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        memcpy(sfn, name, strlen(name));
        return;
    }

    int i;
    for (i = 0; *name != '\0' && *name != '.' && i < 8; i++) {
        sfn[i] = toupper(*name++);
    }

    if (*name == '\0') {
        return;
    }
    if (*name++ == '.') {
        if (*name == '\0') {   // name may like: "xxx."
            sfn[i]     = '.';
            sfn[i + 1] = '\0';
            return;
        }

        for (i = 8; *name != '\0' && i < 11; i++) {
            sfn[i] = toupper(*name++);
        }
    }
}

void util_sfn2name(const char* sfn, char* name) {
    int i;
    for (i = 0; i < 8 && sfn[i] != ' '; i++) {
        *name++ = tolower(sfn[i]);
    }

    if (sfn[8] == ' ') {
        *name = '\0';
        return;
    }

    *name++ = '.';

    for (i = 8; i < 11 && sfn[i] != ' '; i++) {
        *name++ = tolower(sfn[i]);
    }
    *name = '\0';
}

int util_get_1st_subpath(const char* subpath, char* name) {
    int i = 0;
    while (subpath[i] != '\0' && subpath[i] != '/' && i < TF_FN_LEN_MAX) {
        name[i] = subpath[i];
        i++;
    }
    if (subpath[i] != '\0' && subpath[i] != '/') {
        return TF_ERR_LFN_NOT_SUPPORTED;
    }
    name[i] = '\0';
    return i;
}

/**
 * @brief get the value from block data
 *
 * @param block
 * @param ofs
 * @param size <= 4
 * @return uint32_t
 */
uint32_t util_get_value_from_block(uint8_t* block, int ofs, int size) {
    uint32_t value = 0;
    for (int i = 0; i < size; i++) {
        value |= ((uint32_t)block[ofs + i]) << (8 * i);
    }
    return value;
}
