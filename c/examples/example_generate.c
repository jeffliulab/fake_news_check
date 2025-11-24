#include "llmproxy.h"
#include <stdio.h>
#include <stdlib.h>

int main() {

    // Auto-loads .env
    ClientConfig cfg = llmproxy_load_config();

    // Required fields
    const char *model  = "4o-mini";
    const char *system = "Answer my question in a funny manner";
    const char *query  = "who are the jumbos?";


    int lastk_val = 5;
    int *lastk = &lastk_val;

    const char *session_id = "GenericSession";   // or NULL for default

    // ---- Call API ----
    char *response = llmproxy_generate(
        &cfg,
        model,
        system,
        query,
        NULL,       // temperature --- optional (NULL → defaults)
        lastk,      // lastk --- optional (NULL → defaults)
        session_id, // session_id --- optional (NULL → defaults)
        NULL,       // optional
        NULL,       // optional
        NULL        // optional
    );

    // ---- Print ----
    if (response) {
        printf("Response from server:\n%s\n", response);
        free(response);
    }

    return 0;
}
