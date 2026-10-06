// Semver-ish version comparison, deliberately free of IDF headers so it can
// be unit-tested on the host with plain gcc.
#include "ota_internal.h"
#include <ctype.h>
#include <string.h>

static const char *skip_v(const char *s)
{
    return (s && (*s == 'v' || *s == 'V')) ? s + 1 : s;
}

static long parse_num(const char **p)
{
    long v = 0;
    const char *s = *p;
    while (isdigit((unsigned char)*s)) {
        v = v * 10 + (*s - '0');
        s++;
    }
    *p = s;
    return v;
}

int ota_version_cmp(const char *a, const char *b)
{
    if (!a) a = "";
    if (!b) b = "";
    a = skip_v(a);
    b = skip_v(b);

    // Compare dot-separated numeric components; a side that has run out
    // counts its remaining components as 0 (so "1.2" == "1.2.0", "" < "1").
    while (*a || *b) {
        bool da = isdigit((unsigned char)*a);
        bool db = isdigit((unsigned char)*b);
        if (!da && !db) break;
        long na = da ? parse_num(&a) : 0;
        long nb = db ? parse_num(&b) : 0;
        if (na != nb) return na < nb ? -1 : 1;
        if (*a == '.') a++;   // consume separator before next component
        if (*b == '.') b++;
    }

    // Numeric parts equal: a pre-release ("-rc1") sorts below a plain release
    bool pre_a = (*a == '-');
    bool pre_b = (*b == '-');
    if (pre_a != pre_b) return pre_a ? -1 : 1;
    if (pre_a && pre_b) {
        int c = strcmp(a, b);
        return c < 0 ? -1 : (c > 0 ? 1 : 0);
    }
    return 0;
}
