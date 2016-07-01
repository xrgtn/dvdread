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
enum vts_file_type_t {
    other,
    vmg_ifo,
    vmg_vob,
    vmg_bup,
    vts_ifo,
    vts_vob,
    vts_bup
};
struct vts_file_extent_s {
    char fname[23];
    int type;
    unsigned int start;
    unsigned int end;
};
typedef struct vts_file_extent_s vts_file_extent_t;
#define MAXTITLES 100
#define MAXVTSVOBS 10
#define MAXVFILES (MAXTITLES*(MAXVTSVOBS+2)+2)
vts_file_extent_t file[MAXVFILES];
int nfiles = 0;

static int findfile(int pos) {
    int i;
    for (i = 0; i < nfiles; i++) {
        if (file[i].start <= pos && pos < file[i].end) return i;
    }
    return -1;
}
static int addfile(char *fname, int start, int len) {
    char *bname, *ext;
    int type, title, part;
    int end = start + (len + DVD_VIDEO_LB_LEN - 1) / DVD_VIDEO_LB_LEN;
    if (fname == NULL) return 0;
    if (nfiles >= MAXVFILES) {
        fprintf(stderr, "can't add %s [%u -- %u)\n", fname, start, end);
        return 0;
    };
    bname = strrchr(fname, '/');
    bname = (bname == NULL) ? fname : bname + 1;
    ext = strrchr(bname, '.');
    if (ext == NULL) {
        type = 0;
    } else if (0 == strcasecmp(ext, ".IFO")) {
        type = 1;
    } else if (0 == strcasecmp(ext, ".VOB")) {
        type = 2;
    } else if (0 == strcasecmp(ext, ".BUP")) {
        type = 3;
    } else {
        type = 0;
    };
    if (type != 0) {
        if (0 == strncasecmp(bname, "VIDEO_TS", ext - bname)) {
        } else if (2 == sscanf(bname, "VTS_%d_%d", &title, &part)) {
            type += 3;
        } else {
            type = 0;
        };
    };
    switch (type) {
        case 1: file[nfiles].type = vmg_ifo; break;
        case 2: file[nfiles].type = vmg_vob; break;
        case 3: file[nfiles].type = vmg_bup; break;
        case 4: file[nfiles].type = vts_ifo; break;
        case 5: file[nfiles].type = vts_vob; break;
        case 6: file[nfiles].type = vts_bup; break;
        default: file[nfiles].type = other; break;
    };
    strncpy(file[nfiles].fname, fname, sizeof(file[nfiles].fname));
    file[nfiles].fname[sizeof(file[nfiles].fname) - 1] = '\0';
    file[nfiles].start = start;
    file[nfiles++].end = end;
    return 1;
}

int main(int argc, char *argv[]) {
    dvd_reader_t *prdr = NULL;
    dvdcss_t dvdcss;
    unsigned char p_data[DVDCSS_BLOCK_SIZE * 2];
    unsigned char *p_buffer;
    unsigned int  s, ss, s1, s2;
    char fname[23];
    uint32_t start, len;
    int t, v, lastfile, curfile, r;
    char *vmgfname[] = {
        "/VIDEO_TS/VIDEO_TS.IFO",
        "/VIDEO_TS/VIDEO_TS.VOB",
        "/VIDEO_TS/VIDEO_TS.BUP"};
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
    for (t = 0; t < sizeof(vmgfname) / sizeof(vmgfname[0]); t++) {
        start = UDFFindFile(prdr, vmgfname[t], &len);
        if (start && len) addfile(vmgfname[t], start, len);
    }
    for (t = 0; t < MAXTITLES; t++) {
        snprintf(fname, sizeof(fname), "/VIDEO_TS/VTS_%02d_0.IFO", t);
        start = UDFFindFile(prdr, fname, &len);
        if (start && len) addfile(fname, start, len);
        for (v = 0; v < MAXVTSVOBS; v++) {
            snprintf(fname, sizeof(fname),
                "/VIDEO_TS/VTS_%02d_%d.VOB", t, v);
            start = UDFFindFile(prdr, fname, &len);
            if (!start || !len) break;
            addfile(fname, start, len);
        }
        if (t != 0 && v == 0 && (!start || !len)) break;
        snprintf(fname, sizeof(fname), "/VIDEO_TS/VTS_%02d_0.BUP", t);
        start = UDFFindFile(prdr, fname, &len);
        if (start && len) addfile(fname, start, len);
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
    for (s = s1, ss = s1, lastfile = -1, curfile = -1; s < s2;
            s++, lastfile = curfile) {
        if (curfile < 0 || s < file[curfile].start
                || s >= file[curfile].end)
            curfile = findfile(s);
        /* Advance to new line if file/section changes: */
        if (curfile != lastfile && s != s1) {
            fprintf(stderr, "\n");
            ss = s;
        };
        /* Print current sector or sectors range: */
        if (s == ss) fprintf(stderr, "\r%u ", s);
        else fprintf(stderr, "\r%u - %u ", ss, s);
        /* Append file name if any: */
        if (curfile >= 0)
            fprintf(stderr, "(%s) ", file[curfile].fname);
        /* Seek for VOB key if entering new VOB, skip otherwise: */
        if (curfile >= 0 && (file[curfile].type == vmg_vob
                    || file[curfile].type == vts_vob)
                && curfile != lastfile) {
            st = "seek key";
            r = dvdcss_seek(dvdcss, s, DVDCSS_SEEK_KEY);
        } else {
            st = "seek";
            r = dvdcss_seek(dvdcss, s, DVDCSS_NOFLAGS);
        };
        if (r != (int)s) goto CSSERR;
        /* Decrypt if inside VOB, read plain data otherwise: */
        if (curfile >= 0 && (file[curfile].type == vmg_vob
                    || file[curfile].type == vts_vob)) {
            st = "decrypt";
            r = dvdcss_read(dvdcss, p_buffer, 1, DVDCSS_READ_DECRYPT);
        } else {
            st = "read";
            r = dvdcss_read(dvdcss, p_buffer, 1, DVDCSS_NOFLAGS);
        };
        if (r == 0) goto EOFDVD;
        if (r != 1) {
            /* Report each bad sector on separate line: */
            if (s > ss) {
                fprintf(stderr, "\n%u ", s);
                if (curfile >= 0)
                    fprintf(stderr, "(%s) ", file[curfile].fname);
                /* Restart sectors range after the error: */
                ss = s + 1;
            };
            /* Print error warning: */
            fprintf(stderr, "%s - %s\n", st, dvdcss_error(dvdcss));
            /* XXX: substitute zeroes for bad sector data: */
            memset(p_buffer, '\0', DVDCSS_BLOCK_SIZE);
        };

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
