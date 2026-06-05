/* Copyright (C) 2026 BCTaoTao. Licensed under LGPLv3. */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "util.h"

char* my_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *result = (char *)malloc(len);
    if (result) {
        memcpy(result, s, len);
    }
    return result;
}

char* trim_whitespace(char *str) {
    while (*str == ' ' || *str == '\t') str++;
    char *end = str + strlen(str);
    while (end > str && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) {
        end--;
    }
    *end = '\0';
    return str;
}
