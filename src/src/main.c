/*
 * NewOrder C — smoke test and benchmark binary.
 * Usage: neworder_c smoke | bench [N]
 */
#define _POSIX_C_SOURCE 200809L

#include "neworder.h"
#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double wall_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ── smoke test: full flow through every feature ─────────────────────────── */
static int smoke(void) {
    NoLedger l;
    char customer[NO_ADDRESS_LEN], merchant[NO_ADDRESS_LEN];
    char validator1[NO_ADDRESS_LEN], validator2[NO_ADDRESS_LEN];
    char token_id[NO_ID_LEN], contract_id[NO_ID_LEN];
    NoAiPayment payment, verified;
    char response[256];

    no_make_address("customer-seed",  customer);
    no_make_address("merchant-seed",  merchant);
    no_make_address("validator1-seed", validator1);
    no_make_address("validator2-seed", validator2);

    no_ledger_init(&l);

    /* configure 2-validator round-robin */
    {
        char va[2][NO_ADDRESS_LEN];
        snprintf(va[0], NO_ADDRESS_LEN, "%s", validator1);
        snprintf(va[1], NO_ADDRESS_LEN, "%s", validator2);
        if (!no_set_validators(&l, (const char (*)[NO_ADDRESS_LEN])va, 2)) {
            fprintf(stderr, "set_validators failed\n"); no_ledger_free(&l); return 1;
        }
    }

    /* fund customer via direct mint (pre-chain) */
    if (!no_mint(&l, customer, 200.0, "initial funds")) {
        fprintf(stderr, "mint failed: %s\n", no_last_error(&l));
        no_ledger_free(&l); return 1;
    }

    /* block 1: validator1's turn */
    if (!no_produce_block(&l, validator1)) {
        fprintf(stderr, "block 1 failed: %s\n", no_last_error(&l));
        no_ledger_free(&l); return 1;
    }
    printf("[block 1] validator1 produced — height=%llu supply=%.8f\n",
           (unsigned long long)l.blocks[l.block_count-1].index, no_supply(&l));

    /* native coin transfer */
    if (!no_add_transfer(&l, customer, merchant, 10.0, 0.05, "hello")) {
        fprintf(stderr, "transfer failed: %s\n", no_last_error(&l));
        no_ledger_free(&l); return 1;
    }

    /* token create */
    if (!no_token_create(&l, customer, "Demo Token", "DMT",
                          1000000.0, 2, 0.01, token_id)) {
        fprintf(stderr, "token_create failed: %s\n", no_last_error(&l));
        no_ledger_free(&l); return 1;
    }
    printf("[mempool] token_create queued — prospective token_id=%s\n", token_id);

    /* block 2: validator2's turn */
    if (!no_produce_block(&l, validator2)) {
        fprintf(stderr, "block 2 failed: %s\n", no_last_error(&l));
        no_ledger_free(&l); return 1;
    }
    printf("[block 2] validator2 produced — height=%llu supply=%.8f\n",
           (unsigned long long)l.blocks[l.block_count-1].index, no_supply(&l));
    printf("  customer balance : %.8f NO\n", no_balance_of(&l, customer));
    printf("  merchant balance : %.8f NO\n", no_balance_of(&l, merchant));

    /* determine the actual token_id after it lands in a block */
    if (l.token_count > 0) {
        snprintf(token_id, NO_ID_LEN, "%s", l.tokens[0].token_id);
        printf("  token %s balance: %.8f DMT\n", token_id,
               no_token_balance_of(&l, token_id, customer));
    }

    /* token transfer */
    if (l.token_count > 0) {
        if (!no_token_transfer(&l, customer, token_id, merchant, 250.0, 0.01)) {
            fprintf(stderr, "token_transfer failed: %s\n", no_last_error(&l));
            no_ledger_free(&l); return 1;
        }
    }

    /* counter contract deploy */
    if (!no_contract_deploy(&l, customer, "VoteCounter", 1, 0.01, contract_id)) {
        fprintf(stderr, "contract_deploy failed: %s\n", no_last_error(&l));
        no_ledger_free(&l); return 1;
    }

    /* block 3: validator1's turn again */
    if (!no_produce_block(&l, validator1)) {
        fprintf(stderr, "block 3 failed: %s\n", no_last_error(&l));
        no_ledger_free(&l); return 1;
    }
    printf("[block 3] validator1 produced — height=%llu\n",
           (unsigned long long)l.blocks[l.block_count-1].index);

    /* update contract_id to the one actually assigned */
    if (l.contract_count > 0) {
        snprintf(contract_id, NO_ID_LEN, "%s", l.contracts[0].contract_id);

        /* contract call: increment twice */
        no_contract_call(&l, customer, contract_id,
                          "increment", 5, NULL, NULL, 0.0);
        no_contract_call(&l, customer, contract_id,
                          "increment", 3, NULL, NULL, 0.0);

        if (!no_produce_block(&l, validator2)) {
            fprintf(stderr, "block 4 failed: %s\n", no_last_error(&l));
            no_ledger_free(&l); return 1;
        }
        printf("[block 4] validator2 produced\n");
        {
            const NoContract *c = no_contract_find(&l, contract_id);
            if (c) printf("  contract %s value=%lld\n", c->name, (long long)c->counter_value);
        }
    }

    /* AI payment flow */
    if (!no_ai_create_payment(&l, "chat-basic", customer, merchant, 2, &payment)) {
        fprintf(stderr, "ai_create failed: %s\n", no_last_error(&l));
        no_ledger_free(&l); return 1;
    }
    if (!no_add_transfer(&l, customer, merchant,
                          payment.amount_due, 0.01, payment.memo)) {
        fprintf(stderr, "ai payment transfer failed: %s\n", no_last_error(&l));
        no_ledger_free(&l); return 1;
    }
    if (!no_produce_block(&l, validator1)) {
        fprintf(stderr, "block 5 failed: %s\n", no_last_error(&l));
        no_ledger_free(&l); return 1;
    }
    printf("[block 5] validator1 produced — AI payment transfer confirmed\n");

    if (!no_ai_verify_payment(&l, payment.payment_id, &verified) || !verified.paid) {
        fprintf(stderr, "ai_verify failed\n"); no_ledger_free(&l); return 1;
    }
    if (!no_ai_consume(&l, payment.payment_id, "hello from C", response, sizeof(response))) {
        fprintf(stderr, "ai_consume failed: %s\n", no_last_error(&l));
        no_ledger_free(&l); return 1;
    }

    printf("\nNewOrder C smoke OK\n");
    printf("  chain height : %llu\n",
           (unsigned long long)l.blocks[l.block_count-1].index);
    printf("  supply       : %.8f NO\n", no_supply(&l));
    printf("  customer     : %.8f NO\n", no_balance_of(&l, customer));
    printf("  merchant     : %.8f NO\n", no_balance_of(&l, merchant));
    printf("  tokens       : %zu\n", l.token_count);
    printf("  contracts    : %zu\n", l.contract_count);
    printf("  ai response  : %s\n", response);

    /* SHA-256 self-test */
    {
        char h[65];
        sha256_hex("abc", 3, h);
        if (strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") != 0)
            printf("  SHA-256 SELF-TEST FAILED!\n");
        else
            printf("  SHA-256 self-test OK\n");
    }

    no_ledger_free(&l);
    return 0;
}

/* ── benchmark ────────────────────────────────────────────────────────────── */
static int bench(int argc, char **argv) {
    NoLedger l;
    char customer[NO_ADDRESS_LEN], recipient[NO_ADDRESS_LEN];
    char validator[NO_ADDRESS_LEN];
    int count = 100000, i;
    double t0, t1;

    if (argc >= 3) count = atoi(argv[2]);
    if (count <= 0) count = 100000;

    no_make_address("bench-customer",  customer);
    no_make_address("bench-recipient", recipient);
    no_make_address("bench-validator", validator);

    no_ledger_init(&l);
    no_mint(&l, customer, (double)count + 1000.0, "bench funds");

    t0 = wall_seconds();
    for (i = 0; i < count; i++) {
        char memo[NO_MEMO_LEN];
        snprintf(memo, sizeof(memo), "bench:%d", i);
        if (!no_add_transfer(&l, customer, recipient, 0.001, 0.0, memo)) {
            fprintf(stderr, "add failed at %d: %s\n", i, no_last_error(&l));
            no_ledger_free(&l); return 1;
        }
    }
    t1 = wall_seconds();

    {
        double add_sec = t1 - t0;
        double t2, block_sec;
        t2 = wall_seconds();
        if (!no_produce_block(&l, validator)) {
            fprintf(stderr, "block failed: %s\n", no_last_error(&l));
            no_ledger_free(&l); return 1;
        }
        block_sec = wall_seconds() - t2;

        printf("{\n");
        printf("  \"transactions\": %d,\n", count);
        printf("  \"add_seconds\": %.6f,\n", add_sec);
        printf("  \"add_tps\": %.2f,\n", (double)count / add_sec);
        printf("  \"block_seconds\": %.6f,\n", block_sec);
        printf("  \"block_tps_equivalent\": %.2f,\n", (double)count / block_sec);
        printf("  \"customer_balance\": %.8f,\n", no_balance_of(&l, customer));
        printf("  \"recipient_balance\": %.8f,\n", no_balance_of(&l, recipient));
        printf("  \"supply\": %.8f\n", no_supply(&l));
        printf("}\n");
    }
    no_ledger_free(&l);
    return 0;
}

static void usage(const char *prog) {
    printf("Usage:\n");
    printf("  %s smoke\n", prog);
    printf("  %s bench [transactions]\n", prog);
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(argv[0]); return 1; }
    if (strcmp(argv[1], "smoke") == 0) return smoke();
    if (strcmp(argv[1], "bench") == 0) return bench(argc, argv);
    usage(argv[0]); return 1;
}
