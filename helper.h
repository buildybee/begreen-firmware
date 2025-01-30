#ifndef HELPER_H
#define HELPER_H

#include <cstring>

bool v1GreaterThanV2(const char v1[], const char v2[]) {
    int i = 0, j = 0, vnum1, vnum2;

    while (v1[i] || v2[j]) {
        vnum1 = vnum2 = 0;

        while (v1[i] && v1[i] != '.') vnum1 = vnum1 * 10 + (v1[i++] - '0');
        while (v2[j] && v2[j] != '.') vnum2 = vnum2 * 10 + (v2[j++] - '0');

        if (vnum1 > vnum2) return true;
        if (vnum2 > vnum1) return false;

        if (v1[i] == '.') i++;
        if (v2[j] == '.') j++;
    }
    return false;
}

#endif
