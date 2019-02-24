#include <getopt.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cnmt.h"
#include "nsp.h"
#include "types.h"
#include "utils.h"
#include "settings.h"
#include "pki.h"
#include "xci.h"
#include "extkeys.h"
#include "version.h"

/* 4NXCI by The-4n
   Based on hactool by SciresM
   */

nsp_ctx_t *application_nsps;
cnmts_ctx_t applications_cnmt_ctx;
nsp_ctx_t *patch_nsps;
cnmts_ctx_t patches_cnmt_ctx;
nsp_ctx_t *addon_nsps;
cnmts_ctx_t addons_cnmt_ctx;

// Print Usage
static void usage(void)
{
    fprintf(stderr,
            "Usage: %s [options...] <path_to_file.xci>\n\n"
            "Options:\n"
            "-k, --keyset             Set keyset filepath, default filepath is ." OS_PATH_SEPARATOR "keys.dat\n"
            "-h, --help               Display usage\n"
            "-t, --tempdir            Set temporary directory path\n"
            "-o, --outdir             Set output directory path\n"
            "-r, --rename             Use Titlename instead of Titleid in nsp name\n"
            "--keepncaid              Keep current ncas ids\n",
            USAGE_PROGRAM_NAME);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    nxci_ctx_t tool_ctx;
    char input_name[0x200];

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("4NXCI %s by The-4n\n", NXCI_VERSION);

    memset(&tool_ctx, 0, sizeof(tool_ctx));
    memset(input_name, 0, sizeof(input_name));
    memset(&applications_cnmt_ctx, 0, sizeof(cnmts_ctx_t));
    memset(&application_nsps, 0, sizeof(application_nsps));
    memset(&patches_cnmt_ctx, 0, sizeof(cnmts_ctx_t));
    memset(&patch_nsps, 0, sizeof(patch_nsps));
    memset(&addons_cnmt_ctx, 0, sizeof(cnmts_ctx_t));
    memset(&addon_nsps, 0, sizeof(addon_nsps));

    filepath_t keypath;
    filepath_init(&keypath);

    pki_initialize_keyset(&tool_ctx.settings.keyset);

    // Hardcode secure partition save path to "4nxci_extracted_nsp" directory
    filepath_init(&tool_ctx.settings.secure_dir_path);
    filepath_set(&tool_ctx.settings.secure_dir_path, "4nxci_extracted_xci");

    // Parse options
    while (1)
    {
        int option_index;
        int c;
        static struct option long_options[] =
            {
                {"keyset", 1, NULL, 'k'},
                {"help", 0, NULL, 'h'},
                {"rename", 0, NULL, 'r'},
                {"tempdir", 1, NULL, 't'},
                {"outdir", 1, NULL, 'o'},
                {"keepncaid", 0, NULL, 1},
                {NULL, 0, NULL, 0},
            };

        c = getopt_long(argc, argv, "k:t:o:hr", long_options, &option_index);
        if (c == -1)
            break;

        switch (c)
        {
        case 'k':
            filepath_set(&keypath, optarg);
            break;
        case 'h':
            usage();
            break;
        case 'r':
            tool_ctx.settings.titlename = 1;
            break;
        case 't':
            filepath_set(&tool_ctx.settings.secure_dir_path, optarg);
            break;
        case 'o':
            filepath_init(&tool_ctx.settings.out_dir_path);
            filepath_set(&tool_ctx.settings.out_dir_path, optarg);
            break;
        case 1:
            tool_ctx.settings.keepncaid = 1;
            break;
        default:
            usage();
        }
    }

    // Locating default key file
    FILE *keyfile = NULL;
    keyfile = os_fopen(keypath.os_path, OS_MODE_READ);
    if (keypath.valid == VALIDITY_INVALID)
    {
        filepath_set(&keypath, "keys.dat");
        keyfile = os_fopen(keypath.os_path, OS_MODE_READ);
        if (keyfile == NULL)
        {
            filepath_set(&keypath, "keys.txt");
            keyfile = os_fopen(keypath.os_path, OS_MODE_READ);
        }
        if (keyfile == NULL)
        {
            filepath_set(&keypath, "keys.ini");
            keyfile = os_fopen(keypath.os_path, OS_MODE_READ);
        }
        if (keyfile == NULL)
        {
            filepath_set(&keypath, "prod.keys");
            keyfile = os_fopen(keypath.os_path, OS_MODE_READ);
        }
    }

    // Try to populate default keyfile.
    if (keyfile != NULL)
    {
        printf("\nLoading '%s' keyset file\n", keypath.char_path);
        extkeys_initialize_keyset(&tool_ctx.settings.keyset, keyfile);
        pki_derive_keys(&tool_ctx.settings.keyset);
        fclose(keyfile);
    }
    else
    {
        printf("\n");
        fprintf(stderr, "Error: Unable to open keyset file\n"
                        "Use -k or --keyset to specify your keyset file path or place your keyset in ." OS_PATH_SEPARATOR "keys.dat\n");
        return EXIT_FAILURE;
    }

    // Copy input file
    if (optind == argc - 1)
        strncpy(input_name, argv[optind], sizeof(input_name));
    else if ((optind < argc) || (argc == 1))
        usage();

    if (!(tool_ctx.file = fopen(input_name, "rb")))
    {
        fprintf(stderr, "unable to open %s: %s\n", input_name, strerror(errno));
        return EXIT_FAILURE;
    }

    xci_ctx_t xci_ctx;
    memset(&xci_ctx, 0, sizeof(xci_ctx));
    xci_ctx.file = tool_ctx.file;
    xci_ctx.tool_ctx = &tool_ctx;

    // Remove existing temp directory
    filepath_remove_directory(&xci_ctx.tool_ctx->settings.secure_dir_path);

    // Create output directory if it's valid
    if (xci_ctx.tool_ctx->settings.out_dir_path.valid == VALIDITY_VALID)
        os_makedir(xci_ctx.tool_ctx->settings.out_dir_path.os_path);

    printf("\n");

    xci_process(&xci_ctx);

    // Process ncas in cnmts
    application_nsps = (nsp_ctx_t *)calloc(1, sizeof(nsp_ctx_t) * applications_cnmt_ctx.count);
    printf("===> Processing %u Application(s):\n", applications_cnmt_ctx.count);
    for (int apppc = 0; apppc < applications_cnmt_ctx.count; apppc++)
    {
        printf("===> Processing Application %i Metadata:\n", apppc + 1);
        cnmt_gamecard_process(xci_ctx.tool_ctx, &applications_cnmt_ctx.cnmt_xml[apppc], &applications_cnmt_ctx.cnmt[apppc], &application_nsps[apppc]);
    }
    if (patches_cnmt_ctx.count != 0)
    {
        patch_nsps = (nsp_ctx_t *)calloc(1, sizeof(nsp_ctx_t) * patches_cnmt_ctx.count);
        printf("===> Processing %u Patch(es):\n", patches_cnmt_ctx.count);
        for (int patchpc = 0; patchpc < patches_cnmt_ctx.count; patchpc++)
        {
            printf("===> Processing Patch %i Metadata:\n", patchpc + 1);
            cnmt_download_process(xci_ctx.tool_ctx, &patches_cnmt_ctx.cnmt_xml[patchpc], &patches_cnmt_ctx.cnmt[patchpc], &patch_nsps[patchpc]);
        }
    }
    if (addons_cnmt_ctx.count != 0)
    {
        addon_nsps = (nsp_ctx_t *)calloc(1, sizeof(nsp_ctx_t) * addons_cnmt_ctx.count);
        printf("===> Processing %u Addon(s):\n", addons_cnmt_ctx.count);
        for (int addpc = 0; addpc < addons_cnmt_ctx.count; addpc++)
        {
            printf("===> Processing AddOn %i Metadata:\n", addpc + 1);
            cnmt_gamecard_process(xci_ctx.tool_ctx, &addons_cnmt_ctx.cnmt_xml[addpc], &addons_cnmt_ctx.cnmt[addpc], &addon_nsps[addpc]);
        }
    }

    filepath_remove_directory(&xci_ctx.tool_ctx->settings.secure_dir_path);

    printf("\nSummary:\n");
    for (int gsum = 0; gsum < applications_cnmt_ctx.count; gsum++)
        printf("Game NSP %i: %s\n", gsum + 1, application_nsps[gsum].filepath.char_path);
    if (patches_cnmt_ctx.count != 0)
    {
        for (int patchsum = 0; patchsum < patches_cnmt_ctx.count; patchsum++)
            printf("Update NSP: %i: %s\n", patchsum + 1, patch_nsps[patchsum].filepath.char_path);
    }
    if (addons_cnmt_ctx.count != 0)
    {
        for (int dlcsum = 0; dlcsum < addons_cnmt_ctx.count; dlcsum++)
            printf("DLC NSP %i: %s\n", dlcsum + 1, addon_nsps[dlcsum].filepath.char_path);
    }

    fclose(tool_ctx.file);
    printf("\nDone!\n");
    return EXIT_SUCCESS;
}
