#ifndef NEWORDER_H
#define NEWORDER_H

#include <stddef.h>
#include <stdint.h>

#define NO_ADDRESS_LEN 48
#define NO_ID_LEN 72
#define NO_MEMO_LEN 96
#define NO_SERVICE_LEN 32
#define NO_ERROR_LEN 160

typedef enum {
    NO_TX_COINBASE = 0,
    NO_TX_TRANSFER = 1
} NoTxType;

typedef struct {
    char txid[NO_ID_LEN];
    NoTxType type;
    char sender[NO_ADDRESS_LEN];
    char recipient[NO_ADDRESS_LEN];
    double amount;
    double fee;
    uint64_t timestamp;
    char memo[NO_MEMO_LEN];
} NoTransaction;

typedef struct {
    uint64_t index;
    char previous_hash[NO_ID_LEN];
    char hash[NO_ID_LEN];
    char validator[NO_ADDRESS_LEN];
    NoTransaction *transactions;
    size_t transaction_count;
    size_t transaction_capacity;
    uint64_t timestamp;
} NoBlock;

typedef struct {
    char address[NO_ADDRESS_LEN];
    double balance;
    double pending_debit;
} NoAccount;

typedef struct {
    char payment_id[NO_ID_LEN];
    char service_id[NO_SERVICE_LEN];
    char customer_address[NO_ADDRESS_LEN];
    char merchant_address[NO_ADDRESS_LEN];
    int units;
    int consumed_units;
    double amount_due;
    char memo[NO_MEMO_LEN];
    int paid;
    int consumed;
    char paid_txid[NO_ID_LEN];
} NoAiPayment;

typedef struct {
    NoBlock *blocks;
    size_t block_count;
    size_t block_capacity;
    NoTransaction *mempool;
    size_t mempool_count;
    size_t mempool_capacity;
    NoAccount *accounts;
    size_t account_count;
    size_t account_capacity;
    NoAiPayment *payments;
    size_t payment_count;
    size_t payment_capacity;
    char last_error[NO_ERROR_LEN];
} NoLedger;

void no_ledger_init(NoLedger *ledger);
void no_ledger_free(NoLedger *ledger);

void no_make_address(const char *seed, char out[NO_ADDRESS_LEN]);
void no_make_txid(const NoTransaction *tx, char out[NO_ID_LEN]);

int no_mint(NoLedger *ledger, const char *recipient, double amount, const char *memo);
int no_add_transfer(NoLedger *ledger, const char *sender, const char *recipient, double amount, double fee, const char *memo);
int no_produce_block(NoLedger *ledger, const char *validator);
double no_balance_of(const NoLedger *ledger, const char *address);

int no_ai_create_payment(
    NoLedger *ledger,
    const char *service_id,
    const char *customer_address,
    const char *merchant_address,
    int units,
    NoAiPayment *out
);
int no_ai_verify_payment(NoLedger *ledger, const char *payment_id, NoAiPayment *out);
int no_ai_consume(NoLedger *ledger, const char *payment_id, const char *prompt, char *response, size_t response_size);

const char *no_last_error(const NoLedger *ledger);

#endif
