/*
 * NewOrder C node — HTTP explorer + PoA block producer.
 * Usage: neworder_node [--port N] [--data-dir PATH]
 *                      [--validator ADDR] [--validators A,B,C]
 *                      [--ai-merchant ADDR]
 */
#define _POSIX_C_SOURCE 200809L

#include "neworder.h"
#include "nohttp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("  --port N             HTTP port (default 8080)\n");
    printf("  --data-dir PATH      Data directory (default data)\n");
    printf("  --validator ADDR     This node's validator NO address\n");
    printf("  --validators A,B,C  Comma-separated validator list for round-robin\n");
    printf("  --ai-merchant ADDR   NO address that receives AI payments\n");
}

int main(int argc, char **argv) {
    NoLedger ledger;
    NoNodeConfig cfg;
    char data_dir[256]        = "data";
    char validator_addr[NO_ADDRESS_LEN] = "";
    char ai_merchant[NO_ADDRESS_LEN]    = "";
    char validators_str[1024] = "";
    int  port = 8080;
    int  i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i+1 < argc)
            port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--data-dir") == 0 && i+1 < argc)
            snprintf(data_dir, sizeof(data_dir), "%s", argv[++i]);
        else if (strcmp(argv[i], "--validator") == 0 && i+1 < argc)
            snprintf(validator_addr, sizeof(validator_addr), "%s", argv[++i]);
        else if (strcmp(argv[i], "--validators") == 0 && i+1 < argc)
            snprintf(validators_str, sizeof(validators_str), "%s", argv[++i]);
        else if (strcmp(argv[i], "--ai-merchant") == 0 && i+1 < argc)
            snprintf(ai_merchant, sizeof(ai_merchant), "%s", argv[++i]);
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]); return 0;
        }
    }

    /* initialise ledger */
    no_ledger_init(&ledger);

    /* parse validators list */
    if (validators_str[0]) {
        char addrs[16][NO_ADDRESS_LEN];
        size_t count = 0;
        char *tok, *ctx = NULL, *copy;
        copy = (char *)malloc(strlen(validators_str) + 1);
        if (!copy) { fprintf(stderr, "oom\n"); return 1; }
        strcpy(copy, validators_str);
        tok = strtok_r(copy, ",", &ctx);
        while (tok && count < 16) {
            while (*tok == ' ') tok++;
            snprintf(addrs[count], NO_ADDRESS_LEN, "%s", tok);
            count++;
            tok = strtok_r(NULL, ",", &ctx);
        }
        free(copy);
        if (!no_set_validators(&ledger, (const char (*)[NO_ADDRESS_LEN])addrs, count)) {
            fprintf(stderr, "set_validators: %s\n", no_last_error(&ledger));
            return 1;
        }
    }

    /* load persisted chain */
    {
        char chain_path[512];
        snprintf(chain_path, sizeof(chain_path), "%s/chain.json", data_dir);
        if (!no_load_chain(&ledger, chain_path)) {
            printf("No existing chain found — starting fresh.\n");
        } else {
            printf("Loaded chain: height=%llu\n",
                   (unsigned long long)ledger.blocks[ledger.block_count-1].index);
        }
    }

    /* load AI payments */
    {
        char pay_path[512];
        snprintf(pay_path, sizeof(pay_path), "%s/ai_payments.json", data_dir);
        no_load_payments(&ledger, pay_path);
    }

    /* set AI merchant address */
    if (!ai_merchant[0] && validator_addr[0])
        snprintf(ai_merchant, sizeof(ai_merchant), "%s", validator_addr);
    if (!ai_merchant[0])
        snprintf(ai_merchant, sizeof(ai_merchant), "NO_AI_MERCHANT");

    cfg.ledger              = &ledger;
    cfg.data_dir            = data_dir;
    cfg.validator_address   = validator_addr[0] ? validator_addr : NULL;
    cfg.ai_merchant_address = ai_merchant;
    cfg.port                = port;

    printf("NewOrder C node starting\n");
    printf("  data-dir   : %s\n", data_dir);
    printf("  validator  : %s\n", validator_addr[0] ? validator_addr : "(none)");
    printf("  ai-merchant: %s\n", ai_merchant);
    printf("  validators : %zu configured\n", ledger.validator_count);

    no_http_serve(&cfg);

    no_ledger_free(&ledger);
    return 0;
}
