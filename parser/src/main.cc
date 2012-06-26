/* @(#) main.cc -- the entry point of parser */

#define XTERN
#include<common.h>
#undef XTERN
#include<parser.h>
#include<handy.h>
#include<getopt.h>
#include<backupfile.h>

static const char* program_name;

static const char* options[] = {
 "-d DIRECTORY --directory DIRECTORY\tTreat every file in DIRECTORY as input patch to parse.",
 "-v --version Version information.",
 "-h --help Print help information.",
 0 // explicit sentinel
};

static bool from_directory = false;
static char *directory = "./";

static const char* shortopts = "d:vh";

static PatchParser* current_parser = NULL;

static const struct option longopts[] =
{
  {"directory", required_argument, NULL, 'd'},
  {"version", no_argument, NULL, 'v'},
  {"help", no_argument, NULL, 'h'},
  {NULL, no_argument, NULL, 0}
};

static void usage(FILE *stream)
{
    fprintf(stream, "Usage: %s  [OPTION]... [PATCHFILE]\n\n", program_name);
    const char **p;
    for (p = options; *p; p++) {
        fprintf(stream, "%s\n\n", *p);
    }
}

static void version()
{
    printf("%s Version 1.0 \n", program_name);
}

void cleanup()
{
    if (debug) {
        fprintf(stdout, "cleaning up...\n");
    }
    if (current_parser != NULL) {
        current_parser->cleanup();
    }
}

int main(int argc, char *argv[])
{
    program_name = dupstr(argv[0]);

    atexit(cleanup); // clean up work
    
    int optc;

    while ((optc = getopt_long (argc, argv, shortopts, longopts, (int *) 0)) != -1) {
        switch(optc) {
            case 'd': {
                    from_directory = true;
                    if (optarg == NULL) {
                        diegrace("NULL directory");
                    }
                    size_t len = strlen(optarg);
                    if (optarg[len - 1] != DIRECTORY_SEPARATOR) {
                        directory = (char *) malloc(len + 2);
                        strncpy(directory, optarg, len);
                        directory[len] =  DIRECTORY_SEPARATOR;
                        directory[len + 1] = '\0';
                    }
                    else {
                        directory = dupstr(optarg);
                    }
                }
                break;
            case 'v':
                version();
                break;
            case 'h':
                usage(stdout);
                break;
            default:
                usage(stderr);
        }
    }
    if (from_directory && (argc == optind + 1)) {
        fprintf(stderr, "Invalid: input files already specified from the directory\n");
        exit(1);
    }
    if (argc > optind + 1) {
        usage(stdout);
        exit(1);
    }
    
    simple_backup_suffix = ".orig";

    verbosity = SILENT;
    posixly_correct = false;
    batch = false;
    initial_time = 0;
    no_strip_trailing_cr = false;
    explicit_inname = false;
    revision = NULL;
    debug = 8;
    time(&initial_time);

    gbufsize = 8 * KB;
    gbuf = (char *) xmalloc(gbufsize);

    init_backup_hash_table(); 

    filenode *lst;
    if (from_directory) {
        lst = listdir(directory);
        if (NULL == lst) {
            diegrace("Cannot get input files from %s\n", directory);
        }
    }
    else{
        lst = (filenode *) malloc(sizeof(filenode));
        lst->file = argv[optind];
        lst->next = NULL;
    }
    filenode *p;
    for (p = lst; p; p = p->next){
        char *fullname;
        if (from_directory) {
            size_t l1 = strlen(directory);
            size_t l2 = strlen(p->file);
            fullname = (char *) malloc(l1 + l2 + 1);
            strncpy(fullname, directory, l1);
            strncat(fullname, p->file, l2);
            fullname[l1 + l2] = '\0';
        }
        else {
            fullname = p->file;
        }
        if (debug) {
            printf("scan file: %s\n", fullname);
        }
        PatchParser *parser = new PatchParser(fullname, UNI_DIFF); 
        if (NULL == parser) {
            errgrace("Out of memory\n");
        }

        current_parser == parser;

        parser->init_output(0);
        parser->open_patch_file();

        bool apply_empty_patch = false;
        
        /* for each patch in patch file */
        while(parser->there_is_another_patch() || apply_empty_patch) {
            if (debug) {
                printf("got a patch\n");
            }
            if (!issource(parser->inname)) {
                printf("skip non-source patch\n");
                parser->skippatch();
            }
            parser->gobble();
            parser->reinitialize();
            apply_empty_patch = false;
        }
        if(parser->snap) { // something was wrong
            fprintf(stderr, "something is wrong when parsing %s\n", fullname);
        }
        delete parser;
        current_parser = NULL;
    }
    if (gbuf) {
        free(gbuf);
    }
    // save the effort to free file list
    return 0;
}
