#ifndef taos-test_VERSION_H_
#define taos-test_VERSION_H_

#include <ace/Get_Opt.h>

const char* const VERSION = "debug/3.0.1";
const char* const VERSION_STRING = "taos-test: branch = debug/3.0.1, commit = 98cc065fc, model = NO_MODEL, defines = SAFTY INFLUX_SIMPLELIFY_ALARM _DMSQL _PGSQL ";
const char* const MODEL_STRING = "";
const char* const MODEL_EXTRA_STRING = "";

#ifndef TRY_PRINT_VERSION
#define TRY_PRINT_VERSION(argc, argv) \
    ACE_Get_Opt get_opt_version (argc, argv, ACE_TEXT("VME"), 1, 0, 3); \
    get_opt_version.long_option (ACE_TEXT ("version"), 'V'); \
    get_opt_version.long_option (ACE_TEXT ("model"), 'M'); \
    get_opt_version.long_option (ACE_TEXT ("extra"), 'E'); \
    int ccc; \
    while ((ccc = get_opt_version ()) != -1) {  \
    switch (ccc) {                      \
        case 'V':                       \
        printf("%s\n", VERSION_STRING); \
        return 0;                       \
        case 'M':                       \
        printf("%s\n", MODEL_STRING);   \
        return 0;                       \
        case 'E':                       \
        printf("%s\n", MODEL_EXTRA_STRING);     \
        return 0; }}
#endif

#endif

