#ifndef NOHTTP_H
#define NOHTTP_H

#include "neworder.h"

/* Configuration passed to the node server */
typedef struct {
    NoLedger   *ledger;
    const char *data_dir;
    const char *validator_address; /* NULL if not a validator node */
    const char *ai_merchant_address;
    int         port;
} NoNodeConfig;

/* Start the blocking HTTP server on the given port. Never returns on success. */
void no_http_serve(NoNodeConfig *cfg);

#endif
