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
cnmt_xml_ctx_t application_cnmt_xml;
cnmt_xml_ctx_t patch_cnmt_xml;
cnmt_ctx_t application_cnmt;
cnmt_ctx_t patch_cnmt;
nsp_ctx_t application_nsp;
nsp_ctx_t patch_nsp;
nsp_ctx_t *addon_nsps;
cnmt_addons_ctx_t addons_cnmt_ctx;

// Print Usage
static void usage(void)
{
    fprintf(stderr,
        "Usage: %s [options...] <path_to_file.xci>\n\n"
        "Options:\n"
        "-k, --keyset             Set keyset filepath, default filepath is ." OS_PATH_SEPARATOR "keys.dat\n"
        "-h, --help               Display usage\n"
        "--nodummytik             Skips creating and packing dummy tik and cert into nsps\n"
        , USAGE_PROGRAM_NAME);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    //Compiler nag
    (void)argc;

    nxci_ctx_t tool_ctx;
    char input_name[0x200];

    printf("4NXCI %s by The-4n\n", NXCI_VERSION);

    memset(&tool_ctx, 0, sizeof(tool_ctx));
    memset(input_name, 0, sizeof(input_name));
    memset(&application_cnmt, 0, sizeof(cnmt_ctx_t));
    memset(&patch_cnmt, 0, sizeof(cnmt_ctx_t));
    memset(&application_cnmt_xml, 0, sizeof(cnmt_xml_ctx_t));
    memset(&patch_cnmt_xml, 0, sizeof(cnmt_xml_ctx_t));
    memset(&application_nsp, 0, sizeof(nsp_ctx_t));
    memset(&patch_nsp, 0, sizeof(nsp_ctx_t));
    memset(&addons_cnmt_ctx, 0, sizeof(cnmt_addons_ctx_t));
    memset(&addon_nsps, 0, sizeof(addon_nsps));

    filepath_t keypath;

    filepath_init(&keypath);
    pki_initialize_keyset(&tool_ctx.settings.keyset, KEYSET_RETAIL);
    // Default keyset filepath
    filepath_set(&keypath, "keys.dat");

    // Parse options
    while (1)
    {
        int option_index;
        int c;
        static struct option long_options[] =
            {
                {"keyset", 1, NULL, 'k'},
                {"help", 0, NULL, 'h'},
                {"nodummytik", 0, NULL, 1},
                {NULL, 0, NULL, 0},
            };

        c = getopt_long(argc, argv, "k:h", long_options, &option_index);
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
            case 1:
                tool_ctx.settings.no_dummy_tik = 1;
                break;
            default:
                usage();
        }
    }

    // Try to populate default keyfile.
    FILE *keyfile = NULL;
    keyfile = os_fopen(keypath.os_path, OS_MODE_READ);

    if (keyfile != NULL)
    {
        extkeys_initialize_keyset(&tool_ctx.settings.keyset, keyfile);
        pki_derive_keys(&tool_ctx.settings.keyset);
        fclose(keyfile);
    }
    else
    {
        fprintf(stderr, "Unable to open keyset '%s'\n"
                        "Use -k or --keyset to specify your keyset path or place your keyset in ." OS_PATH_SEPARATOR "keys.dat\n", keypath.char_path);
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

    // Hardcode secure partition save path to "4nxci_extracted_nsp" directory
    filepath_init(&xci_ctx.tool_ctx->settings.secure_dir_path);
    filepath_set(&xci_ctx.tool_ctx->settings.secure_dir_path, "4nxci_extracted_xci");

    printf("\n");

    xci_process(&xci_ctx);

    // Process ncas in cnmts
    printf("===> Processing Application Metadata:\n");
    cnmt_gamecard_process(xci_ctx.tool_ctx, &application_cnmt_xml, &application_cnmt, &application_nsp);
    if (patch_cnmt.title_id != 0)
    {
        printf("===> Processing Patch Metadata:\n");
        cnmt_download_process(xci_ctx.tool_ctx, &patch_cnmt_xml, &patch_cnmt, &patch_nsp);
    }
    if (addons_cnmt_ctx.count != 0)
    {
        addon_nsps = (nsp_ctx_t *)calloc(1, sizeof(nsp_ctx_t) * addons_cnmt_ctx.count);
        printf("===> Processing %u Addon(s):\n", addons_cnmt_ctx.count);
        for (int i = 0; i < addons_cnmt_ctx.count; i++)
        {
            printf("===> Processing AddOn %i Metadata:\n", i + 1);
            cnmt_gamecard_process(xci_ctx.tool_ctx, &addons_cnmt_ctx.addon_cnmt_xml[i], &addons_cnmt_ctx.addon_cnmt[i], &addon_nsps[i]);
        }
    }

    printf("\nSummary:\n");
    printf("Game NSP: %s\n", application_nsp.filepath.char_path);
    if (patch_cnmt.title_id != 0)
        printf("Update NSP: %s\n", patch_nsp.filepath.char_path);
    if (addons_cnmt_ctx.count != 0)
    {
        for (int i2 = 0; i2 < addons_cnmt_ctx.count; i2++)
            printf("DLC NSP %i: %s\n", i2 + 1, addon_nsps[i2].filepath.char_path);
    }

    fclose(tool_ctx.file);
    printf("\nDone!\n");
    return EXIT_SUCCESS;
}
