/*-
 * Dumps decrypted image of DVD to stdout.
 * Copyright (c) 2011 Alexander Gattin (xrgtn).
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License (<http://www.gnu.org/licenses/>)
 * for more details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dvdread/dvd_reader.h>
#include <dvdread/dvd_udf.h>
#include <dvdcss/dvdcss.h>

static int dumpsector(unsigned char *);
struct se_s {
    char fname[23];
    unsigned int start;
    unsigned int end;
};
typedef struct se_s se_t;
#define MAXTITLES 100
#define MAXVTSVOBS 10
#define MAXVOBS (MAXTITLES*MAXVTSVOBS)
se_t vob[MAXVOBS];
int nvobs = 0;

static int findvob(int pos) {
    int i;
    for (i = 0; i < nvobs; i++) {
        if (vob[i].start <= pos && pos < vob[i].end) return i;
    }
    return -1;
}
static int addvob(char *tfname, int start, int len) {
    int end = start + (len + DVD_VIDEO_LB_LEN - 1) / DVD_VIDEO_LB_LEN;
    if (nvobs >= MAXVOBS) {
        fprintf(stderr, "can't add %s [%u -- %u)\n", tfname, start, end);
        return 0;
    }
    strncpy(vob[nvobs].fname, tfname, sizeof(vob[nvobs].fname));
    vob[nvobs].start = start;
    vob[nvobs++].end = end;
    return 1;
}

int main(int argc, char *argv[]) {
    dvd_reader_t *prdr = NULL;
    dvdcss_t dvdcss;
    unsigned char p_data[DVDCSS_BLOCK_SIZE * 2];
    unsigned char *p_buffer;
    unsigned int  s, ss, s1, s2;
    char tfname[23];
    uint32_t start, len;
    int t, v, curvob, s_vob, r;
    char *st = "init";
    /* usage */
    if (argc < 2 || argc > 4) {
        fprintf(stderr, "usage: %s <src> [<s1> [<s2>]]\n", argv[0]);
        fprintf(stderr, "examples:\n");
        fprintf(stderr, "  %s /dev/hdc >/tmp/decss.iso\n", argv[0]);
        fprintf(stderr, "  %s cssed.iso >decss.iso\n", argv[0]);
        return -1;
    }
    s1 = (argc < 3) ? 0 : atoi(argv[2]);
    s2 = (argc < 4) ? -1 : atoi(argv[3]);
    prdr = DVDOpen(argv[1]);
    if (prdr == NULL) {
        fprintf(stderr, "can't open %s\n", argv[1]);
        return 1;
    }
    snprintf(tfname, sizeof(tfname), "/VIDEO_TS/VIDEO_TS.VOB");
    start = UDFFindFile(prdr, tfname, &len);
    if (start && len) addvob(tfname, start, len);
    for (t = 0; t < MAXTITLES; t++) {
        for (v = 0; v < MAXVTSVOBS; v++) {
            snprintf(tfname, sizeof(tfname),
                "/VIDEO_TS/VTS_%02d_%d.VOB", t, v);
            start = UDFFindFile(prdr, tfname, &len);
            if (!start || !len) break;
            addvob(tfname, start, len);
        }
        if (t != 0 && v == 0 && (!start || !len)) break;
    }
    DVDClose(prdr);
    /* Initialize libdvdcss */
    dvdcss = dvdcss_open(argv[1]);
    if (dvdcss == NULL) {
        fprintf(stderr, "can't open %s\n", argv[1]);
        return 1;
    }
    if (dvdcss_is_scrambled(dvdcss))
        fprintf(stderr, "%s disk is scrambled\n", argv[1]);
    /* Align our read buffer */
    p_buffer = p_data + DVDCSS_BLOCK_SIZE
        - ((long int)p_data & (DVDCSS_BLOCK_SIZE-1));
    for (s = s1, ss = s1, curvob = -1; s < s2; s++) {
        s_vob = curvob;
        if (curvob < 0 || s < vob[curvob].start
                || vob[curvob].end <= s)
            s_vob = findvob(s);
        /* Advance to new line if VOB/section changes: */
        if (s_vob != curvob && s != s1) {
            fprintf(stderr, "\n");
            ss = s;
        };
        /* Print current sector or sectors range: */
        if (s == ss) fprintf(stderr, "\r%u ", s);
        else fprintf(stderr, "\r%u - %u ", ss, s);
        /* Append VOB name if any: */
        if (s_vob >= 0) fprintf(stderr, "(%s) ", vob[s_vob].fname);
        /* Seek for VOB key if VOB changes, skip otherwise: */
        st = "seek";
        if (s_vob >= 0 && curvob != s_vob) {
            st = "seek key";
            r = dvdcss_seek(dvdcss, s, DVDCSS_SEEK_KEY);
        } else r = dvdcss_seek(dvdcss, s, DVDCSS_NOFLAGS);
        if (r != (int)s) goto CSSERR;
        /* Decrypt if inside VOB, read plain data otherwise: */
        st = "read";
        curvob = s_vob;
        if (curvob >= 0) {
            st = "decrypt";
            r = dvdcss_read(dvdcss, p_buffer, 1, DVDCSS_READ_DECRYPT);
        } else r = dvdcss_read(dvdcss, p_buffer, 1, DVDCSS_NOFLAGS);
        if (r == 0) goto EOFDVD;
        if (r != 1) goto CSSERR;
        if (!dumpsector(p_buffer)) goto STDERR;
    }
EOFDVD:
    fprintf(stderr, "end of file %s\n", argv[1]);
    dvdcss_close(dvdcss);
    return 0;
STDERR:
    perror(NULL);
    dvdcss_close(dvdcss);
    return 1;
CSSERR:
    fprintf(stderr, "%s - %s\n", st, dvdcss_error(dvdcss));
    dvdcss_close(dvdcss);
    return 1;
}

/* Dumps DVDCSS_BLOCK_SIZE bytes buffer to stdout */
static int dumpsector(unsigned char *p_buffer) {
    int r = fwrite(p_buffer, DVDCSS_BLOCK_SIZE, 1, stdout);
    return r == 1;
}

/* vi:set sw=4 et ts=8 tw=71 ai: */
