/*-
 * Copyright 2003-2005 Colin Percival
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted providing that the following conditions 
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#if 0
__FBSDID("$FreeBSD: src/usr.bin/bsdiff/bspatch/bspatch.c,v 1.1 2005/08/06 01:59:06 cperciva Exp $");
#endif

#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <unistd.h>
#include <fcntl.h>
#include "bzlib/bzlib.h"
#include <jni.h>
#include <android/log.h>
#include <time.h>

typedef unsigned char u_char;

static off_t offtin(u_char *buf) {
    off_t y;

    y = buf[7] & 0x7F;
    y = y * 256;
    y += buf[6];
    y = y * 256;
    y += buf[5];
    y = y * 256;
    y += buf[4];
    y = y * 256;
    y += buf[3];
    y = y * 256;
    y += buf[2];
    y = y * 256;
    y += buf[1];
    y = y * 256;
    y += buf[0];

    if (buf[7] & 0x80) y = -y;

    return y;
}

long long current_time()
{
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (long long)tv.tv_sec * 1000 + (long long)tv.tv_usec / 1000;
}

int patch_file(int argc, char *argv[]) {
    FILE *f, *cpf, *dpf, *epf;
    BZFILE *cpfbz2, *dpfbz2, *epfbz2;
    int cbz2err, dbz2err, ebz2err;
    int fd;
    ssize_t oldsize, newsize;
    ssize_t bzctrllen, bzdatalen;
    u_char header[32], buf[8];
    u_char *old, *new;
    off_t oldpos, newpos;
    off_t ctrl[3];
    off_t lenread;
    off_t i;

    if (argc != 4) {
        __android_log_print(ANDROID_LOG_ERROR, "ERROR", "usage: %s oldfile newfile patchfile\n",
                            argv[0]);;
        errx(1, "usage: %s oldfile newfile patchfile\n", argv[0]);
    }

    __android_log_print(ANDROID_LOG_ERROR, "DEBUG", "argv: old=%s, new=%s, patch=%s ", argv[1],
                        argv[2], argv[3]);;

    /* Open patch file */
    if ((f = fopen(argv[3], "r")) == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, "ERROR", "fopen(%s)", argv[3]);;
        err(1, "fopen(%s)", argv[3]);
    }

    /*
    File format:
        0	8	"BSDIFF40"
        8	8	X
        16	8	Y
        24	8	sizeof(newfile)
        32	X	bzip2(control block)
        32+X	Y	bzip2(diff block)
        32+X+Y	???	bzip2(extra block)
    with control block a set of triples (x,y,z) meaning "add x bytes
    from oldfile to x bytes from the diff block; copy y bytes from the
    extra block; seek forwards in oldfile by z bytes".
    */

    /* Read header */
    if (fread(header, 1, 32, f) < 32) {
        if (feof(f)) {
            __android_log_print(ANDROID_LOG_ERROR, "ERROR", "Corrupt patch\n");;
            errx(1, "Corrupt patch\n");
        }
        err(1, "fread(%s)", argv[3]);
    }

    /* Check for appropriate magic */
    if (memcmp(header, "BSDIFF40", 8) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "ERROR", "Corrupt patch\n");;
        errx(1, "Corrupt patch\n");
    }

    /* Read lengths from header */
    bzctrllen = offtin(header + 8);
    bzdatalen = offtin(header + 16);
    newsize = offtin(header + 24);
    if ((bzctrllen < 0) || (bzdatalen < 0) || (newsize < 0)) {
        __android_log_print(ANDROID_LOG_ERROR, "ERROR", "Corrupt patch\n");;
        errx(1, "Corrupt patch\n");
    }


    /* Close patch file and re-open it via libbzip2 at the right places */
    if (fclose(f))
        err(1, "fclose(%s)", argv[3]);

    if ((cpf = fopen(argv[3], "r")) == NULL)
        err(1, "fopen(%s)", argv[3]);

    if (fseeko(cpf, 32, SEEK_SET))
        err(1, "fseeko(%s, %lld)", argv[3], (long long) 32);

    if ((cpfbz2 = BZ2_bzReadOpen(&cbz2err, cpf, 0, 0, NULL, 0)) == NULL)
        errx(1, "BZ2_bzReadOpen, bz2err = %d", cbz2err);

    if ((dpf = fopen(argv[3], "r")) == NULL)
        err(1, "fopen(%s)", argv[3]);

    if (fseeko(dpf, 32 + bzctrllen, SEEK_SET))
        err(1, "fseeko(%s, %lld)", argv[3], (long long) (32 + bzctrllen));

    if ((dpfbz2 = BZ2_bzReadOpen(&dbz2err, dpf, 0, 0, NULL, 0)) == NULL)
        errx(1, "BZ2_bzReadOpen, bz2err = %d", dbz2err);

    if ((epf = fopen(argv[3], "r")) == NULL)
        err(1, "fopen(%s)", argv[3]);

    if (fseeko(epf, 32 + bzctrllen + bzdatalen, SEEK_SET))
        err(1, "fseeko(%s, %lld)", argv[3],
            (long long) (32 + bzctrllen + bzdatalen));

    if ((epfbz2 = BZ2_bzReadOpen(&ebz2err, epf, 0, 0, NULL, 0)) == NULL)
        errx(1, "BZ2_bzReadOpen, bz2err = %d", ebz2err);

    if (((fd = open(argv[1], O_RDONLY, 0)) < 0) ||
        ((oldsize = lseek(fd, 0, SEEK_END)) == -1) ||
        ((old = malloc(oldsize + 1)) == NULL) ||
        (lseek(fd, 0, SEEK_SET) != 0) ||
        (read(fd, old, oldsize) != oldsize) ||
        (close(fd) == -1))
        err(1, "%s", argv[1]);
    if ((new = malloc(newsize + 1)) == NULL) err(1, NULL);

    oldpos = 0;
    newpos = 0;
    long long start_time = current_time();
    __android_log_print(ANDROID_LOG_ERROR, "DEBUG", "read file...");;
    while (newpos < newsize) {
        /* Read control data */
        for (i = 0; i <= 2; i++) {
            lenread = BZ2_bzRead(&cbz2err, cpfbz2, buf, 8);
            if ((lenread < 8) || ((cbz2err != BZ_OK) &&
                                  (cbz2err != BZ_STREAM_END))) {
                __android_log_print(ANDROID_LOG_ERROR, "ERROR", "Corrupt patch\n");;
                errx(1, "Corrupt patch\n");
            }
            ctrl[i] = offtin(buf);
        };

        /* Sanity-check */
        if (newpos + ctrl[0] > newsize)
            errx(1, "Corrupt patch\n");

        /* Read diff string */
        lenread = BZ2_bzRead(&dbz2err, dpfbz2, new + newpos, ctrl[0]);
        if ((lenread < ctrl[0]) ||
            ((dbz2err != BZ_OK) && (dbz2err != BZ_STREAM_END)))
            errx(1, "Corrupt patch\n");

        /* Add old data to diff string */
        for (i = 0; i < ctrl[0]; i++)
            if ((oldpos + i >= 0) && (oldpos + i < oldsize))
                new[newpos + i] += old[oldpos + i];

        /* Adjust pointers */
        newpos += ctrl[0];
        oldpos += ctrl[0];

        /* Sanity-check */
        if (newpos + ctrl[1] > newsize)
            errx(1, "Corrupt patch\n");

        /* Read extra string */
        lenread = BZ2_bzRead(&ebz2err, epfbz2, new + newpos, ctrl[1]);
        if ((lenread < ctrl[1]) ||
            ((ebz2err != BZ_OK) && (ebz2err != BZ_STREAM_END)))
            errx(1, "Corrupt patch\n");

        /* Adjust pointers */
        newpos += ctrl[1];
        oldpos += ctrl[2];
    };
    long long end_time = current_time();
    __android_log_print(ANDROID_LOG_ERROR, "DEBUG", "read file finished. take %lld ms\n new size:%s", end_time - start_time, newsize);;
    /* Clean up the bzip2 reads */
    BZ2_bzReadClose(&cbz2err, cpfbz2);
    BZ2_bzReadClose(&dbz2err, dpfbz2);
    BZ2_bzReadClose(&ebz2err, epfbz2);
    if (fclose(cpf) || fclose(dpf) || fclose(epf))
        err(1, "fclose(%s)", argv[3]);

    __android_log_print(ANDROID_LOG_ERROR, "DEBUG", "close BZ2_bzRead");;

    /* Write the new file */
    ssize_t write_size = 0;
    if (((fd = open(argv[2], O_CREAT | O_TRUNC | O_WRONLY, 0666)) < 0) ||
        ((write_size = write(fd, new,  newsize)) != newsize) || (close(fd) == -1))
    {
        __android_log_print(ANDROID_LOG_ERROR, "DEBUG", "write new file %s open:%d \nwrite size:%ld",
                            argv[2], fd, write_size);;
        err(1, "%s", argv[2]);
    }

    free(new);
    free(old);

    __android_log_print(ANDROID_LOG_ERROR, "DEBUG", "patch done!");;
    return 0;
}

// to patch file
JNIEXPORT void JNICALL Java_org_ivan_bspatch_BSPatchUtils_patch(JNIEnv *env, jclass type,
                                                                jstring oldFile_,
                                                                jstring newFile_, jstring patch_) {
    const char *oldFile = (*env)->GetStringUTFChars(env, oldFile_, 0);
    const char *newFile = (*env)->GetStringUTFChars(env, newFile_, 0);
    const char *patch = (*env)->GetStringUTFChars(env, patch_, 0);

    const char *argv[4] = {"bspatch", oldFile, newFile, patch};

    __android_log_print(ANDROID_LOG_ERROR, "DEBUG", "############");
    int r = patch_file(4, (char **) argv);

    __android_log_print(ANDROID_LOG_ERROR, "DEBUG", "@@@@@@@@@@%d", r);

    (*env)->ReleaseStringUTFChars(env, oldFile_, oldFile);
    (*env)->ReleaseStringUTFChars(env, newFile_, newFile);
    (*env)->ReleaseStringUTFChars(env, patch_, patch);
}