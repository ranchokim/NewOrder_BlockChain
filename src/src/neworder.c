/*
 * NewOrder C ledger — core blockchain state, tokens, contracts, PoA validators.
 * Crypto note: block/tx IDs use SHA-256 (replacing the original FNV1a).
 * Signature verification is not implemented; the node trusts submitted keys.
 */
#include "neworder.h"
#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NO_INITIAL_CAPACITY 16
#define NO_BLOCK_REWARD     50.0
#define NO_MAX_SUPPLY       21000000.0
#define NO_COIN_SYMBOL      "NO"

/* ── internal helpers ─────────────────────────────────────────────────────── */

static uint64_t no_now(void) {
    return (uint64_t)time(NULL);
}

static void no_set_error(NoLedger *l, const char *msg) {
    snprintf(l->last_error, sizeof(l->last_error), "%s", msg);
}

static int no_reserve(void **items, size_t *cap, size_t needed, size_t isz) {
    void *next;
    size_t nc = *cap ? *cap : NO_INITIAL_CAPACITY;
    if (needed <= *cap) return 1;
    while (nc < needed) nc *= 2;
    next = realloc(*items, nc * isz);
    if (!next) return 0;
    *items = next; *cap = nc;
    return 1;
}

/* ── address / txid generation (SHA-256 based) ────────────────────────────── */

void no_make_address(const char *seed, char out[NO_ADDRESS_LEN]) {
    char buf[256]; char hex[65];
    snprintf(buf, sizeof(buf), "neworder-address:%s", seed);
    sha256_hex(buf, strlen(buf), hex);
    snprintf(out, NO_ADDRESS_LEN, "NO%.38s", hex);
}

void no_make_txid(const NoTransaction *tx, char out[NO_ID_LEN]) {
    char buf[1024]; char hex[65];
    snprintf(buf, sizeof(buf),
        "tx:%d:%s:%s:%.8f:%.8f:%llu:%s",
        (int)tx->type, tx->sender, tx->recipient,
        tx->amount, tx->fee,
        (unsigned long long)tx->timestamp,
        tx->memo);
    sha256_hex(buf, strlen(buf), hex);
    snprintf(out, NO_ID_LEN, "T%.62s", hex);
}

static void no_make_block_hash(NoBlock *blk) {
    char buf[512]; char hex[65];
    snprintf(buf, sizeof(buf),
        "block:%llu:%s:%s:%llu:%zu",
        (unsigned long long)blk->index,
        blk->previous_hash,
        blk->validator,
        (unsigned long long)blk->timestamp,
        blk->transaction_count);
    sha256_hex(buf, strlen(buf), hex);
    snprintf(blk->hash, NO_ID_LEN, "B%.62s", hex);
}

static void no_make_token_id(const char *txid, char out[NO_ID_LEN]) {
    char hex[65];
    sha256_hex(txid, strlen(txid), hex);
    snprintf(out, NO_ID_LEN, "NOT%.38s", hex);
}

static void no_make_contract_id(const char *txid, char out[NO_ID_LEN]) {
    char hex[65];
    sha256_hex(txid, strlen(txid), hex);
    snprintf(out, NO_ID_LEN, "NOC%.38s", hex);
}

/* ── account index ────────────────────────────────────────────────────────── */

static NoAccount *no_find_account(NoLedger *l, const char *addr) {
    size_t i;
    for (i = 0; i < l->account_count; i++)
        if (strcmp(l->accounts[i].address, addr) == 0) return &l->accounts[i];
    return NULL;
}

static const NoAccount *no_find_account_c(const NoLedger *l, const char *addr) {
    size_t i;
    for (i = 0; i < l->account_count; i++)
        if (strcmp(l->accounts[i].address, addr) == 0) return &l->accounts[i];
    return NULL;
}

static NoAccount *no_get_account(NoLedger *l, const char *addr) {
    NoAccount *a = no_find_account(l, addr);
    if (a) return a;
    if (!no_reserve((void **)&l->accounts, &l->account_capacity,
                    l->account_count + 1, sizeof(NoAccount))) {
        no_set_error(l, "out of memory: account");
        return NULL;
    }
    a = &l->accounts[l->account_count++];
    memset(a, 0, sizeof(*a));
    snprintf(a->address, sizeof(a->address), "%s", addr);
    return a;
}

/* ── token balance index ──────────────────────────────────────────────────── */

static NoTokenBalance *no_find_tbal(NoLedger *l,
                                     const char *token_id, const char *addr) {
    size_t i;
    for (i = 0; i < l->token_balance_count; i++) {
        NoTokenBalance *tb = &l->token_balances[i];
        if (strcmp(tb->token_id, token_id) == 0 &&
            strcmp(tb->address,  addr)     == 0) return tb;
    }
    return NULL;
}

static const NoTokenBalance *no_find_tbal_c(const NoLedger *l,
                                             const char *token_id,
                                             const char *addr) {
    size_t i;
    for (i = 0; i < l->token_balance_count; i++) {
        const NoTokenBalance *tb = &l->token_balances[i];
        if (strcmp(tb->token_id, token_id) == 0 &&
            strcmp(tb->address,  addr)     == 0) return tb;
    }
    return NULL;
}

static NoTokenBalance *no_get_tbal(NoLedger *l,
                                    const char *token_id, const char *addr) {
    NoTokenBalance *tb = no_find_tbal(l, token_id, addr);
    if (tb) return tb;
    if (!no_reserve((void **)&l->token_balances, &l->token_balance_capacity,
                    l->token_balance_count + 1, sizeof(NoTokenBalance))) {
        no_set_error(l, "out of memory: token balance");
        return NULL;
    }
    tb = &l->token_balances[l->token_balance_count++];
    memset(tb, 0, sizeof(*tb));
    snprintf(tb->token_id, sizeof(tb->token_id), "%s", token_id);
    snprintf(tb->address,  sizeof(tb->address),  "%s", addr);
    return tb;
}

/* ── apply a confirmed transaction to indexed state ───────────────────────── */

static void no_apply_confirmed(NoLedger *l, const NoTransaction *tx) {
    NoAccount *sender, *recipient;
    NoTokenBalance *tbsender, *tbrecipient;
    NoToken *tok;
    NoContract *con;
    size_t i;

    switch (tx->type) {
    case NO_TX_COINBASE:
        recipient = no_get_account(l, tx->recipient);
        if (recipient) recipient->balance += tx->amount;
        break;

    case NO_TX_TRANSFER:
        sender    = no_get_account(l, tx->sender);
        recipient = no_get_account(l, tx->recipient);
        if (sender) {
            sender->balance -= tx->amount + tx->fee;
            if (sender->pending_debit >= tx->amount + tx->fee)
                sender->pending_debit -= tx->amount + tx->fee;
            else
                sender->pending_debit = 0.0;
        }
        if (recipient) recipient->balance += tx->amount;
        break;

    case NO_TX_TOKEN_CREATE:
        /* deduct fee from sender */
        sender = no_get_account(l, tx->sender);
        if (sender) {
            sender->balance -= tx->fee;
            if (sender->pending_debit >= tx->fee)
                sender->pending_debit -= tx->fee;
            else
                sender->pending_debit = 0.0;
        }
        /* register token */
        if (!no_reserve((void **)&l->tokens, &l->token_capacity,
                        l->token_count + 1, sizeof(NoToken))) break;
        tok = &l->tokens[l->token_count++];
        memset(tok, 0, sizeof(*tok));
        no_make_token_id(tx->txid, tok->token_id);
        snprintf(tok->name,    sizeof(tok->name),    "%s", tx->token_name);
        snprintf(tok->symbol,  sizeof(tok->symbol),  "%s", tx->token_symbol);
        snprintf(tok->issuer,  sizeof(tok->issuer),  "%s", tx->sender);
        tok->total_supply     = tx->token_total_supply;
        tok->decimals         = tx->token_decimals;
        tok->created_at_block = (l->block_count > 0) ? l->blocks[l->block_count-1].index : 0;
        snprintf(tok->create_txid, sizeof(tok->create_txid), "%s", tx->txid);
        /* credit full supply to issuer */
        tbsender = no_get_tbal(l, tok->token_id, tx->sender);
        if (tbsender) tbsender->balance += tx->token_total_supply;
        break;

    case NO_TX_TOKEN_TRANSFER:
        /* deduct native fee from sender */
        sender = no_get_account(l, tx->sender);
        if (sender) {
            sender->balance -= tx->fee;
            if (sender->pending_debit >= tx->fee)
                sender->pending_debit -= tx->fee;
            else
                sender->pending_debit = 0.0;
        }
        /* move token balance */
        tbsender   = no_get_tbal(l, tx->ref_id, tx->sender);
        tbrecipient = no_get_tbal(l, tx->ref_id, tx->token_to);
        if (tbsender)    tbsender->balance   -= tx->token_amount;
        if (tbrecipient) tbrecipient->balance += tx->token_amount;
        break;

    case NO_TX_CONTRACT_DEPLOY:
        sender = no_get_account(l, tx->sender);
        if (sender) {
            sender->balance -= tx->fee;
            if (sender->pending_debit >= tx->fee)
                sender->pending_debit -= tx->fee;
            else
                sender->pending_debit = 0.0;
        }
        if (!no_reserve((void **)&l->contracts, &l->contract_capacity,
                        l->contract_count + 1, sizeof(NoContract))) break;
        con = &l->contracts[l->contract_count++];
        memset(con, 0, sizeof(*con));
        no_make_contract_id(tx->txid, con->contract_id);
        snprintf(con->name,  sizeof(con->name),  "%s", tx->contract_name);
        snprintf(con->owner, sizeof(con->owner), "%s", tx->sender);
        con->is_counter       = tx->contract_is_counter;
        con->created_at_block = (l->block_count > 0) ? l->blocks[l->block_count-1].index : 0;
        snprintf(con->deploy_txid, sizeof(con->deploy_txid), "%s", tx->txid);
        break;

    case NO_TX_CONTRACT_CALL:
        sender = no_get_account(l, tx->sender);
        if (sender) {
            sender->balance -= tx->fee;
            if (sender->pending_debit >= tx->fee)
                sender->pending_debit -= tx->fee;
            else
                sender->pending_debit = 0.0;
        }
        /* find contract and apply state change */
        for (i = 0; i < l->contract_count; i++) {
            con = &l->contracts[i];
            if (strcmp(con->contract_id, tx->ref_id) != 0) continue;
            if (con->is_counter) {
                if (strcmp(tx->contract_method, "increment") == 0)
                    con->counter_value += tx->contract_arg_amount ? tx->contract_arg_amount : 1;
                else if (strcmp(tx->contract_method, "decrement") == 0)
                    con->counter_value -= tx->contract_arg_amount ? tx->contract_arg_amount : 1;
                else if (strcmp(tx->contract_method, "reset") == 0)
                    con->counter_value = 0;
            } else {
                if (strcmp(tx->contract_method, "set") == 0 &&
                    tx->contract_arg_key[0]) {
                    size_t j;
                    for (j = 0; j < con->kv_count; j++) {
                        if (strcmp(con->kv[j].key, tx->contract_arg_key) == 0) {
                            snprintf(con->kv[j].value, NO_KV_VAL_LEN,
                                     "%s", tx->contract_arg_value);
                            goto done_kv;
                        }
                    }
                    if (con->kv_count < NO_KV_MAX) {
                        snprintf(con->kv[con->kv_count].key,   NO_KV_KEY_LEN,
                                 "%s", tx->contract_arg_key);
                        snprintf(con->kv[con->kv_count].value, NO_KV_VAL_LEN,
                                 "%s", tx->contract_arg_value);
                        con->kv_count++;
                    }
                    done_kv:;
                } else if (strcmp(tx->contract_method, "delete") == 0 &&
                           tx->contract_arg_key[0]) {
                    size_t j;
                    for (j = 0; j < con->kv_count; j++) {
                        if (strcmp(con->kv[j].key, tx->contract_arg_key) == 0) {
                            con->kv[j] = con->kv[--con->kv_count];
                            break;
                        }
                    }
                }
            }
            break;
        }
        break;
    }
}

/* ── ledger init / free ───────────────────────────────────────────────────── */

void no_ledger_init(NoLedger *l) {
    NoBlock *genesis;
    NoTransaction coinbase;
    memset(l, 0, sizeof(*l));
    no_reserve((void **)&l->blocks, &l->block_capacity, 1, sizeof(NoBlock));
    genesis = &l->blocks[l->block_count++];
    memset(genesis, 0, sizeof(*genesis));
    genesis->index = 0;
    snprintf(genesis->previous_hash, sizeof(genesis->previous_hash),
             "%064d", 0);
    snprintf(genesis->validator, sizeof(genesis->validator), "GENESIS");
    genesis->timestamp = 0;

    /* genesis coinbase (amount 0) */
    genesis->transaction_capacity = 1;
    genesis->transactions = calloc(1, sizeof(NoTransaction));
    if (genesis->transactions) {
        memset(&coinbase, 0, sizeof(coinbase));
        coinbase.type = NO_TX_COINBASE;
        snprintf(coinbase.sender,    sizeof(coinbase.sender),    "COINBASE");
        snprintf(coinbase.recipient, sizeof(coinbase.recipient), "NO_GENESIS");
        coinbase.amount    = 0.0;
        coinbase.timestamp = 0;
        snprintf(coinbase.memo, sizeof(coinbase.memo), "NewOrder genesis block");
        no_make_txid(&coinbase, coinbase.txid);
        genesis->transactions[genesis->transaction_count++] = coinbase;
    }
    no_make_block_hash(genesis);
}

void no_ledger_free(NoLedger *l) {
    size_t i;
    for (i = 0; i < l->block_count; i++)
        free(l->blocks[i].transactions);
    free(l->blocks);
    free(l->mempool);
    free(l->accounts);
    free(l->tokens);
    free(l->token_balances);
    free(l->contracts);
    free(l->payments);
    memset(l, 0, sizeof(*l));
}

/* ── PoA validators ───────────────────────────────────────────────────────── */

int no_set_validators(NoLedger *l,
                       const char addrs[][NO_ADDRESS_LEN], size_t count) {
    size_t i;
    if (count > 16) { no_set_error(l, "max 16 validators"); return 0; }
    l->validator_count = count;
    for (i = 0; i < count; i++)
        snprintf(l->validator_addrs[i], NO_ADDRESS_LEN, "%s", addrs[i]);
    return 1;
}

const char *no_next_validator(const NoLedger *l) {
    uint64_t next_index;
    if (!l->validator_count) return "";
    next_index = (l->block_count > 0) ? l->blocks[l->block_count-1].index + 1 : 1;
    return l->validator_addrs[(next_index - 1) % l->validator_count];
}

int no_can_produce(const NoLedger *l, const char *validator) {
    const char *expected = no_next_validator(l);
    if (!expected[0]) return 1; /* no validators configured — anyone can produce */
    return strcmp(validator, expected) == 0;
}

/* ── native coin ──────────────────────────────────────────────────────────── */

double no_balance_of(const NoLedger *l, const char *addr) {
    const NoAccount *a = no_find_account_c(l, addr);
    if (!a) return 0.0;
    return a->balance - a->pending_debit;
}

double no_supply(const NoLedger *l) {
    /* coinbase payouts include base reward + collected fees.
       Subtract fees so only newly minted coins are counted. */
    double minted = 0.0;
    size_t i, j;
    for (i = 0; i < l->block_count; i++) {
        const NoBlock *blk = &l->blocks[i];
        for (j = 0; j < blk->transaction_count; j++) {
            const NoTransaction *tx = &blk->transactions[j];
            if (tx->type == NO_TX_COINBASE)
                minted += tx->amount;
            else
                minted -= tx->fee;
        }
    }
    return minted;
}

int no_mint(NoLedger *l, const char *recipient, double amount, const char *memo) {
    NoTransaction tx;
    if (amount <= 0.0) { no_set_error(l, "mint amount must be positive"); return 0; }
    memset(&tx, 0, sizeof(tx));
    tx.type = NO_TX_COINBASE;
    snprintf(tx.sender,    sizeof(tx.sender),    "COINBASE");
    snprintf(tx.recipient, sizeof(tx.recipient), "%s", recipient);
    tx.amount    = amount;
    tx.timestamp = no_now();
    snprintf(tx.memo, sizeof(tx.memo), "%s", memo ? memo : "NewOrder mint");
    no_make_txid(&tx, tx.txid);
    no_apply_confirmed(l, &tx);
    return 1;
}

int no_add_transfer(NoLedger *l, const char *sender, const char *recipient,
                    double amount, double fee, const char *memo) {
    NoTransaction *tx;
    NoAccount *sa;
    if (strncmp(sender,    NO_COIN_SYMBOL, 2) != 0 ||
        strncmp(recipient, NO_COIN_SYMBOL, 2) != 0) {
        no_set_error(l, "sender and recipient must be NO addresses");
        return 0;
    }
    if (amount <= 0.0) { no_set_error(l, "amount must be positive");   return 0; }
    if (fee    <  0.0) { no_set_error(l, "fee cannot be negative");    return 0; }
    sa = no_get_account(l, sender);
    if (!sa) return 0;
    if (sa->balance - sa->pending_debit < amount + fee) {
        no_set_error(l, "insufficient balance");
        return 0;
    }
    if (!no_reserve((void **)&l->mempool, &l->mempool_capacity,
                    l->mempool_count + 1, sizeof(NoTransaction))) {
        no_set_error(l, "out of memory: mempool");
        return 0;
    }
    tx = &l->mempool[l->mempool_count++];
    memset(tx, 0, sizeof(*tx));
    tx->type = NO_TX_TRANSFER;
    snprintf(tx->sender,    sizeof(tx->sender),    "%s", sender);
    snprintf(tx->recipient, sizeof(tx->recipient), "%s", recipient);
    tx->amount    = amount;
    tx->fee       = fee;
    tx->timestamp = no_now();
    snprintf(tx->memo, sizeof(tx->memo), "%s", memo ? memo : "");
    no_make_txid(tx, tx->txid);
    sa->pending_debit += amount + fee;
    return 1;
}

/* ── token operations ─────────────────────────────────────────────────────── */

int no_token_create(NoLedger *l, const char *sender,
                    const char *name, const char *symbol,
                    double total_supply, int decimals, double fee,
                    char token_id_out[NO_ID_LEN]) {
    NoTransaction *tx;
    NoAccount *sa;
    size_t i;

    if (!name || !name[0])   { no_set_error(l, "token name required");          return 0; }
    if (!symbol || !symbol[0]){ no_set_error(l, "token symbol required");       return 0; }
    if (total_supply <= 0.0) { no_set_error(l, "total_supply must be positive"); return 0; }
    if (decimals < 0 || decimals > 18) {
        no_set_error(l, "token decimals must be 0-18"); return 0;
    }
    /* symbol must not clash with native coin */
    if (strcmp(symbol, "NO") == 0) {
        no_set_error(l, "token symbol cannot equal NO"); return 0;
    }
    /* symbol must be alphanumeric and 1-12 chars */
    for (i = 0; symbol[i]; i++) {
        char c = symbol[i];
        if (!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9'))) {
            no_set_error(l, "token symbol must be alphanumeric"); return 0;
        }
    }
    if (i == 0 || i > 12) { no_set_error(l, "token symbol must be 1-12 chars"); return 0; }

    sa = no_get_account(l, sender);
    if (!sa) return 0;
    if (sa->balance - sa->pending_debit < fee) {
        no_set_error(l, "insufficient balance for fee"); return 0;
    }

    if (!no_reserve((void **)&l->mempool, &l->mempool_capacity,
                    l->mempool_count + 1, sizeof(NoTransaction))) {
        no_set_error(l, "out of memory: mempool"); return 0;
    }
    tx = &l->mempool[l->mempool_count++];
    memset(tx, 0, sizeof(*tx));
    tx->type = NO_TX_TOKEN_CREATE;
    snprintf(tx->sender,       sizeof(tx->sender),       "%s", sender);
    snprintf(tx->recipient,    sizeof(tx->recipient),    "TOKEN");
    tx->fee       = fee;
    tx->timestamp = no_now();
    snprintf(tx->token_name,   sizeof(tx->token_name),   "%s", name);
    snprintf(tx->token_symbol, sizeof(tx->token_symbol), "%s", symbol);
    tx->token_total_supply = total_supply;
    tx->token_decimals     = decimals;
    no_make_txid(tx, tx->txid);
    no_make_token_id(tx->txid, token_id_out);
    sa->pending_debit += fee;
    return 1;
}

int no_token_transfer(NoLedger *l, const char *sender,
                      const char *token_id, const char *to,
                      double amount, double fee) {
    NoTransaction *tx;
    NoAccount *sa;
    const NoTokenBalance *tbsrc;
    size_t i;

    if (amount <= 0.0) { no_set_error(l, "token amount must be positive"); return 0; }

    /* verify token exists */
    for (i = 0; i < l->token_count; i++)
        if (strcmp(l->tokens[i].token_id, token_id) == 0) break;
    if (i == l->token_count) { no_set_error(l, "unknown token_id"); return 0; }

    sa = no_get_account(l, sender);
    if (!sa) return 0;
    if (sa->balance - sa->pending_debit < fee) {
        no_set_error(l, "insufficient NO balance for fee"); return 0;
    }
    tbsrc = no_find_tbal_c(l, token_id, sender);
    if (!tbsrc || tbsrc->balance < amount) {
        no_set_error(l, "insufficient token balance"); return 0;
    }

    if (!no_reserve((void **)&l->mempool, &l->mempool_capacity,
                    l->mempool_count + 1, sizeof(NoTransaction))) {
        no_set_error(l, "out of memory: mempool"); return 0;
    }
    tx = &l->mempool[l->mempool_count++];
    memset(tx, 0, sizeof(*tx));
    tx->type = NO_TX_TOKEN_TRANSFER;
    snprintf(tx->sender,    sizeof(tx->sender),    "%s", sender);
    snprintf(tx->recipient, sizeof(tx->recipient), "%s", to);
    tx->fee          = fee;
    tx->timestamp    = no_now();
    snprintf(tx->ref_id,   sizeof(tx->ref_id),   "%s", token_id);
    snprintf(tx->token_to, sizeof(tx->token_to), "%s", to);
    tx->token_amount = amount;
    no_make_txid(tx, tx->txid);
    sa->pending_debit += fee;
    return 1;
}

double no_token_balance_of(const NoLedger *l,
                            const char *token_id, const char *addr) {
    const NoTokenBalance *tb = no_find_tbal_c(l, token_id, addr);
    return tb ? tb->balance : 0.0;
}

const NoToken *no_token_find(const NoLedger *l, const char *token_id) {
    size_t i;
    for (i = 0; i < l->token_count; i++)
        if (strcmp(l->tokens[i].token_id, token_id) == 0) return &l->tokens[i];
    return NULL;
}

/* ── smart contracts ──────────────────────────────────────────────────────── */

int no_contract_deploy(NoLedger *l, const char *sender,
                       const char *name, int is_counter, double fee,
                       char contract_id_out[NO_ID_LEN]) {
    NoTransaction *tx;
    NoAccount *sa;

    if (!name || !name[0]) { no_set_error(l, "contract name required"); return 0; }

    sa = no_get_account(l, sender);
    if (!sa) return 0;
    if (sa->balance - sa->pending_debit < fee) {
        no_set_error(l, "insufficient balance for fee"); return 0;
    }

    if (!no_reserve((void **)&l->mempool, &l->mempool_capacity,
                    l->mempool_count + 1, sizeof(NoTransaction))) {
        no_set_error(l, "out of memory: mempool"); return 0;
    }
    tx = &l->mempool[l->mempool_count++];
    memset(tx, 0, sizeof(*tx));
    tx->type = NO_TX_CONTRACT_DEPLOY;
    snprintf(tx->sender,        sizeof(tx->sender),        "%s", sender);
    snprintf(tx->recipient,     sizeof(tx->recipient),     "CONTRACT");
    tx->fee       = fee;
    tx->timestamp = no_now();
    snprintf(tx->contract_name, sizeof(tx->contract_name), "%s", name);
    tx->contract_is_counter = is_counter;
    no_make_txid(tx, tx->txid);
    no_make_contract_id(tx->txid, contract_id_out);
    sa->pending_debit += fee;
    return 1;
}

int no_contract_call(NoLedger *l, const char *sender,
                     const char *contract_id, const char *method,
                     long long arg_amount, const char *arg_key,
                     const char *arg_value, double fee) {
    NoTransaction *tx;
    NoAccount *sa;
    const NoContract *con = no_contract_find(l, contract_id);

    if (!con) { no_set_error(l, "unknown contract_id"); return 0; }

    /* validate method */
    if (con->is_counter) {
        if (strcmp(method,"increment")!=0 && strcmp(method,"decrement")!=0 &&
            strcmp(method,"reset")!=0) {
            no_set_error(l, "unsupported counter method"); return 0;
        }
    } else {
        if (strcmp(method,"set")!=0 && strcmp(method,"delete")!=0) {
            no_set_error(l, "unsupported key_value method"); return 0;
        }
    }

    sa = no_get_account(l, sender);
    if (!sa) return 0;
    if (sa->balance - sa->pending_debit < fee) {
        no_set_error(l, "insufficient balance for fee"); return 0;
    }

    if (!no_reserve((void **)&l->mempool, &l->mempool_capacity,
                    l->mempool_count + 1, sizeof(NoTransaction))) {
        no_set_error(l, "out of memory: mempool"); return 0;
    }
    tx = &l->mempool[l->mempool_count++];
    memset(tx, 0, sizeof(*tx));
    tx->type = NO_TX_CONTRACT_CALL;
    snprintf(tx->sender,    sizeof(tx->sender),    "%s", sender);
    snprintf(tx->recipient, sizeof(tx->recipient), "%s", contract_id);
    tx->fee       = fee;
    tx->timestamp = no_now();
    snprintf(tx->ref_id,           sizeof(tx->ref_id),           "%s", contract_id);
    snprintf(tx->contract_method,  sizeof(tx->contract_method),  "%s", method);
    tx->contract_arg_amount = arg_amount;
    snprintf(tx->contract_arg_key,   sizeof(tx->contract_arg_key),   "%s", arg_key   ? arg_key   : "");
    snprintf(tx->contract_arg_value, sizeof(tx->contract_arg_value), "%s", arg_value ? arg_value : "");
    no_make_txid(tx, tx->txid);
    sa->pending_debit += fee;
    return 1;
}

const NoContract *no_contract_find(const NoLedger *l, const char *contract_id) {
    size_t i;
    for (i = 0; i < l->contract_count; i++)
        if (strcmp(l->contracts[i].contract_id, contract_id) == 0)
            return &l->contracts[i];
    return NULL;
}

/* ── block production and validation ─────────────────────────────────────── */

int no_validate_block(const NoLedger *l, const NoBlock *blk) {
    double fees = 0.0;
    double coinbase_amount = 0.0;
    int coinbase_count = 0;
    size_t i;
    const NoBlock *prev;
    const char *expected;

    if (l->block_count == 0) return 0;
    prev = &l->blocks[l->block_count - 1];

    if (blk->index != prev->index + 1) return 0;
    if (strcmp(blk->previous_hash, prev->hash) != 0) return 0;

    expected = no_next_validator(l);
    if (expected[0] && strcmp(blk->validator, expected) != 0) return 0;

    for (i = 0; i < blk->transaction_count; i++) {
        const NoTransaction *tx = &blk->transactions[i];
        if (tx->type == NO_TX_COINBASE) {
            coinbase_count++;
            coinbase_amount = tx->amount;
        } else {
            fees += tx->fee;
        }
    }
    if (coinbase_count != 1) return 0;
    if (coinbase_amount > NO_BLOCK_REWARD + fees) return 0;
    return 1;
}

int no_produce_block(NoLedger *l, const char *validator) {
    NoBlock *blk;
    NoTransaction coinbase;
    double fees = 0.0;
    double reward;
    size_t i;

    if (l->block_count == 0) {
        no_set_error(l, "ledger has no genesis block");
        return 0;
    }

    /* check validator turn */
    if (l->validator_count > 0 && !no_can_produce(l, validator)) {
        no_set_error(l, "not this validator's turn");
        return 0;
    }

    for (i = 0; i < l->mempool_count; i++)
        fees += l->mempool[i].fee;

    reward = NO_BLOCK_REWARD + fees;
    /* cap at remaining supply */
    {
        double remaining = NO_MAX_SUPPLY - no_supply(l);
        if (reward > remaining) reward = remaining < 0.0 ? 0.0 : remaining;
    }

    if (!no_reserve((void **)&l->blocks, &l->block_capacity,
                    l->block_count + 1, sizeof(NoBlock))) {
        no_set_error(l, "out of memory: block");
        return 0;
    }
    blk = &l->blocks[l->block_count++];
    memset(blk, 0, sizeof(*blk));
    blk->index         = l->blocks[l->block_count - 2].index + 1;
    blk->timestamp     = no_now();
    snprintf(blk->previous_hash, sizeof(blk->previous_hash), "%s",
             l->blocks[l->block_count - 2].hash);
    snprintf(blk->validator, sizeof(blk->validator), "%s", validator);
    blk->transaction_capacity = l->mempool_count + 1;
    blk->transactions = calloc(blk->transaction_capacity, sizeof(NoTransaction));
    if (!blk->transactions) {
        no_set_error(l, "out of memory: block transactions");
        l->block_count--;
        return 0;
    }

    /* coinbase */
    memset(&coinbase, 0, sizeof(coinbase));
    coinbase.type = NO_TX_COINBASE;
    snprintf(coinbase.sender,    sizeof(coinbase.sender),    "COINBASE");
    snprintf(coinbase.recipient, sizeof(coinbase.recipient), "%s", validator);
    coinbase.amount    = reward;
    coinbase.timestamp = no_now();
    snprintf(coinbase.memo, sizeof(coinbase.memo), "NewOrder validator reward");
    no_make_txid(&coinbase, coinbase.txid);
    blk->transactions[blk->transaction_count++] = coinbase;
    no_apply_confirmed(l, &coinbase);

    /* mempool transactions */
    for (i = 0; i < l->mempool_count; i++) {
        blk->transactions[blk->transaction_count++] = l->mempool[i];
        no_apply_confirmed(l, &l->mempool[i]);
    }
    l->mempool_count = 0;
    no_make_block_hash(blk);
    return 1;
}

/* ── AI payment gateway ───────────────────────────────────────────────────── */

static double no_ai_price(const char *service_id) {
    if (strcmp(service_id, "chat-basic") == 0) return 0.25;
    if (strcmp(service_id, "summary")    == 0) return 0.75;
    return -1.0;
}

static NoAiPayment *no_find_payment(NoLedger *l, const char *payment_id) {
    size_t i;
    for (i = 0; i < l->payment_count; i++)
        if (strcmp(l->payments[i].payment_id, payment_id) == 0)
            return &l->payments[i];
    return NULL;
}

int no_ai_create_payment(NoLedger *l, const char *service_id,
                          const char *customer_address,
                          const char *merchant_address,
                          int units, NoAiPayment *out) {
    NoAiPayment *p;
    char seed[256];
    double price = no_ai_price(service_id);
    if (price < 0.0) { no_set_error(l, "unknown AI service"); return 0; }
    if (units <= 0)  { no_set_error(l, "units must be positive"); return 0; }
    if (!no_reserve((void **)&l->payments, &l->payment_capacity,
                    l->payment_count + 1, sizeof(NoAiPayment))) {
        no_set_error(l, "out of memory: payment"); return 0;
    }
    p = &l->payments[l->payment_count++];
    memset(p, 0, sizeof(*p));
    snprintf(seed, sizeof(seed), "%s:%s:%s:%d:%zu:%llu",
             service_id, customer_address, merchant_address,
             units, l->payment_count, (unsigned long long)no_now());
    {
        char hex[65];
        sha256_hex(seed, strlen(seed), hex);
        snprintf(p->payment_id, sizeof(p->payment_id), "AIP%.38s", hex);
    }
    snprintf(p->service_id,        sizeof(p->service_id),        "%s", service_id);
    snprintf(p->customer_address,  sizeof(p->customer_address),  "%s", customer_address);
    snprintf(p->merchant_address,  sizeof(p->merchant_address),  "%s", merchant_address);
    p->units      = units;
    p->amount_due = price * units;
    snprintf(p->memo, sizeof(p->memo), "AIPAY:%s", p->payment_id);
    if (out) *out = *p;
    return 1;
}

int no_ai_verify_payment(NoLedger *l, const char *payment_id, NoAiPayment *out) {
    NoAiPayment *p = no_find_payment(l, payment_id);
    size_t i, j;
    if (!p) { no_set_error(l, "payment not found"); return 0; }
    if (!p->paid) {
        for (i = 0; i < l->block_count && !p->paid; i++) {
            for (j = 0; j < l->blocks[i].transaction_count; j++) {
                const NoTransaction *tx = &l->blocks[i].transactions[j];
                if (tx->type == NO_TX_TRANSFER &&
                    strcmp(tx->sender,    p->customer_address) == 0 &&
                    strcmp(tx->recipient, p->merchant_address)  == 0 &&
                    strcmp(tx->memo,      p->memo)              == 0 &&
                    tx->amount + 1e-8 >= p->amount_due) {
                    p->paid = 1;
                    snprintf(p->paid_txid, sizeof(p->paid_txid), "%s", tx->txid);
                    break;
                }
            }
        }
    }
    if (out) *out = *p;
    return 1;
}

int no_ai_consume(NoLedger *l, const char *payment_id,
                   const char *prompt, char *response, size_t rsz) {
    NoAiPayment *p;
    char digest[65];
    if (!no_ai_verify_payment(l, payment_id, NULL)) return 0;
    p = no_find_payment(l, payment_id);
    if (!p || !p->paid) { no_set_error(l, "payment is not paid"); return 0; }
    if (p->consumed_units >= p->units) {
        no_set_error(l, "payment has no remaining units"); return 0;
    }
    p->consumed_units++;
    p->consumed = (p->consumed_units >= p->units);
    sha256_hex(prompt ? prompt : "", strlen(prompt ? prompt : ""), digest);
    snprintf(response, rsz, "Processed paid NewOrder C AI request %.16s", digest);
    return 1;
}

const char *no_last_error(const NoLedger *l) {
    return l->last_error[0] ? l->last_error : "no error";
}

/* ── JSON persistence ─────────────────────────────────────────────────────── */

static void write_str(FILE *f, const char *s) {
    /* write a JSON-escaped string (handles the chars we actually emit) */
    fputc('"', f);
    for (; *s; s++) {
        if (*s == '"')       fputs("\\\"", f);
        else if (*s == '\\') fputs("\\\\", f);
        else                 fputc(*s, f);
    }
    fputc('"', f);
}

static void write_tx(FILE *f, const NoTransaction *tx, int last) {
    fprintf(f, "    {");
    fprintf(f, "\"txid\":"); write_str(f, tx->txid); fprintf(f, ",");
    fprintf(f, "\"type\":%d,", (int)tx->type);
    fprintf(f, "\"sender\":"); write_str(f, tx->sender); fprintf(f, ",");
    fprintf(f, "\"recipient\":"); write_str(f, tx->recipient); fprintf(f, ",");
    fprintf(f, "\"amount\":%.8f,\"fee\":%.8f,\"timestamp\":%llu,",
            tx->amount, tx->fee, (unsigned long long)tx->timestamp);
    fprintf(f, "\"memo\":"); write_str(f, tx->memo);
    /* extended fields */
    if (tx->type == NO_TX_TOKEN_CREATE) {
        fprintf(f, ",\"token_name\":"); write_str(f, tx->token_name);
        fprintf(f, ",\"token_symbol\":"); write_str(f, tx->token_symbol);
        fprintf(f, ",\"token_total_supply\":%.8f,\"token_decimals\":%d",
                tx->token_total_supply, tx->token_decimals);
    }
    if (tx->type == NO_TX_TOKEN_TRANSFER) {
        fprintf(f, ",\"ref_id\":"); write_str(f, tx->ref_id);
        fprintf(f, ",\"token_to\":"); write_str(f, tx->token_to);
        fprintf(f, ",\"token_amount\":%.8f", tx->token_amount);
    }
    if (tx->type == NO_TX_CONTRACT_DEPLOY) {
        fprintf(f, ",\"contract_name\":"); write_str(f, tx->contract_name);
        fprintf(f, ",\"contract_is_counter\":%d", tx->contract_is_counter);
    }
    if (tx->type == NO_TX_CONTRACT_CALL) {
        fprintf(f, ",\"ref_id\":"); write_str(f, tx->ref_id);
        fprintf(f, ",\"contract_method\":"); write_str(f, tx->contract_method);
        fprintf(f, ",\"contract_arg_amount\":%lld", (long long)tx->contract_arg_amount);
        fprintf(f, ",\"contract_arg_key\":"); write_str(f, tx->contract_arg_key);
        fprintf(f, ",\"contract_arg_value\":"); write_str(f, tx->contract_arg_value);
    }
    fprintf(f, "}%s\n", last ? "" : ",");
}

int no_save_chain(const NoLedger *l, const char *path) {
    FILE *f = fopen(path, "w");
    size_t i, j;
    if (!f) return 0;
    fprintf(f, "[\n");
    for (i = 0; i < l->block_count; i++) {
        const NoBlock *blk = &l->blocks[i];
        fprintf(f, "  {\"index\":%llu,", (unsigned long long)blk->index);
        fprintf(f, "\"previous_hash\":"); write_str(f, blk->previous_hash);
        fprintf(f, ",\"hash\":"); write_str(f, blk->hash);
        fprintf(f, ",\"validator\":"); write_str(f, blk->validator);
        fprintf(f, ",\"timestamp\":%llu", (unsigned long long)blk->timestamp);
        fprintf(f, ",\"transactions\":[\n");
        for (j = 0; j < blk->transaction_count; j++)
            write_tx(f, &blk->transactions[j], j == blk->transaction_count - 1);
        fprintf(f, "  ]}%s\n", i == l->block_count - 1 ? "" : ",");
    }
    fprintf(f, "]\n");
    fclose(f);
    return 1;
}

/* Minimal JSON field extractor for chain loading */
static const char *jf(const char *p, const char *key) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\":", key);
    p = strstr(p, search);
    if (!p) return NULL;
    p += strlen(search);
    while (*p == ' ') p++;
    return p;
}

static void jget_str(const char *obj, const char *key, char *out, size_t sz) {
    const char *p = jf(obj, key), *e;
    size_t len;
    out[0] = '\0';
    if (!p || *p != '"') return;
    p++;
    e = strchr(p, '"');
    if (!e) return;
    len = (size_t)(e - p);
    if (len >= sz) len = sz - 1;
    memcpy(out, p, len);
    out[len] = '\0';
}

static double jget_dbl(const char *obj, const char *key) {
    const char *p = jf(obj, key);
    return p ? strtod(p, NULL) : 0.0;
}

static long long jget_ll(const char *obj, const char *key) {
    const char *p = jf(obj, key);
    return p ? (long long)strtoll(p, NULL, 10) : 0LL;
}

static int jget_int(const char *obj, const char *key) {
    const char *p = jf(obj, key);
    return p ? atoi(p) : 0;
}

int no_load_chain(NoLedger *l, const char *path) {
    FILE *f = fopen(path, "r");
    char *buf;
    long fsize;
    const char *p, *blk_end;

    if (!f) return 0;
    fseek(f, 0, SEEK_END); fsize = ftell(f); fseek(f, 0, SEEK_SET);
    buf = (char *)malloc((size_t)fsize + 1);
    if (!buf) { fclose(f); return 0; }
    fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    buf[fsize] = '\0';

    /* reset ledger (keep validators) */
    {
        char saved_validators[16][NO_ADDRESS_LEN];
        size_t saved_count = l->validator_count;
        size_t vi;
        for (vi = 0; vi < saved_count; vi++)
            memcpy(saved_validators[vi], l->validator_addrs[vi], NO_ADDRESS_LEN);
        no_ledger_free(l);
        memset(l, 0, sizeof(*l));
        l->validator_count = saved_count;
        for (vi = 0; vi < saved_count; vi++)
            memcpy(l->validator_addrs[vi], saved_validators[vi], NO_ADDRESS_LEN);
    }

    p = buf;
    while ((p = strchr(p, '{')) != NULL) {
        const char *tx_arr;
        const char *tx_p;
        NoBlock *blk;
        char index_str[32];

        /* rough block-level parse: find next top-level { that has "index" */
        if (!jf(p, "index")) { p++; continue; }

        if (!no_reserve((void **)&l->blocks, &l->block_capacity,
                        l->block_count + 1, sizeof(NoBlock))) break;
        blk = &l->blocks[l->block_count++];
        memset(blk, 0, sizeof(*blk));

        jget_str(p, "index", index_str, sizeof(index_str));
        blk->index = (uint64_t)strtoull(index_str, NULL, 10);
        blk->index = (uint64_t)jget_ll(p, "index");
        jget_str(p, "previous_hash", blk->previous_hash, sizeof(blk->previous_hash));
        jget_str(p, "hash",          blk->hash,          sizeof(blk->hash));
        jget_str(p, "validator",     blk->validator,      sizeof(blk->validator));
        blk->timestamp = (uint64_t)jget_ll(p, "timestamp");

        /* parse transactions array */
        tx_arr = jf(p, "transactions");
        if (tx_arr && *tx_arr == '[') {
            tx_p = tx_arr + 1;
            while ((tx_p = strchr(tx_p, '{')) != NULL) {
                const char *tx_end = strchr(tx_p, '}');
                NoTransaction tx;
                char type_str[8];

                if (!tx_end) break;
                /* check this { is for a tx (has "txid") */
                if (!jf(tx_p, "txid")) { tx_p++; continue; }

                memset(&tx, 0, sizeof(tx));
                jget_str(tx_p, "txid",      tx.txid,      sizeof(tx.txid));
                jget_str(tx_p, "type",      type_str,     sizeof(type_str));
                tx.type = (NoTxType)atoi(jf(tx_p, "type") ? jf(tx_p, "type") : "0");
                jget_str(tx_p, "sender",    tx.sender,    sizeof(tx.sender));
                jget_str(tx_p, "recipient", tx.recipient, sizeof(tx.recipient));
                tx.amount    = jget_dbl(tx_p, "amount");
                tx.fee       = jget_dbl(tx_p, "fee");
                tx.timestamp = (uint64_t)jget_ll(tx_p, "timestamp");
                jget_str(tx_p, "memo", tx.memo, sizeof(tx.memo));

                if (tx.type == NO_TX_TOKEN_CREATE) {
                    jget_str(tx_p, "token_name",   tx.token_name,   sizeof(tx.token_name));
                    jget_str(tx_p, "token_symbol", tx.token_symbol, sizeof(tx.token_symbol));
                    tx.token_total_supply = jget_dbl(tx_p, "token_total_supply");
                    tx.token_decimals     = jget_int(tx_p, "token_decimals");
                }
                if (tx.type == NO_TX_TOKEN_TRANSFER) {
                    jget_str(tx_p, "ref_id",    tx.ref_id,    sizeof(tx.ref_id));
                    jget_str(tx_p, "token_to",  tx.token_to,  sizeof(tx.token_to));
                    tx.token_amount = jget_dbl(tx_p, "token_amount");
                }
                if (tx.type == NO_TX_CONTRACT_DEPLOY) {
                    jget_str(tx_p, "contract_name", tx.contract_name, sizeof(tx.contract_name));
                    tx.contract_is_counter = jget_int(tx_p, "contract_is_counter");
                }
                if (tx.type == NO_TX_CONTRACT_CALL) {
                    jget_str(tx_p, "ref_id",           tx.ref_id,           sizeof(tx.ref_id));
                    jget_str(tx_p, "contract_method",  tx.contract_method,  sizeof(tx.contract_method));
                    tx.contract_arg_amount = (long long)jget_ll(tx_p, "contract_arg_amount");
                    jget_str(tx_p, "contract_arg_key",   tx.contract_arg_key,   sizeof(tx.contract_arg_key));
                    jget_str(tx_p, "contract_arg_value", tx.contract_arg_value, sizeof(tx.contract_arg_value));
                }

                /* add tx to block */
                if (!no_reserve((void **)&blk->transactions, &blk->transaction_capacity,
                                blk->transaction_count + 1, sizeof(NoTransaction))) break;
                blk->transactions[blk->transaction_count++] = tx;
                /* replay state */
                no_apply_confirmed(l, &tx);

                /* advance past this tx */
                tx_p = tx_end + 1;

                /* stop if we've reached the closing ] of transactions array */
                if (strchr(tx_p, ']') && strchr(tx_p, ']') < strchr(tx_p, '{'))
                    break;
            }
        }

        /* compute block hash to verify (don't re-hash, trust stored hash) */

        /* advance past this block */
        blk_end = jf(p, "transactions");
        if (blk_end) {
            const char *close = strchr(blk_end, ']');
            p = close ? close + 1 : p + 1;
        } else {
            p++;
        }
    }

    free(buf);
    return 1;
}

int no_save_payments(const NoLedger *l, const char *path) {
    FILE *f = fopen(path, "w");
    size_t i;
    if (!f) return 0;
    fprintf(f, "[\n");
    for (i = 0; i < l->payment_count; i++) {
        const NoAiPayment *p = &l->payments[i];
        fprintf(f, "  {");
        fprintf(f, "\"payment_id\":"); write_str(f, p->payment_id); fprintf(f, ",");
        fprintf(f, "\"service_id\":"); write_str(f, p->service_id); fprintf(f, ",");
        fprintf(f, "\"customer_address\":"); write_str(f, p->customer_address); fprintf(f, ",");
        fprintf(f, "\"merchant_address\":"); write_str(f, p->merchant_address); fprintf(f, ",");
        fprintf(f, "\"units\":%d,\"consumed_units\":%d,\"amount_due\":%.8f,",
                p->units, p->consumed_units, p->amount_due);
        fprintf(f, "\"memo\":"); write_str(f, p->memo); fprintf(f, ",");
        fprintf(f, "\"paid\":%d,\"consumed\":%d,", p->paid, p->consumed);
        fprintf(f, "\"paid_txid\":"); write_str(f, p->paid_txid);
        fprintf(f, "}%s\n", i == l->payment_count - 1 ? "" : ",");
    }
    fprintf(f, "]\n");
    fclose(f);
    return 1;
}

int no_load_payments(NoLedger *l, const char *path) {
    FILE *f = fopen(path, "r");
    char *buf; long fsize;
    const char *p;
    if (!f) return 1; /* not an error if no file yet */
    fseek(f, 0, SEEK_END); fsize = ftell(f); fseek(f, 0, SEEK_SET);
    buf = (char *)malloc((size_t)fsize + 1);
    if (!buf) { fclose(f); return 0; }
    fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    buf[fsize] = '\0';
    p = buf;
    while ((p = strchr(p, '{')) != NULL) {
        NoAiPayment pay;
        if (!jf(p, "payment_id")) { p++; continue; }
        memset(&pay, 0, sizeof(pay));
        jget_str(p, "payment_id",       pay.payment_id,       sizeof(pay.payment_id));
        jget_str(p, "service_id",       pay.service_id,       sizeof(pay.service_id));
        jget_str(p, "customer_address", pay.customer_address, sizeof(pay.customer_address));
        jget_str(p, "merchant_address", pay.merchant_address, sizeof(pay.merchant_address));
        pay.units          = jget_int(p, "units");
        pay.consumed_units = jget_int(p, "consumed_units");
        pay.amount_due     = jget_dbl(p, "amount_due");
        jget_str(p, "memo",      pay.memo,      sizeof(pay.memo));
        pay.paid     = jget_int(p, "paid");
        pay.consumed = jget_int(p, "consumed");
        jget_str(p, "paid_txid", pay.paid_txid, sizeof(pay.paid_txid));
        if (!no_reserve((void **)&l->payments, &l->payment_capacity,
                        l->payment_count + 1, sizeof(NoAiPayment))) break;
        l->payments[l->payment_count++] = pay;
        p++;
    }
    free(buf);
    return 1;
}
