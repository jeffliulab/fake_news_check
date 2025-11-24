#include "llmproxy.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    // This automatically loads .env inside llmproxy_load_config()
    ClientConfig cfg = llmproxy_load_config();

    char *info = llmproxy_model_info(&cfg);
    printf("%s\n", info);
    free(info);

    return 0;
}