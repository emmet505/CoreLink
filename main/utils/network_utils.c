#include "network_utils.h"

#include "string.h"

bool is_valid_ipv4(const char *ip) {
    if ( !ip || strlen(ip) < 7 || strlen(ip) > 15 ) return false;
    int parts [4];
    char dummy;

    if (sscanf(ip, "%d.%d.%d.%d%c",
                &parts[0], &parts[1],
                &parts[2], &parts[3], &dummy) != 4){
        return false;
    }

    for (int i = 0; i < 4; i++) {
        if (parts[i] < 0 || parts[i] > 255) return false;
    }
    return true;
}