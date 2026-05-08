#ifndef NEWORDER_H
#define NEWORDER_H

#include <stddef.h>
#include <stdint.h>

/* ── field size constants ─────────────────────────────────────────────────── */
#define NO_ADDRESS_LEN   48
#define NO_ID_LEN        72
#define NO_MEMO_LEN      96
#define NO_SERVICE_LEN   32
#define NO_ERROR_LEN    160
#define NO_TOKEN_NAME_LEN 64
#define NO_TOKEN_SYM_LEN  16
#define NO_CONTRACT_NAME_LEN 64
#define NO_METHOD_LEN    32
#define NO_KV_KEY_LEN    64
#define NO_KV_VAL_LEN   256
#define NO_KV_MAX       128

/* ── transaction types ────────────────────────────────────────────────────── */
typedef enum {
    NO_TX_COINBASE         = 0,
    NO_TX_TRANSFER         = 1,
    NO_TX_TOKEN_CREATE     = 2,
    NO_TX_TOKEN_TRANSFER   = 3,
    NO_TX_CONTRACT_DEPLOY  = 4,
    NO_TX_CONTRACT_CALL    = 5
} NoTxType;

/* ── Transaction ──────────────────────────────────────────────────────────── */
typedef struct {
    char     txid[NO_ID_LEN];
    NoTxType type;
    char     sender[NO_ADDRESS_LEN];
    char     recipient[NO_ADDRESS_LEN];
    double   amount;
    double   fee;
    uint64_t timestamp;
    char     memo[NO_MEMO_LEN];

    /* token_create fields */
    char   token_name[NO_TOKEN_NAME_LEN];
    char   token_symbol[NO_TOKEN_SYM_LEN];
    double token_total_supply;
    int    token_decimals;

    /* token_transfer / contract_call — token/contract identifier */
    char   ref_id[NO_ID_LEN];       /* token_id or contract_id */
    char   token_to[NO_ADDRESS_LEN];
    double token_amount;

    /* contract_deploy fields */
    char contract_name[NO_CONTRACT_NAME_LEN];
    int  contract_is_counter;        /* 1 = counter, 0 = key_value */

    /* contract_call fields */
    char      contract_method[NO_METHOD_LEN];
    long long contract_arg_amount;
    char      contract_arg_key[NO_KV_KEY_LEN];
    char      contract_arg_value[NO_KV_VAL_LEN];
} NoTransaction;

/* ── Block ────────────────────────────────────────────────────────────────── */
typedef struct {
    uint64_t index;
    char     previous_hash[NO_ID_LEN];
    char     hash[NO_ID_LEN];
    char     validator[NO_ADDRESS_LEN];
    uint64_t timestamp;
    NoTransaction *transactions;
    size_t   transaction_count;
    size_t   transaction_capacity;
} NoBlock;

/* ── Account (indexed balance) ────────────────────────────────────────────── */
typedef struct {
    char   address[NO_ADDRESS_LEN];
    double balance;
    double pending_debit;
} NoAccount;

/* ── Token ────────────────────────────────────────────────────────────────── */
typedef struct {
    char   token_id[NO_ID_LEN];
    char   name[NO_TOKEN_NAME_LEN];
    char   symbol[NO_TOKEN_SYM_LEN];
    char   issuer[NO_ADDRESS_LEN];
    double total_supply;
    int    decimals;
    uint64_t created_at_block;
    char   create_txid[NO_ID_LEN];
} NoToken;

/* ── Token balance (indexed) ──────────────────────────────────────────────── */
typedef struct {
    char   token_id[NO_ID_LEN];
    char   address[NO_ADDRESS_LEN];
    double balance;
} NoTokenBalance;

/* ── Smart contract ───────────────────────────────────────────────────────── */
typedef struct {
    char key[NO_KV_KEY_LEN];
    char value[NO_KV_VAL_LEN];
} NoKVEntry;

typedef struct {
    char     contract_id[NO_ID_LEN];
    char     name[NO_CONTRACT_NAME_LEN];
    char     owner[NO_ADDRESS_LEN];
    int      is_counter;
    uint64_t created_at_block;
    char     deploy_txid[NO_ID_LEN];
    /* live state */
    long long    counter_value;
    NoKVEntry    kv[NO_KV_MAX];
    size_t       kv_count;
} NoContract;

/* ── AI payment ───────────────────────────────────────────────────────────── */
typedef struct {
    char   payment_id[NO_ID_LEN];
    char   service_id[NO_SERVICE_LEN];
    char   customer_address[NO_ADDRESS_LEN];
    char   merchant_address[NO_ADDRESS_LEN];
    int    units;
    int    consumed_units;
    double amount_due;
    char   memo[NO_MEMO_LEN];
    int    paid;
    int    consumed;
    char   paid_txid[NO_ID_LEN];
} NoAiPayment;

/* ── Ledger ───────────────────────────────────────────────────────────────── */
typedef struct {
    /* blocks */
    NoBlock *blocks;
    size_t   block_count;
    size_t   block_capacity;
    /* mempool */
    NoTransaction *mempool;
    size_t         mempool_count;
    size_t         mempool_capacity;
    /* native coin accounts (indexed) */
    NoAccount *accounts;
    size_t     account_count;
    size_t     account_capacity;
    /* tokens */
    NoToken *tokens;
    size_t   token_count;
    size_t   token_capacity;
    /* token balances (indexed) */
    NoTokenBalance *token_balances;
    size_t          token_balance_count;
    size_t          token_balance_capacity;
    /* smart contracts */
    NoContract *contracts;
    size_t      contract_count;
    size_t      contract_capacity;
    /* AI payments */
    NoAiPayment *payments;
    size_t       payment_count;
    size_t       payment_capacity;
    /* PoA validators (round-robin) */
    char   validator_addrs[16][NO_ADDRESS_LEN];
    size_t validator_count;
    /* error string */
    char last_error[NO_ERROR_LEN];
} NoLedger;

/* ── Core ledger API ──────────────────────────────────────────────────────── */
void   no_ledger_init(NoLedger *ledger);
void   no_ledger_free(NoLedger *ledger);

/* address / id helpers */
void   no_make_address(const char *seed, char out[NO_ADDRESS_LEN]);
void   no_make_txid(const NoTransaction *tx, char out[NO_ID_LEN]);

/* native coin */
int    no_mint(NoLedger *ledger, const char *recipient, double amount, const char *memo);
int    no_add_transfer(NoLedger *ledger, const char *sender, const char *recipient,
                       double amount, double fee, const char *memo);
int    no_produce_block(NoLedger *ledger, const char *validator);
double no_balance_of(const NoLedger *ledger, const char *address);
double no_supply(const NoLedger *ledger);

/* token operations */
int    no_token_create(NoLedger *ledger, const char *sender,
                       const char *name, const char *symbol,
                       double total_supply, int decimals, double fee,
                       char token_id_out[NO_ID_LEN]);
int    no_token_transfer(NoLedger *ledger, const char *sender,
                         const char *token_id, const char *to,
                         double amount, double fee);
double no_token_balance_of(const NoLedger *ledger,
                           const char *token_id, const char *address);
const NoToken   *no_token_find(const NoLedger *ledger, const char *token_id);

/* smart contract operations */
int    no_contract_deploy(NoLedger *ledger, const char *sender,
                          const char *name, int is_counter, double fee,
                          char contract_id_out[NO_ID_LEN]);
int    no_contract_call(NoLedger *ledger, const char *sender,
                        const char *contract_id, const char *method,
                        long long arg_amount, const char *arg_key,
                        const char *arg_value, double fee);
const NoContract *no_contract_find(const NoLedger *ledger, const char *contract_id);

/* PoA validators */
int         no_set_validators(NoLedger *ledger,
                               const char addrs[][NO_ADDRESS_LEN], size_t count);
const char *no_next_validator(const NoLedger *ledger);
int         no_can_produce(const NoLedger *ledger, const char *validator);

/* block validation */
int    no_validate_block(const NoLedger *ledger, const NoBlock *block);

/* AI payment gateway */
int    no_ai_create_payment(NoLedger *ledger, const char *service_id,
                             const char *customer_address,
                             const char *merchant_address, int units,
                             NoAiPayment *out);
int    no_ai_verify_payment(NoLedger *ledger, const char *payment_id,
                             NoAiPayment *out);
int    no_ai_consume(NoLedger *ledger, const char *payment_id,
                     const char *prompt, char *response, size_t response_size);

/* persistence */
int    no_save_chain(const NoLedger *ledger, const char *path);
int    no_load_chain(NoLedger *ledger, const char *path);
int    no_save_payments(const NoLedger *ledger, const char *path);
int    no_load_payments(NoLedger *ledger, const char *path);

const char *no_last_error(const NoLedger *ledger);

#endif
