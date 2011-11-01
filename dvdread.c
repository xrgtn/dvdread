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
#define MAXVOBS 200
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
    unsigned int  s, s1, s2;
    char tfname[23];
    uint32_t start, len;
    int t, curvob, newvob, r;
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
    for (t = 0; t < MAXTITLES; t++) {
        if (t == 0) {
            snprintf(tfname, sizeof(tfname), "/VIDEO_TS/VIDEO_TS.VOB");
        } else snprintf(tfname, sizeof(tfname),
            "/VIDEO_TS/VTS_%02d_%d.VOB", t, 0);
        start = UDFFindFile(prdr, tfname, &len);
        if (start && len) addvob(tfname, start, len);
        if (!t) continue;
        snprintf(tfname, sizeof(tfname), "/VIDEO_TS/VTS_%02d_%d.VOB",
            t, 1);
        start = UDFFindFile(prdr, tfname, &len);
        if (start && len) addvob(tfname, start, len);
        if (!start || !len) break;
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
    for (s = s1, curvob = -1; s < s2; s++) {
        newvob = curvob;
        if (curvob < 0 || s < vob[curvob].start
                || vob[curvob].end <= s)
            newvob = findvob(s);
        if (newvob != curvob && s != s1) fprintf(stderr, "\n");
        fprintf(stderr, "\rsector %u ", s);
        if (newvob >= 0) fprintf(stderr, "(%s) ", vob[newvob].fname);
        curvob = newvob;
        st = "seek";
        if (curvob >= 0 && s == vob[curvob].start) {
            st = "seek key";
            r = dvdcss_seek(dvdcss, s, DVDCSS_SEEK_KEY);
        } else r = dvdcss_seek(dvdcss, s, DVDCSS_NOFLAGS);
        if (r != (int)s) goto CSSERR;
        st = "read";
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
