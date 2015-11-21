/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/** THIS FILE MUST BE REMOVED WHEN THE APR-UTIL LIBRARY WILL BE FIXED
 * AND PACKAGED. */

/* mbox_date_parse_rfc: Since the apr_date_parse_rfc() function is
 * buggy in current versions of the Apache Portable Runtime Library,
 * we use a fixed version copied here while the fixed version will be
 * uploaded and available.
 *
 * mbox_strcasestr: ap_strcasestr is currently held in HTTPd source
 * code instead of APR. strcasestr is not portable on Solaris, and we
 * need a portable strcasestr even without linking to HTTPd (for
 * mod-mbox-util).
 */

#include "mbox_externals.h"

#if APR_HAVE_STDLIB_H
#include <stdlib.h>
#endif

#if APR_HAVE_CTYPE_H
#include <ctype.h>
#endif

/* Compare a string to a mask. See apr-util/misc/apr_date.c for more
 * information.
 */
static int mbox_date_checkmask(const char *data, const char *mask)
{
    int i;
    char d;

    for (i = 0; i < 256; i++) {
        d = data[i];
        switch (mask[i]) {
        case '\0':
            return (d == '\0');

        case '*':
            return 1;

        case '@':
            if (!apr_isupper(d))
                return 0;
            break;
        case '$':
            if (!apr_islower(d))
                return 0;
            break;
        case '#':
            if (!apr_isdigit(d))
                return 0;
            break;
        case '&':
            if (!apr_isxdigit(d))
                return 0;
            break;
        case '~':
            if ((d != ' ') && !apr_isdigit(d))
                return 0;
            break;
        default:
            if (mask[i] != d)
                return 0;
            break;
        }
    }

    return 0;                   /* We only get here if mask is corrupted (exceeds 256) */
}

/* Parses RFC 822 or RFC 2822 dates. */

#define TIMEPARSE(ds,hr10,hr1,min10,min1,sec10,sec1)        \
    {                                                       \
        ds.tm_hour = ((hr10 - '0') * 10) + (hr1 - '0');     \
        ds.tm_min = ((min10 - '0') * 10) + (min1 - '0');    \
        ds.tm_sec = ((sec10 - '0') * 10) + (sec1 - '0');    \
    }
#define TIMEPARSE_STD(ds,timstr)                            \
    {                                                       \
        TIMEPARSE(ds, timstr[0],timstr[1],                  \
                      timstr[3],timstr[4],                  \
                      timstr[6],timstr[7]);                 \
    }

apr_time_t mbox_date_parse_rfc(const char *date)
{
    apr_time_exp_t ds;
    apr_time_t result;
    int mint, mon;
    const char *monstr, *timstr, *gmtstr;
    static const int months[12] = {
        ('J' << 16) | ('a' << 8) | 'n', ('F' << 16) | ('e' << 8) | 'b',
        ('M' << 16) | ('a' << 8) | 'r', ('A' << 16) | ('p' << 8) | 'r',
        ('M' << 16) | ('a' << 8) | 'y', ('J' << 16) | ('u' << 8) | 'n',
        ('J' << 16) | ('u' << 8) | 'l', ('A' << 16) | ('u' << 8) | 'g',
        ('S' << 16) | ('e' << 8) | 'p', ('O' << 16) | ('c' << 8) | 't',
        ('N' << 16) | ('o' << 8) | 'v', ('D' << 16) | ('e' << 8) | 'c'
    };

    if (!date)
        return APR_DATE_BAD;

    /* Not all dates have text months at the beginning. */
    if (!apr_isdigit(date[0])) {
        while (*date && apr_isspace(*date))     /* Find first non-whitespace char */
            ++date;

        if (*date == '\0')
            return APR_DATE_BAD;

        if ((date = strchr(date, ' ')) == NULL) /* Find space after weekday */
            return APR_DATE_BAD;

        ++date;                 /* Now pointing to first char after space, which should be */
    }

    /* start of the actual date information for all 11 formats. */
    if (mbox_date_checkmask(date, "## @$$ #### ##:##:## *")) {  /* RFC 1123 format */
        ds.tm_year = ((date[7] - '0') * 10 + (date[8] - '0') - 19) * 100;

        if (ds.tm_year < 0)
            return APR_DATE_BAD;

        ds.tm_year += ((date[9] - '0') * 10) + (date[10] - '0');

        ds.tm_mday = ((date[0] - '0') * 10) + (date[1] - '0');

        monstr = date + 3;
        timstr = date + 12;
        gmtstr = date + 21;

        TIMEPARSE_STD(ds, timstr);
    }
    else if (mbox_date_checkmask(date, "##-@$$-## ##:##:## *")) {       /* RFC 850 format  */
        ds.tm_year = ((date[7] - '0') * 10) + (date[8] - '0');

        if (ds.tm_year < 70)
            ds.tm_year += 100;

        ds.tm_mday = ((date[0] - '0') * 10) + (date[1] - '0');

        monstr = date + 3;
        timstr = date + 10;
        gmtstr = date + 19;

        TIMEPARSE_STD(ds, timstr);
    }
    else if (mbox_date_checkmask(date, "@$$ ~# ##:##:## ####*")) {
        /* asctime format */
        ds.tm_year = ((date[16] - '0') * 10 + (date[17] - '0') - 19) * 100;
        if (ds.tm_year < 0)
            return APR_DATE_BAD;

        ds.tm_year += ((date[18] - '0') * 10) + (date[19] - '0');

        if (date[4] == ' ')
            ds.tm_mday = 0;
        else
            ds.tm_mday = (date[4] - '0') * 10;

        ds.tm_mday += (date[5] - '0');

        monstr = date;
        timstr = date + 7;
        gmtstr = NULL;

        TIMEPARSE_STD(ds, timstr);
    }
    else if (mbox_date_checkmask(date, "# @$$ #### ##:##:## *")) {
        /* RFC 1123 format */
        ds.tm_year = ((date[6] - '0') * 10 + (date[7] - '0') - 19) * 100;

        if (ds.tm_year < 0)
            return APR_DATE_BAD;

        ds.tm_year += ((date[8] - '0') * 10) + (date[9] - '0');
        ds.tm_mday = (date[0] - '0');

        monstr = date + 2;
        timstr = date + 11;
        gmtstr = date + 20;

        TIMEPARSE_STD(ds, timstr);
    }
    else if (mbox_date_checkmask(date, "## @$$ ## ##:##:## *")) {
        /* This is the old RFC 1123 date format - many many years ago, people
         * used two-digit years.  Oh, how foolish.  */
        ds.tm_year = ((date[7] - '0') * 10) + (date[8] - '0');

        if (ds.tm_year < 70)
            ds.tm_year += 100;

        ds.tm_mday = ((date[0] - '0') * 10) + (date[1] - '0');

        monstr = date + 3;
        timstr = date + 10;
        gmtstr = date + 19;

        TIMEPARSE_STD(ds, timstr);
    }
    else if (mbox_date_checkmask(date, "# @$$ ## ##:##:## *")) {
        /* This is the old RFC 1123 date format - many many years ago, people
         * used two-digit years.  Oh, how foolish.  */
        ds.tm_year = ((date[6] - '0') * 10) + (date[7] - '0');

        if (ds.tm_year < 70)
            ds.tm_year += 100;

        ds.tm_mday = (date[0] - '0');

        monstr = date + 2;
        timstr = date + 9;
        gmtstr = date + 18;

        TIMEPARSE_STD(ds, timstr);
    }
    else if (mbox_date_checkmask(date, "## @$$ ## ##:## *")) {
        /* Loser format.  This is quite bogus.  */
        ds.tm_year = ((date[7] - '0') * 10) + (date[8] - '0');

        if (ds.tm_year < 70)
            ds.tm_year += 100;

        ds.tm_mday = ((date[0] - '0') * 10) + (date[1] - '0');

        monstr = date + 3;
        timstr = date + 10;
        gmtstr = NULL;

        TIMEPARSE(ds, timstr[0], timstr[1], timstr[3], timstr[4], '0', '0');
    }
    else if (mbox_date_checkmask(date, "# @$$ ## ##:## *")) {
        /* Loser format.  This is quite bogus.  */
        ds.tm_year = ((date[6] - '0') * 10) + (date[7] - '0');

        if (ds.tm_year < 70)
            ds.tm_year += 100;

        ds.tm_mday = (date[0] - '0');

        monstr = date + 2;
        timstr = date + 9;
        gmtstr = NULL;

        TIMEPARSE(ds, timstr[0], timstr[1], timstr[3], timstr[4], '0', '0');
    }
    else if (mbox_date_checkmask(date, "## @$$ ## #:##:## *")) {
        /* Loser format.  This is quite bogus.  */
        ds.tm_year = ((date[7] - '0') * 10) + (date[8] - '0');

        if (ds.tm_year < 70)
            ds.tm_year += 100;

        ds.tm_mday = ((date[0] - '0') * 10) + (date[1] - '0');

        monstr = date + 3;
        timstr = date + 9;
        gmtstr = date + 18;

        TIMEPARSE(ds, '0', timstr[1], timstr[3], timstr[4], timstr[6],
                  timstr[7]);
    }
    else if (mbox_date_checkmask(date, "# @$$ ## #:##:## *")) {
        /* Loser format.  This is quite bogus.  */
        ds.tm_year = ((date[6] - '0') * 10) + (date[7] - '0');

        if (ds.tm_year < 70)
            ds.tm_year += 100;

        ds.tm_mday = (date[0] - '0');

        monstr = date + 2;
        timstr = date + 8;
        gmtstr = date + 17;

        TIMEPARSE(ds, '0', timstr[1], timstr[3], timstr[4], timstr[6],
                  timstr[7]);
    }
    else if (mbox_date_checkmask(date, " # @$$ #### ##:##:## *")) {
        /* RFC 1123 format with a space instead of a leading zero. */
        ds.tm_year = ((date[7] - '0') * 10 + (date[8] - '0') - 19) * 100;

        if (ds.tm_year < 0)
            return APR_DATE_BAD;

        ds.tm_year += ((date[9] - '0') * 10) + (date[10] - '0');

        ds.tm_mday = (date[1] - '0');

        monstr = date + 3;
        timstr = date + 12;
        gmtstr = date + 21;

        TIMEPARSE_STD(ds, timstr);
    }
    else if (mbox_date_checkmask(date, "##-@$$-#### ##:##:## *")) {
        /* RFC 1123 with dashes instead of spaces between date/month/year
         * This also looks like RFC 850 with four digit years.
         */
        ds.tm_year = ((date[7] - '0') * 10 + (date[8] - '0') - 19) * 100;
        if (ds.tm_year < 0)
            return APR_DATE_BAD;

        ds.tm_year += ((date[9] - '0') * 10) + (date[10] - '0');

        ds.tm_mday = ((date[0] - '0') * 10) + (date[1] - '0');

        monstr = date + 3;
        timstr = date + 12;
        gmtstr = date + 21;

        TIMEPARSE_STD(ds, timstr);
    }
    else
        return APR_DATE_BAD;

    if (ds.tm_mday <= 0 || ds.tm_mday > 31)
        return APR_DATE_BAD;

    if ((ds.tm_hour > 23) || (ds.tm_min > 59) || (ds.tm_sec > 61))
        return APR_DATE_BAD;

    mint = (monstr[0] << 16) | (monstr[1] << 8) | monstr[2];
    for (mon = 0; mon < 12; mon++)
        if (mint == months[mon])
            break;

    if (mon == 12)
        return APR_DATE_BAD;

    if ((ds.tm_mday == 31) && (mon == 3 || mon == 5 || mon == 8 || mon == 10))
        return APR_DATE_BAD;

    /* February gets special check for leapyear */

    if ((mon == 1) && ((ds.tm_mday > 29)
                       || ((ds.tm_mday == 29)
                           && ((ds.tm_year & 3)
                               || (((ds.tm_year % 100) == 0)
                                   && (((ds.tm_year % 400) != 100)))))))
        return APR_DATE_BAD;

    ds.tm_mon = mon;

    /* tm_gmtoff is the number of seconds off of GMT the time is.
     *
     * We only currently support: [+-]ZZZZ where Z is the offset in
     * hours from GMT.
     *
     * If there is any confusion, tm_gmtoff will remain 0.
     */
    ds.tm_gmtoff = 0;

    /* Do we have a timezone ? */
    if (gmtstr) {
        int offset;
        switch (*gmtstr) {
        case '-':
            offset = atoi(gmtstr + 1);
            ds.tm_gmtoff -= (offset / 100) * 60 * 60;
            ds.tm_gmtoff -= (offset % 100) * 60;
            break;
        case '+':
            offset = atoi(gmtstr + 1);
            ds.tm_gmtoff += (offset / 100) * 60 * 60;
            ds.tm_gmtoff += (offset % 100) * 60;
            break;
        }
    }

    /* apr_time_exp_get uses tm_usec field, but it hasn't been set yet.
     * It should be safe to just zero out this value.
     * tm_usec is the number of microseconds into the second.  HTTP only
     * cares about second granularity.
     */
    ds.tm_usec = 0;

    if (apr_time_exp_gmt_get(&result, &ds) != APR_SUCCESS)
        return APR_DATE_BAD;

    return result;
}

/*
 * Similar to standard strstr() but we ignore case in this version.
 * Based on the strstr() implementation further below.
 */
char *mbox_strcasestr(const char *s1, const char *s2)
{
    char *p1, *p2;
    if (*s2 == '\0') {
        /* an empty s2 */
        return ((char *) s1);
    }
    while (1) {
        for (; (*s1 != '\0') && (apr_tolower(*s1) != apr_tolower(*s2)); s1++);
        if (*s1 == '\0') {
            return (NULL);
        }
        /* found first character of s2, see if the rest matches */
        p1 = (char *) s1;
        p2 = (char *) s2;
        for (++p1, ++p2; apr_tolower(*p1) == apr_tolower(*p2); ++p1, ++p2) {
            if (*p1 == '\0') {
                /* both strings ended together */
                return ((char *) s1);
            }
        }
        if (*p2 == '\0') {
            /* second string ended, a match */
            break;
        }
        /* didn't find a match here, try starting at next character in s1 */
        s1++;
    }
    return ((char *) s1);
}
