#include "neworder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NO_INITIAL_CAPACITY 16
#define NO_BLOCK_REWARD 50.0

static uint64_t no_now(void) {
    return (uint64_t)time(NULL);
}

static uint64_t no_fnv1a(const char *text) {
    uint64_t hash = 1469598103934665603ULL;
    while (*text) {
        hash ^= (unsigned char)*text++;
        hash *= 1099511628211ULL;
    }
    return hash;
}

static void no_hex_id(const char *prefix, const char *seed, char *out, size_t out_size) {
    uint64_t a;
    uint64_t b;
    uint64_t c;
    char mixed[512];

    snprintf(mixed, sizeof(mixed), "%s:%s", prefix, seed);
    a = no_fnv1a(mixed);
    snprintf(mixed, sizeof(mixed), "%s:%s:%llu", prefix, seed, (unsigned long long)a);
    b = no_fnv1a(mixed);
    snprintf(mixed, sizeof(mixed), "%s:%s:%llu", prefix, seed, (unsigned long long)b);
    c = no_fnv1a(mixed);
    snprintf(out, out_size, "%s%016llx%016llx%016llx", prefix, (unsigned long long)a, (unsigned long long)b, (unsigned long long)c);
}

static void no_set_error(NoLedger *ledger, const char *message) {
    snprintf(ledger->last_error, sizeof(ledger->last_error), "%s", message);
}

static int no_reserve(void **items, size_t *capacity, size_t needed, size_t item_size) {
    void *next;
    size_t next_capacity = *capacity ? *capacity : NO_INITIAL_CAPACITY;
    if (needed <= *capacity) {
        return 1;
    }
    while (next_capacity < needed) {
        next_capacity *= 2;
    }
    next = realloc(*items, next_capacity * item_size);
    if (!next) {
        return 0;
    }
    *items = next;
    *capacity = next_capacity;
    return 1;
}

static NoAccount *no_find_account(NoLedger *ledger, const char *address) {
    size_t i;
    for (i = 0; i < ledger->account_count; i++) {
        if (strcmp(ledger->accounts[i].address, address) == 0) {
            return &ledger->accounts[i];
        }
    }
    return NULL;
}

static const NoAccount *no_find_account_const(const NoLedger *ledger, const char *address) {
    size_t i;
    for (i = 0; i < ledger->account_count; i++) {
        if (strcmp(ledger->accounts[i].address, address) == 0) {
            return &ledger->accounts[i];
        }
    }
    return NULL;
}

static NoAccount *no_get_account(NoLedger *ledger, const char *address) {
    NoAccount *account = no_find_account(ledger, address);
    if (account) {
        return account;
    }
    if (!no_reserve((void **)&ledger->accounts, &ledger->account_capacity, ledger->account_count + 1, sizeof(NoAccount))) {
        no_set_error(ledger, "out of memory while creating account");
        return NULL;
    }
    account = &ledger->accounts[ledger->account_count++];
    memset(account, 0, sizeof(*account));
    snprintf(account->address, sizeof(account->address), "%s", address);
    return account;
}

static void no_apply_confirmed(NoLedger *ledger, const NoTransaction *tx) {
    NoAccount *sender;
    NoAccount *recipient;
    if (tx->type == NO_TX_COINBASE) {
        recipient = no_get_account(ledger, tx->recipient);
        if (recipient) {
            recipient->balance += tx->amount;
        }
        return;
    }
    sender = no_get_account(ledger, tx->sender);
    recipient = no_get_account(ledger, tx->recipient);
    if (sender) {
        sender->balance -= tx->amount + tx->fee;
        if (sender->pending_debit >= tx->amount + tx->fee) {
            sender->pending_debit -= tx->amount + tx->fee;
        } else {
            sender->pending_debit = 0.0;
        }
    }
    if (recipient) {
        recipient->balance += tx->amount;
    }
}

static void no_block_hash(NoBlock *block) {
    char seed[512];
    snprintf(
        seed,
        sizeof(seed),
        "%llu:%s:%s:%llu:%zu",
        (unsigned long long)block->index,
        block->previous_hash,
        block->validator,
        (unsigned long long)block->timestamp,
        block->transaction_count
    );
    no_hex_id("B", seed, block->hash, sizeof(block->hash));
}

void no_make_address(const char *seed, char out[NO_ADDRESS_LEN]) {
    no_hex_id("NO", seed, out, NO_ADDRESS_LEN);
}

void no_make_txid(const NoTransaction *tx, char out[NO_ID_LEN]) {
    char seed[768];
    snprintf(
        seed,
        sizeof(seed),
        "%d:%s:%s:%.8f:%.8f:%llu:%s",
        (int)tx->type,
        tx->sender,
        tx->recipient,
        tx->amount,
        tx->fee,
        (unsigned long long)tx->timestamp,
        tx->memo
    );
    no_hex_id("T", seed, out, NO_ID_LEN);
}

void no_ledger_init(NoLedger *ledger) {
    NoBlock *genesis;
    memset(ledger, 0, sizeof(*ledger));
    no_reserve((void **)&ledger->blocks, &ledger->block_capacity, 1, sizeof(NoBlock));
    genesis = &ledger->blocks[ledger->block_count++];
    memset(genesis, 0, sizeof(*genesis));
    genesis->index = 0;
    snprintf(genesis->previous_hash, sizeof(genesis->previous_hash), "%064d", 0);
    snprintf(genesis->validator, sizeof(genesis->validator), "GENESIS");
    genesis->timestamp = 0;
    no_block_hash(genesis);
}

void no_ledger_free(NoLedger *ledger) {
    size_t i;
    for (i = 0; i < ledger->block_count; i++) {
        free(ledger->blocks[i].transactions);
    }
    free(ledger->blocks);
    free(ledger->mempool);
    free(ledger->accounts);
    free(ledger->payments);
    memset(ledger, 0, sizeof(*ledger));
}

int no_mint(NoLedger *ledger, const char *recipient, double amount, const char *memo) {
    NoTransaction tx;
    if (amount <= 0.0) {
        no_set_error(ledger, "mint amount must be positive");
        return 0;
    }
    memset(&tx, 0, sizeof(tx));
    tx.type = NO_TX_COINBASE;
    snprintf(tx.sender, sizeof(tx.sender), "COINBASE");
    snprintf(tx.recipient, sizeof(tx.recipient), "%s", recipient);
    tx.amount = amount;
    tx.timestamp = no_now();
    snprintf(tx.memo, sizeof(tx.memo), "%s", memo ? memo : "NewOrder C mint");
    no_make_txid(&tx, tx.txid);
    no_apply_confirmed(ledger, &tx);
    return 1;
}

int no_add_transfer(NoLedger *ledger, const char *sender, const char *recipient, double amount, double fee, const char *memo) {
    NoTransaction *tx;
    NoAccount *sender_account;
    double total;

    if (strncmp(sender, "NO", 2) != 0 || strncmp(recipient, "NO", 2) != 0) {
        no_set_error(ledger, "sender and recipient must be NO addresses");
        return 0;
    }
    if (amount <= 0.0) {
        no_set_error(ledger, "amount must be positive");
        return 0;
    }
    if (fee < 0.0) {
        no_set_error(ledger, "fee cannot be negative");
        return 0;
    }
    sender_account = no_get_account(ledger, sender);
    if (!sender_account) {
        return 0;
    }
    total = amount + fee;
    if (sender_account->balance - sender_account->pending_debit < total) {
        no_set_error(ledger, "insufficient balance");
        return 0;
    }
    if (!no_reserve((void **)&ledger->mempool, &ledger->mempool_capacity, ledger->mempool_count + 1, sizeof(NoTransaction))) {
        no_set_error(ledger, "out of memory while adding transaction");
        return 0;
    }
    tx = &ledger->mempool[ledger->mempool_count++];
    memset(tx, 0, sizeof(*tx));
    tx->type = NO_TX_TRANSFER;
    snprintf(tx->sender, sizeof(tx->sender), "%s", sender);
    snprintf(tx->recipient, sizeof(tx->recipient), "%s", recipient);
    tx->amount = amount;
    tx->fee = fee;
    tx->timestamp = no_now();
    snprintf(tx->memo, sizeof(tx->memo), "%s", memo ? memo : "");
    no_make_txid(tx, tx->txid);
    sender_account->pending_debit += total;
    return 1;
}

int no_produce_block(NoLedger *ledger, const char *validator) {
    NoBlock *block;
    NoTransaction coinbase;
    uint64_t previous_index;
    char previous_hash[NO_ID_LEN];
    size_t i;
    double fees = 0.0;

    if (ledger->block_count == 0) {
        no_set_error(ledger, "ledger has no genesis block");
        return 0;
    }
    previous_index = ledger->blocks[ledger->block_count - 1].index;
    snprintf(previous_hash, sizeof(previous_hash), "%s", ledger->blocks[ledger->block_count - 1].hash);
    for (i = 0; i < ledger->mempool_count; i++) {
        fees += ledger->mempool[i].fee;
    }
    if (!no_reserve((void **)&ledger->blocks, &ledger->block_capacity, ledger->block_count + 1, sizeof(NoBlock))) {
        no_set_error(ledger, "out of memory while creating block");
        return 0;
    }
    block = &ledger->blocks[ledger->block_count++];
    memset(block, 0, sizeof(*block));
    block->index = previous_index + 1;
    snprintf(block->previous_hash, sizeof(block->previous_hash), "%s", previous_hash);
    snprintf(block->validator, sizeof(block->validator), "%s", validator);
    block->timestamp = no_now();
    block->transaction_capacity = ledger->mempool_count + 1;
    block->transactions = calloc(block->transaction_capacity, sizeof(NoTransaction));
    if (!block->transactions) {
        no_set_error(ledger, "out of memory while storing block transactions");
        return 0;
    }

    memset(&coinbase, 0, sizeof(coinbase));
    coinbase.type = NO_TX_COINBASE;
    snprintf(coinbase.sender, sizeof(coinbase.sender), "COINBASE");
    snprintf(coinbase.recipient, sizeof(coinbase.recipient), "%s", validator);
    coinbase.amount = NO_BLOCK_REWARD + fees;
    coinbase.timestamp = no_now();
    snprintf(coinbase.memo, sizeof(coinbase.memo), "NewOrder C validator reward");
    no_make_txid(&coinbase, coinbase.txid);
    block->transactions[block->transaction_count++] = coinbase;
    no_apply_confirmed(ledger, &coinbase);

    for (i = 0; i < ledger->mempool_count; i++) {
        block->transactions[block->transaction_count++] = ledger->mempool[i];
        no_apply_confirmed(ledger, &ledger->mempool[i]);
    }
    ledger->mempool_count = 0;
    no_block_hash(block);
    return 1;
}

double no_balance_of(const NoLedger *ledger, const char *address) {
    const NoAccount *account = no_find_account_const(ledger, address);
    if (!account) {
        return 0.0;
    }
    return account->balance - account->pending_debit;
}

static double no_ai_price(const char *service_id) {
    if (strcmp(service_id, "chat-basic") == 0) {
        return 0.25;
    }
    if (strcmp(service_id, "summary") == 0) {
        return 0.75;
    }
    return -1.0;
}

static NoAiPayment *no_find_payment(NoLedger *ledger, const char *payment_id) {
    size_t i;
    for (i = 0; i < ledger->payment_count; i++) {
        if (strcmp(ledger->payments[i].payment_id, payment_id) == 0) {
            return &ledger->payments[i];
        }
    }
    return NULL;
}

int no_ai_create_payment(NoLedger *ledger, const char *service_id, const char *customer_address, const char *merchant_address, int units, NoAiPayment *out) {
    NoAiPayment *payment;
    char seed[256];
    double price = no_ai_price(service_id);
    if (price < 0.0) {
        no_set_error(ledger, "unknown AI service");
        return 0;
    }
    if (units <= 0) {
        no_set_error(ledger, "units must be positive");
        return 0;
    }
    if (!no_reserve((void **)&ledger->payments, &ledger->payment_capacity, ledger->payment_count + 1, sizeof(NoAiPayment))) {
        no_set_error(ledger, "out of memory while creating AI payment");
        return 0;
    }
    payment = &ledger->payments[ledger->payment_count++];
    memset(payment, 0, sizeof(*payment));
    snprintf(
        seed,
        sizeof(seed),
        "%s:%s:%s:%d:%zu:%llu",
        service_id,
        customer_address,
        merchant_address,
        units,
        ledger->payment_count,
        (unsigned long long)no_now()
    );
    no_hex_id("AIP", seed, payment->payment_id, sizeof(payment->payment_id));
    snprintf(payment->service_id, sizeof(payment->service_id), "%s", service_id);
    snprintf(payment->customer_address, sizeof(payment->customer_address), "%s", customer_address);
    snprintf(payment->merchant_address, sizeof(payment->merchant_address), "%s", merchant_address);
    payment->units = units;
    payment->amount_due = price * units;
    snprintf(payment->memo, sizeof(payment->memo), "AIPAY:%s", payment->payment_id);
    if (out) {
        *out = *payment;
    }
    return 1;
}

int no_ai_verify_payment(NoLedger *ledger, const char *payment_id, NoAiPayment *out) {
    NoAiPayment *payment = no_find_payment(ledger, payment_id);
    size_t i;
    size_t j;
    if (!payment) {
        no_set_error(ledger, "payment not found");
        return 0;
    }
    if (!payment->paid) {
        for (i = 0; i < ledger->block_count; i++) {
            for (j = 0; j < ledger->blocks[i].transaction_count; j++) {
                const NoTransaction *tx = &ledger->blocks[i].transactions[j];
                if (
                    tx->type == NO_TX_TRANSFER &&
                    strcmp(tx->sender, payment->customer_address) == 0 &&
                    strcmp(tx->recipient, payment->merchant_address) == 0 &&
                    strcmp(tx->memo, payment->memo) == 0 &&
                    tx->amount + 0.00000001 >= payment->amount_due
                ) {
                    payment->paid = 1;
                    snprintf(payment->paid_txid, sizeof(payment->paid_txid), "%s", tx->txid);
                    break;
                }
            }
            if (payment->paid) {
                break;
            }
        }
    }
    if (out) {
        *out = *payment;
    }
    return 1;
}

int no_ai_consume(NoLedger *ledger, const char *payment_id, const char *prompt, char *response, size_t response_size) {
    NoAiPayment *payment;
    char digest[NO_ID_LEN];
    if (!no_ai_verify_payment(ledger, payment_id, NULL)) {
        return 0;
    }
    payment = no_find_payment(ledger, payment_id);
    if (!payment || !payment->paid) {
        no_set_error(ledger, "payment is not paid");
        return 0;
    }
    if (payment->consumed_units >= payment->units) {
        no_set_error(ledger, "payment has no remaining units");
        return 0;
    }
    payment->consumed_units++;
    payment->consumed = payment->consumed_units >= payment->units;
    no_hex_id("R", prompt ? prompt : "", digest, sizeof(digest));
    snprintf(response, response_size, "Processed paid NewOrder C AI request %.16s", digest);
    return 1;
}

const char *no_last_error(const NoLedger *ledger) {
    return ledger->last_error[0] ? ledger->last_error : "no error";
}
