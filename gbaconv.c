#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>

#ifndef __PS3__
/* getopt.h start */
int opterr = 1, /* if error message should be printed */
    optind = 1, /* index into parent argv vector */
    optopt,     /* character checked for validity */
    optreset;   /* reset getopt */
char *optarg;   /* argument associated with option */

#define BADCH (int)'?'
#define BADARG (int)':'
#define EMSG ""

/*
 * getopt --
 *      Parse argc/argv argument vector.
 */
int getopt(int nargc, char *const nargv[], const char *ostr)
{
    static char *place = EMSG; /* option letter processing */
    const char *oli;           /* option letter list index */

    if (optreset || !*place)
    { /* update scanning pointer */
        optreset = 0;
        if (optind >= nargc || *(place = nargv[optind]) != '-')
        {
            place = EMSG;
            return (-1);
        }
        if (place[1] && *++place == '-')
        { /* found "--" */
            ++optind;
            place = EMSG;
            return (-1);
        }
    } /* option letter okay? */
    if ((optopt = (int)*place++) == (int)':' ||
        !(oli = strchr(ostr, optopt)))
    {
        /*
         * if the user didn't specify '-' as an option,
         * assume it means -1.
         */
        if (optopt == (int)'-')
            return (-1);
        if (!*place)
            ++optind;
        if (opterr && *ostr != ':')
            (void)printf("illegal option -- %c\n", optopt);
        return (BADCH);
    }
    if (*++oli != ':')
    { /* don't need argument */
        optarg = NULL;
        if (!*place)
            ++optind;
    }
    else
    {               /* need an argument */
        if (*place) /* no white space */
            optarg = place;
        else if (nargc <= ++optind)
        { /* no arg */
            place = EMSG;
            if (*ostr == ':')
                return (BADARG);
            if (opterr)
                (void)printf("option requires an argument -- %c\n", optopt);
            return (BADCH);
        }
        else /* white space */
            optarg = nargv[optind];
        place = EMSG;
        ++optind;
    }
    return (optopt); /* dump back option letter */
}
/* getopt.h end */
#endif

enum save_type
{
    EEPROM_512B,
    EEPROM_8K,
    FLASH_64K,
    FLASH_128K,
    SRAM,
    SAVE_UNKNOWN
};

static const char *save_type_to_string(enum save_type type)
{
    switch (type)
    {
    case EEPROM_512B:
        return "EEPROM 4kbit";
    case EEPROM_8K:
        return "EEPROM 64kbit";
    case FLASH_64K:
        return "FLASH 512kbit";
    case FLASH_128K:
        return "FLASH 1MBit";
    case SRAM:
        return "SRAM";

    default:
        return "Unknown type";
    }
}

static bool scan_section(const uint8_t *data, unsigned size)
{
    for (unsigned i = 0; i < size; i++)
    {
        if (data[i] != 0xff)
            return true;
    }

    return false;
}

static enum save_type detect_save_type(const uint8_t *data, unsigned size)
{
    if (size == 512)
        return EEPROM_512B;
    if (size == 0x2000)
        return EEPROM_8K;
    if (size == 0x10000)
        return FLASH_64K;
    if (size == 0x20000)
        return FLASH_128K;
    if (size == 0x8000)
        return SRAM;

    if (size == (0x20000 + 0x2000))
    {
        if (scan_section(data, 0x8000) && !scan_section(data + 0x8000, 0x1A000))
            return SRAM;
        if (scan_section(data, 0x10000) && !scan_section(data + 0x10000, 0x10000))
            return FLASH_64K;
        if (scan_section(data, 0x20000))
            return FLASH_128K;

        if (scan_section(data + 0x20000, 512) && !scan_section(data + 0x20000 + 512, 0x20000 - 512))
            return EEPROM_512B;
        if (scan_section(data + 0x20000, 0x2000))
            return EEPROM_8K;
    }

    return SAVE_UNKNOWN;
}

static void dump_srm(FILE *file, const uint8_t *data, enum save_type type)
{
    void *buf = malloc(0x20000 + 0x2000);
    memset(buf, 0xff, 0x20000 + 0x2000);

    switch (type)
    {
    case EEPROM_512B:
        fwrite(buf, 1, 0x20000, file);
        fwrite(data, 1, 512, file);
        fwrite(buf, 1, 0x2000 - 512, file);
        break;

    case EEPROM_8K:
        fwrite(buf, 1, 0x20000, file);
        fwrite(data, 1, 0x2000, file);
        break;

    case FLASH_64K:
        fwrite(data, 1, 0x10000, file);
        fwrite(buf, 1, 0x20000 + 0x2000 - 0x10000, file);
        break;

    case FLASH_128K:
        fwrite(data, 1, 0x20000, file);
        fwrite(buf, 1, 0x2000, file);
        break;

    case SRAM:
        fwrite(data, 1, 0x8000, file);
        fwrite(buf, 1, 0x1A000, file);
        break;

    default:
        break;
    }

    free(buf);
}

static void dump_sav(FILE *file, const uint8_t *data, enum save_type type)
{
    switch (type)
    {
    case EEPROM_512B:
        fwrite(data + 0x20000, 1, 512, file);
        break;

    case EEPROM_8K:
        fwrite(data + 0x20000, 1, 0x2000, file);
        break;

    case FLASH_64K:
        fwrite(data, 1, 0x10000, file);
        break;

    case FLASH_128K:
        fwrite(data, 1, 0x20000, file);
        break;

    case SRAM:
        fwrite(data, 1, 0x8000, file);
        break;

    default:
        break;
    }
}

// One shot cowboy code :)

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <file>\n", argv[0]);
        return 1;
    }

    FILE *file = fopen(argv[1], "rb");
    if (!file)
    {
        fprintf(stderr, "Failed to open file \"%s\"\n", argv[1]);
        goto error;
    }

    fseek(file, 0, SEEK_END);
    long len = ftell(file);
    rewind(file);

    uint8_t *buffer = malloc(len);
    if (!buffer)
    {
        fprintf(stderr, "Failed to allocate memory!\n");
        goto error;
    }
    fread(buffer, 1, len, file);
    fclose(file);
    file = NULL;

    char *out_path = strdup(argv[1]);
    char *split = strrchr(out_path, '.');
    const char *ext = NULL;

    if (split)
    {
        *split = '\0';
        ext = split + 1;

        if (strcasecmp(ext, "srm") == 0)
            strcat(out_path, ".sav");
        else if (strlen(ext) >= 3)
            strcat(out_path, ".srm");
        else
            ext = NULL;
    }

    if (!ext)
    {
        fprintf(stderr, "Cannot detect extension!\n");
        goto error;
    }

    enum save_type type = detect_save_type(buffer, len);
    printf("Detected save type: %s\n", save_type_to_string(type));

    if (type == SAVE_UNKNOWN)
    {
        fprintf(stderr, "Cannot infer save type ...\n");
        goto error;
    }

    file = fopen(out_path, "wb");
    if (!file)
        goto error;

    if (len == (0x20000 + 0x2000))
        dump_sav(file, buffer, type);
    else
        dump_srm(file, buffer, type);
    fclose(file);

    return 0;

error:
    if (file)
        fclose(file);
    return 1;
}
