#include "neworder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double seconds_now(void) {
    return (double)clock() / (double)CLOCKS_PER_SEC;
}

static int smoke(void) {
    NoLedger ledger;
    NoAiPayment payment;
    NoAiPayment verified;
    char customer[NO_ADDRESS_LEN];
    char merchant[NO_ADDRESS_LEN];
    char validator[NO_ADDRESS_LEN];
    char response[160];

    no_make_address("customer", customer);
    no_make_address("merchant", merchant);
    no_make_address("validator", validator);
    no_ledger_init(&ledger);

    if (!no_mint(&ledger, customer, 100.0, "initial customer funds")) {
        fprintf(stderr, "mint failed: %s\n", no_last_error(&ledger));
        no_ledger_free(&ledger);
        return 1;
    }
    if (!no_ai_create_payment(&ledger, "chat-basic", customer, merchant, 2, &payment)) {
        fprintf(stderr, "payment create failed: %s\n", no_last_error(&ledger));
        no_ledger_free(&ledger);
        return 1;
    }
    if (!no_add_transfer(&ledger, customer, merchant, payment.amount_due, 0.01, payment.memo)) {
        fprintf(stderr, "payment transfer failed: %s\n", no_last_error(&ledger));
        no_ledger_free(&ledger);
        return 1;
    }
    if (!no_produce_block(&ledger, validator)) {
        fprintf(stderr, "block failed: %s\n", no_last_error(&ledger));
        no_ledger_free(&ledger);
        return 1;
    }
    if (!no_ai_verify_payment(&ledger, payment.payment_id, &verified) || !verified.paid) {
        fprintf(stderr, "payment verify failed: %s\n", no_last_error(&ledger));
        no_ledger_free(&ledger);
        return 1;
    }
    if (!no_ai_consume(&ledger, payment.payment_id, "hello from C", response, sizeof(response))) {
        fprintf(stderr, "AI consume failed: %s\n", no_last_error(&ledger));
        no_ledger_free(&ledger);
        return 1;
    }

    printf("NewOrder C smoke OK\n");
    printf("customer=%s balance=%.8f\n", customer, no_balance_of(&ledger, customer));
    printf("merchant=%s balance=%.8f\n", merchant, no_balance_of(&ledger, merchant));
    printf("payment_id=%s paid_txid=%s\n", payment.payment_id, verified.paid_txid);
    printf("ai_response=%s\n", response);
    no_ledger_free(&ledger);
    return 0;
}

static int benchmark(int argc, char **argv) {
    NoLedger ledger;
    char customer[NO_ADDRESS_LEN];
    char recipient[NO_ADDRESS_LEN];
    char validator[NO_ADDRESS_LEN];
    int count = 100000;
    int i;
    double start;
    double add_seconds;
    double block_seconds;

    if (argc >= 3) {
        count = atoi(argv[2]);
    }
    if (count <= 0) {
        count = 100000;
    }

    no_make_address("bench-customer", customer);
    no_make_address("bench-recipient", recipient);
    no_make_address("bench-validator", validator);
    no_ledger_init(&ledger);
    no_mint(&ledger, customer, (double)count + 100.0, "benchmark funds");

    start = seconds_now();
    for (i = 0; i < count; i++) {
        char memo[NO_MEMO_LEN];
        snprintf(memo, sizeof(memo), "bench:%d", i);
        if (!no_add_transfer(&ledger, customer, recipient, 0.001, 0.0, memo)) {
            fprintf(stderr, "add failed at %d: %s\n", i, no_last_error(&ledger));
            no_ledger_free(&ledger);
            return 1;
        }
    }
    add_seconds = seconds_now() - start;

    start = seconds_now();
    if (!no_produce_block(&ledger, validator)) {
        fprintf(stderr, "block failed: %s\n", no_last_error(&ledger));
        no_ledger_free(&ledger);
        return 1;
    }
    block_seconds = seconds_now() - start;

    printf("{\n");
    printf("  \"transactions\": %d,\n", count);
    printf("  \"add_seconds\": %.6f,\n", add_seconds);
    printf("  \"add_tps\": %.2f,\n", (double)count / add_seconds);
    printf("  \"block_seconds\": %.6f,\n", block_seconds);
    printf("  \"block_tps_equivalent\": %.2f,\n", (double)count / block_seconds);
    printf("  \"customer_balance\": %.8f,\n", no_balance_of(&ledger, customer));
    printf("  \"recipient_balance\": %.8f\n", no_balance_of(&ledger, recipient));
    printf("}\n");

    no_ledger_free(&ledger);
    return 0;
}

static void usage(const char *program) {
    printf("Usage:\n");
    printf("  %s smoke\n", program);
    printf("  %s bench [transactions]\n", program);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "smoke") == 0) {
        return smoke();
    }
    if (strcmp(argv[1], "bench") == 0) {
        return benchmark(argc, argv);
    }
    usage(argv[0]);
    return 1;
}
