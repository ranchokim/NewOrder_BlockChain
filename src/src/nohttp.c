/*
 * NewOrder C HTTP node server — minimal HTTP/1.0 server implementing all
 * Explorer API routes.
 *
 * Crypto note: signature verification is not performed by this C node.
 * All submitted transactions are accepted if balance constraints are met.
 */
#define _POSIX_C_SOURCE 200809L

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "nohttp.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef int sock_t;
#define SOCK_INVALID     -1
#define sock_close(s)    close(s)
#define sock_read(s,b,n) read((s),(b),(n))
static void sock_write(sock_t s, const void *buf, size_t n) {
    ssize_t r = write(s, buf, n); (void)r;
}
static void net_init(void) {}

/* ── JSON builder ─────────────────────────────────────────────────────────── */

typedef struct { char *buf; size_t pos; size_t cap; } JB;

static void jb_raw(JB *j, const char *s) {
    size_t n = strlen(s);
    if (j->pos + n < j->cap) { memcpy(j->buf + j->pos, s, n); j->pos += n; }
}
static void jb_char(JB *j, char c) {
    if (j->pos + 1 < j->cap) { j->buf[j->pos++] = c; }
}
static void jb_str(JB *j, const char *s) {
    jb_char(j, '"');
    for (; s && *s; s++) {
        if (*s == '"')  { jb_char(j,'\\'); jb_char(j,'"');  }
        else if (*s=='\\') { jb_char(j,'\\'); jb_char(j,'\\'); }
        else            jb_char(j, *s);
    }
    jb_char(j, '"');
}
static void jb_key(JB *j, const char *k, int comma) {
    if (comma) jb_char(j, ',');
    jb_str(j, k); jb_char(j, ':');
}
static void jb_kstr(JB *j, const char *k, const char *v, int c) {
    jb_key(j,k,c); jb_str(j,v);
}
static void jb_kint(JB *j, const char *k, long long v, int c) {
    char tmp[32]; snprintf(tmp,sizeof(tmp),"%lld",v);
    jb_key(j,k,c); jb_raw(j,tmp);
}
static void jb_kdbl(JB *j, const char *k, double v, int c) {
    char tmp[32]; snprintf(tmp,sizeof(tmp),"%.8f",v);
    jb_key(j,k,c); jb_raw(j,tmp);
}
static void jb_kbool(JB *j, const char *k, int v, int c) {
    jb_key(j,k,c); jb_raw(j, v ? "true" : "false");
}
static void jb_finish(JB *j) {
    if (j->pos < j->cap) j->buf[j->pos] = '\0';
}

/* ── JSON request field extractor ─────────────────────────────────────────── */

static const char *jfind(const char *body, const char *key) {
    char search[128];
    const char *p;
    snprintf(search, sizeof(search), "\"%s\"", key);
    p = strstr(body ? body : "", search);
    if (!p) return NULL;
    p += strlen(search);
    while (*p == ' ' || *p == ':') p++;
    return p;
}
static void jget_str(const char *body, const char *key, char *out, size_t sz) {
    const char *p = jfind(body, key), *e;
    size_t len;
    out[0] = '\0';
    if (!p || *p != '"') return;
    p++;
    e = strchr(p, '"');
    if (!e) return;
    len = (size_t)(e - p);
    if (len >= sz) len = sz - 1;
    memcpy(out, p, len); out[len] = '\0';
}
static double jget_dbl(const char *body, const char *key, double def) {
    const char *p = jfind(body, key);
    return (p && *p != '\0' && *p != '"' && *p != '[' && *p != '{') ? strtod(p, NULL) : def;
}
static int jget_int(const char *body, const char *key, int def) {
    const char *p = jfind(body, key);
    return (p && *p != '\0' && *p != '"') ? atoi(p) : def;
}

/* ── HTTP I/O ─────────────────────────────────────────────────────────────── */

static void http_send(sock_t s, int status, const char *body) {
    const char *stxt;
    char hdr[256];
    size_t blen = body ? strlen(body) : 0;
    switch (status) {
    case 200: stxt = "OK";          break;
    case 201: stxt = "Created";     break;
    case 400: stxt = "Bad Request"; break;
    case 404: stxt = "Not Found";   break;
    case 409: stxt = "Conflict";    break;
    default:  stxt = "Error";       break;
    }
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.0 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n",
        status, stxt, blen);
    sock_write(s, hdr, strlen(hdr));
    if (body) sock_write(s, body, blen);
}

/* ── transaction JSON builder ─────────────────────────────────────────────── */

static void jb_tx(JB *j, const NoTransaction *tx, int comma) {
    if (comma) jb_char(j, ',');
    jb_char(j, '{');
    jb_kstr(j, "txid",      tx->txid,      0);
    jb_kint(j, "type",      (long long)tx->type, 1);
    jb_kstr(j, "sender",    tx->sender,    1);
    jb_kstr(j, "recipient", tx->recipient, 1);
    jb_kdbl(j, "amount",    tx->amount,    1);
    jb_kdbl(j, "fee",       tx->fee,       1);
    jb_kint(j, "timestamp", (long long)tx->timestamp, 1);
    jb_kstr(j, "memo",      tx->memo,      1);
    if (tx->type == NO_TX_TOKEN_CREATE) {
        jb_kstr(j, "token_name",   tx->token_name,   1);
        jb_kstr(j, "token_symbol", tx->token_symbol, 1);
        jb_kdbl(j, "token_total_supply", tx->token_total_supply, 1);
        jb_kint(j, "token_decimals",     (long long)tx->token_decimals, 1);
    }
    if (tx->type == NO_TX_TOKEN_TRANSFER) {
        jb_kstr(j, "ref_id",       tx->ref_id,       1);
        jb_kstr(j, "token_to",     tx->token_to,     1);
        jb_kdbl(j, "token_amount", tx->token_amount,  1);
    }
    if (tx->type == NO_TX_CONTRACT_DEPLOY) {
        jb_kstr(j, "contract_name",       tx->contract_name,       1);
        jb_kbool(j,"contract_is_counter", tx->contract_is_counter, 1);
    }
    if (tx->type == NO_TX_CONTRACT_CALL) {
        jb_kstr(j, "ref_id",              tx->ref_id,              1);
        jb_kstr(j, "contract_method",     tx->contract_method,     1);
        jb_kint(j, "contract_arg_amount", (long long)tx->contract_arg_amount, 1);
        jb_kstr(j, "contract_arg_key",    tx->contract_arg_key,    1);
        jb_kstr(j, "contract_arg_value",  tx->contract_arg_value,  1);
    }
    jb_char(j, '}');
}

static void jb_block(JB *j, const NoBlock *blk, int comma) {
    size_t i;
    if (comma) jb_char(j, ',');
    jb_char(j, '{');
    jb_kint(j, "index",         (long long)blk->index,     0);
    jb_kstr(j, "previous_hash", blk->previous_hash,        1);
    jb_kstr(j, "hash",          blk->hash,                 1);
    jb_kstr(j, "validator",     blk->validator,            1);
    jb_kint(j, "timestamp",     (long long)blk->timestamp, 1);
    jb_key(j, "transactions", 1); jb_char(j,'[');
    for (i = 0; i < blk->transaction_count; i++)
        jb_tx(j, &blk->transactions[i], i > 0);
    jb_char(j, ']');
    jb_char(j, '}');
}

/* ── route handlers ───────────────────────────────────────────────────────── */

static void route_stats(sock_t s, NoNodeConfig *cfg) {
    char buf[4096]; JB j = {buf,0,sizeof(buf)};
    const NoLedger *l = cfg->ledger;
    const NoBlock  *lb = &l->blocks[l->block_count - 1];
    size_t i;
    jb_char(&j, '{');
    jb_kstr(&j, "coin",           "NewOrder",                   0);
    jb_kstr(&j, "symbol",         "NO",                         1);
    jb_kint(&j, "height",         (long long)(lb->index),       1);
    jb_kstr(&j, "latest_hash",    lb->hash,                     1);
    jb_kstr(&j, "consensus",      "poa-round-robin",             1);
    jb_kstr(&j, "next_validator", no_next_validator(l),         1);
    jb_kint(&j, "mempool_size",   (long long)l->mempool_count,  1);
    jb_kdbl(&j, "supply",         no_supply(l),                 1);
    jb_kint(&j, "token_count",    (long long)l->token_count,    1);
    jb_kint(&j, "contract_count", (long long)l->contract_count, 1);
    jb_key(&j, "validators", 1); jb_char(&j,'[');
    for (i = 0; i < l->validator_count; i++) {
        if (i) jb_char(&j,',');
        jb_str(&j, l->validator_addrs[i]);
    }
    jb_char(&j,']');
    jb_char(&j, '}');
    jb_finish(&j);
    http_send(s, 200, buf);
}

static void route_chain(sock_t s, NoNodeConfig *cfg) {
    /* potentially large — use heap buffer */
    size_t sz = 1 << 20; /* 1 MB */
    char *buf = (char *)malloc(sz);
    JB j; size_t i;
    if (!buf) { http_send(s, 500, "{\"error\":\"oom\"}"); return; }
    j.buf = buf; j.pos = 0; j.cap = sz;
    jb_char(&j, '[');
    for (i = 0; i < cfg->ledger->block_count; i++)
        jb_block(&j, &cfg->ledger->blocks[i], i > 0);
    jb_char(&j, ']');
    jb_finish(&j);
    http_send(s, 200, buf);
    free(buf);
}

static void route_block(sock_t s, NoNodeConfig *cfg, const char *val) {
    char buf[65536]; JB j = {buf,0,sizeof(buf)};
    const NoLedger *l = cfg->ledger;
    const NoBlock  *blk = NULL;
    size_t i;
    /* numeric index or hash */
    if (val[0] >= '0' && val[0] <= '9') {
        uint64_t idx = (uint64_t)strtoull(val, NULL, 10);
        for (i = 0; i < l->block_count; i++)
            if (l->blocks[i].index == idx) { blk = &l->blocks[i]; break; }
    } else {
        for (i = 0; i < l->block_count; i++)
            if (strcmp(l->blocks[i].hash, val) == 0) { blk = &l->blocks[i]; break; }
    }
    if (!blk) { http_send(s, 404, "{\"error\":\"block not found\"}"); return; }
    jb_block(&j, blk, 0);
    jb_finish(&j);
    http_send(s, 200, buf);
}

static void route_tx(sock_t s, NoNodeConfig *cfg, const char *txid) {
    char buf[16384]; JB j = {buf,0,sizeof(buf)};
    const NoLedger *l = cfg->ledger;
    size_t i, k;
    /* search blocks */
    for (i = 0; i < l->block_count; i++) {
        for (k = 0; k < l->blocks[i].transaction_count; k++) {
            const NoTransaction *tx = &l->blocks[i].transactions[k];
            if (strcmp(tx->txid, txid) == 0) {
                jb_char(&j, '{');
                jb_kint(&j, "block", (long long)l->blocks[i].index, 0);
                jb_key(&j, "transaction", 1); jb_tx(&j, tx, 0);
                jb_char(&j, '}'); jb_finish(&j);
                http_send(s, 200, buf); return;
            }
        }
    }
    /* search mempool */
    for (k = 0; k < l->mempool_count; k++) {
        const NoTransaction *tx = &l->mempool[k];
        if (strcmp(tx->txid, txid) == 0) {
            jb_char(&j, '{');
            jb_raw(&j, "\"block\":null,");
            jb_key(&j, "transaction", 0); jb_tx(&j, tx, 0);
            jb_char(&j, '}'); jb_finish(&j);
            http_send(s, 200, buf); return;
        }
    }
    http_send(s, 404, "{\"error\":\"transaction not found\"}");
}

static void route_address(sock_t s, NoNodeConfig *cfg, const char *addr) {
    char buf[4096]; JB j = {buf,0,sizeof(buf)};
    const NoLedger *l = cfg->ledger;
    size_t i;
    jb_char(&j, '{');
    jb_kstr(&j, "address", addr, 0);
    jb_kdbl(&j, "balance", no_balance_of(l, addr), 1);
    jb_key(&j, "tokens", 1); jb_char(&j, '{');
    {
        int first = 1;
        for (i = 0; i < l->token_balance_count; i++) {
            const NoTokenBalance *tb = &l->token_balances[i];
            if (strcmp(tb->address, addr) == 0 && tb->balance != 0.0) {
                jb_kstr(&j, tb->token_id, "", !first);  /* key */
                /* replace the empty string value with the actual number */
                j.pos -= 2; /* remove the two "" chars */
                { char tmp[32]; snprintf(tmp,sizeof(tmp),"%.8f",tb->balance);
                  jb_raw(&j,tmp); }
                first = 0;
            }
        }
    }
    jb_char(&j, '}'); jb_char(&j, '}');
    jb_finish(&j);
    http_send(s, 200, buf);
}

static void route_mempool(sock_t s, NoNodeConfig *cfg) {
    char *buf = (char *)malloc(1 << 20); JB j; size_t i;
    if (!buf) { http_send(s, 500, "{\"error\":\"oom\"}"); return; }
    j.buf = buf; j.pos = 0; j.cap = 1<<20;
    jb_char(&j, '[');
    for (i = 0; i < cfg->ledger->mempool_count; i++)
        jb_tx(&j, &cfg->ledger->mempool[i], i > 0);
    jb_char(&j, ']'); jb_finish(&j);
    http_send(s, 200, buf); free(buf);
}

static void route_tokens(sock_t s, NoNodeConfig *cfg) {
    char buf[65536]; JB j = {buf,0,sizeof(buf)};
    const NoLedger *l = cfg->ledger; size_t i;
    jb_char(&j, '[');
    for (i = 0; i < l->token_count; i++) {
        const NoToken *t = &l->tokens[i];
        if (i) jb_char(&j, ',');
        jb_char(&j, '{');
        jb_kstr(&j, "token_id", t->token_id, 0);
        jb_kstr(&j, "name",     t->name,     1);
        jb_kstr(&j, "symbol",   t->symbol,   1);
        jb_kstr(&j, "issuer",   t->issuer,   1);
        jb_kdbl(&j, "total_supply", t->total_supply, 1);
        jb_kint(&j, "decimals",      (long long)t->decimals, 1);
        jb_kint(&j, "created_at_block", (long long)t->created_at_block, 1);
        jb_kstr(&j, "create_txid", t->create_txid, 1);
        jb_char(&j, '}');
    }
    jb_char(&j, ']'); jb_finish(&j);
    http_send(s, 200, buf);
}

static void route_token(sock_t s, NoNodeConfig *cfg, const char *token_id) {
    char buf[2048]; JB j = {buf,0,sizeof(buf)};
    const NoToken *t = no_token_find(cfg->ledger, token_id);
    if (!t) { http_send(s, 404, "{\"error\":\"token not found\"}"); return; }
    jb_char(&j, '{');
    jb_kstr(&j, "token_id", t->token_id, 0);
    jb_kstr(&j, "name",     t->name,     1);
    jb_kstr(&j, "symbol",   t->symbol,   1);
    jb_kstr(&j, "issuer",   t->issuer,   1);
    jb_kdbl(&j, "total_supply", t->total_supply, 1);
    jb_kint(&j, "decimals",  (long long)t->decimals,          1);
    jb_kint(&j, "created_at_block", (long long)t->created_at_block, 1);
    jb_kstr(&j, "create_txid", t->create_txid, 1);
    jb_char(&j, '}'); jb_finish(&j);
    http_send(s, 200, buf);
}

static void route_token_balance(sock_t s, NoNodeConfig *cfg,
                                 const char *token_id, const char *addr) {
    char buf[256]; JB j = {buf,0,sizeof(buf)};
    if (!no_token_find(cfg->ledger, token_id)) {
        http_send(s, 404, "{\"error\":\"token not found\"}"); return;
    }
    jb_char(&j, '{');
    jb_kstr(&j, "token_id", token_id, 0);
    jb_kstr(&j, "address",  addr,     1);
    jb_kdbl(&j, "balance",  no_token_balance_of(cfg->ledger, token_id, addr), 1);
    jb_char(&j, '}'); jb_finish(&j);
    http_send(s, 200, buf);
}

static void route_contracts(sock_t s, NoNodeConfig *cfg) {
    char buf[65536]; JB j = {buf,0,sizeof(buf)};
    const NoLedger *l = cfg->ledger; size_t i;
    jb_char(&j, '[');
    for (i = 0; i < l->contract_count; i++) {
        const NoContract *c = &l->contracts[i];
        if (i) jb_char(&j, ',');
        jb_char(&j, '{');
        jb_kstr(&j, "contract_id",      c->contract_id, 0);
        jb_kstr(&j, "name",             c->name,        1);
        jb_kstr(&j, "owner",            c->owner,       1);
        jb_kstr(&j, "contract_type",    c->is_counter ? "counter" : "key_value", 1);
        jb_kint(&j, "created_at_block", (long long)c->created_at_block, 1);
        jb_kstr(&j, "deploy_txid",      c->deploy_txid, 1);
        jb_char(&j, '}');
    }
    jb_char(&j, ']'); jb_finish(&j);
    http_send(s, 200, buf);
}

static void route_contract(sock_t s, NoNodeConfig *cfg, const char *cid) {
    char buf[32768]; JB j = {buf,0,sizeof(buf)};
    const NoContract *c = no_contract_find(cfg->ledger, cid);
    size_t i;
    if (!c) { http_send(s, 404, "{\"error\":\"contract not found\"}"); return; }
    jb_char(&j, '{');
    jb_key(&j, "contract", 0); jb_char(&j, '{');
    jb_kstr(&j, "contract_id",      c->contract_id, 0);
    jb_kstr(&j, "name",             c->name,        1);
    jb_kstr(&j, "owner",            c->owner,       1);
    jb_kstr(&j, "contract_type",    c->is_counter ? "counter" : "key_value", 1);
    jb_kint(&j, "created_at_block", (long long)c->created_at_block, 1);
    jb_char(&j, '}');
    jb_key(&j, "state", 1); jb_char(&j, '{');
    if (c->is_counter) {
        jb_kint(&j, "value", (long long)c->counter_value, 0);
    } else {
        jb_key(&j, "values", 0); jb_char(&j, '{');
        for (i = 0; i < c->kv_count; i++) {
            jb_kstr(&j, c->kv[i].key, c->kv[i].value, i > 0);
        }
        jb_char(&j, '}');
    }
    jb_char(&j, '}');
    jb_char(&j, '}'); jb_finish(&j);
    http_send(s, 200, buf);
}

static void route_ai_services(sock_t s) {
    const char *resp =
        "[{\"service_id\":\"chat-basic\",\"name\":\"Basic AI Chat\","
        "\"price_per_unit\":0.25000000,\"unit_name\":\"prompt\","
        "\"description\":\"Deterministic local chat response for demos and tests.\"},"
        "{\"service_id\":\"summary\",\"name\":\"Document Summary\","
        "\"price_per_unit\":0.75000000,\"unit_name\":\"document\","
        "\"description\":\"Short deterministic summary response for submitted text.\"}]";
    http_send(s, 200, resp);
}

static void jb_payment(JB *j, const NoAiPayment *p) {
    jb_char(j, '{');
    jb_kstr(j, "payment_id",       p->payment_id,       0);
    jb_kstr(j, "service_id",       p->service_id,       1);
    jb_kstr(j, "customer_address", p->customer_address, 1);
    jb_kstr(j, "merchant_address", p->merchant_address, 1);
    jb_kint(j, "units",            (long long)p->units,          1);
    jb_kint(j, "consumed_units",   (long long)p->consumed_units, 1);
    jb_kdbl(j, "amount_due",       p->amount_due, 1);
    jb_kstr(j, "memo",             p->memo, 1);
    jb_kstr(j, "status",
            p->consumed ? "consumed" : (p->paid ? "paid" : "pending"), 1);
    jb_kstr(j, "paid_txid",        p->paid_txid, 1);
    jb_char(j, '}');
}

static void route_ai_payment_get(sock_t s, NoNodeConfig *cfg,
                                  const char *payment_id) {
    char buf[2048]; JB j = {buf,0,sizeof(buf)};
    NoAiPayment pay;
    /* run verify first to update status */
    if (!no_ai_verify_payment(cfg->ledger, payment_id, &pay)) {
        http_send(s, 404, "{\"error\":\"payment not found\"}"); return;
    }
    jb_payment(&j, &pay); jb_finish(&j);
    http_send(s, 200, buf);
}

static void route_mine(sock_t s, NoNodeConfig *cfg, const char *addr) {
    char buf[65536]; JB j = {buf,0,sizeof(buf)};
    const char *validator = cfg->validator_address;
    if (!validator || !validator[0]) {
        http_send(s, 400, "{\"error\":\"node has no validator address configured\"}");
        return;
    }
    if (!no_produce_block(cfg->ledger, validator)) {
        char errbuf[256];
        snprintf(errbuf, sizeof(errbuf), "{\"error\":\"%s\",\"next_validator\":\"%s\"}",
                 no_last_error(cfg->ledger), no_next_validator(cfg->ledger));
        http_send(s, 409, errbuf); return;
    }
    /* save chain after block */
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/chain.json", cfg->data_dir);
        no_save_chain(cfg->ledger, path);
    }
    (void)addr; /* reward address ignored in C port — validator gets reward */
    {
        const NoBlock *blk = &cfg->ledger->blocks[cfg->ledger->block_count - 1];
        jb_char(&j, '{');
        jb_key(&j, "block", 0); jb_block(&j, blk, 0);
        jb_char(&j, '}'); jb_finish(&j);
    }
    http_send(s, 200, buf);
}

/* ── POST handlers ────────────────────────────────────────────────────────── */

static void route_post_transaction(sock_t s, NoNodeConfig *cfg,
                                    const char *body) {
    NoLedger *l = cfg->ledger;
    char sender[NO_ADDRESS_LEN], recipient[NO_ADDRESS_LEN];
    char memo[NO_MEMO_LEN], type_str[32], ref_id[NO_ID_LEN];
    char token_to[NO_ADDRESS_LEN], token_name[NO_TOKEN_NAME_LEN];
    char token_symbol[NO_TOKEN_SYM_LEN], contract_name[NO_CONTRACT_NAME_LEN];
    char contract_method[NO_METHOD_LEN], arg_key[NO_KV_KEY_LEN];
    char arg_value[NO_KV_VAL_LEN];
    double amount, fee, token_total_supply, token_amount;
    int tx_type_int, token_decimals, contract_is_counter;
    long long contract_arg_amount;
    int ok; char txid[NO_ID_LEN]; char errbuf[256];

    jget_str(body, "tx_type",   type_str,  sizeof(type_str));
    jget_str(body, "sender",    sender,    sizeof(sender));
    jget_str(body, "recipient", recipient, sizeof(recipient));
    amount  = jget_dbl(body, "amount",  0.0);
    fee     = jget_dbl(body, "fee",     0.0);
    jget_str(body, "memo", memo, sizeof(memo));

    /* map type string to int */
    if      (strcmp(type_str, "transfer")        == 0) tx_type_int = 1;
    else if (strcmp(type_str, "token_create")    == 0) tx_type_int = 2;
    else if (strcmp(type_str, "token_transfer")  == 0) tx_type_int = 3;
    else if (strcmp(type_str, "contract_deploy") == 0) tx_type_int = 4;
    else if (strcmp(type_str, "contract_call")   == 0) tx_type_int = 5;
    else                                               tx_type_int = 1;

    ok = 0;
    switch (tx_type_int) {
    case 1: /* transfer */
        ok = no_add_transfer(l, sender, recipient, amount, fee, memo);
        if (ok) {
            /* get the txid of the just-added transaction */
            snprintf(txid, sizeof(txid), "%s",
                     l->mempool[l->mempool_count - 1].txid);
        }
        break;
    case 2: /* token_create */
        jget_str(body, "token_name",   token_name,   sizeof(token_name));
        jget_str(body, "token_symbol", token_symbol, sizeof(token_symbol));
        token_total_supply = jget_dbl(body, "token_total_supply", 0.0);
        token_decimals     = jget_int(body, "token_decimals",     0);
        ok = no_token_create(l, sender, token_name, token_symbol,
                              token_total_supply, token_decimals, fee, txid);
        break;
    case 3: /* token_transfer */
        jget_str(body, "ref_id",      ref_id,   sizeof(ref_id));
        jget_str(body, "token_to",    token_to, sizeof(token_to));
        token_amount = jget_dbl(body, "token_amount", 0.0);
        ok = no_token_transfer(l, sender, ref_id, token_to, token_amount, fee);
        if (ok) snprintf(txid, sizeof(txid), "%s",
                          l->mempool[l->mempool_count - 1].txid);
        break;
    case 4: /* contract_deploy */
        jget_str(body, "contract_name", contract_name, sizeof(contract_name));
        contract_is_counter = jget_int(body, "contract_is_counter", 1);
        ok = no_contract_deploy(l, sender, contract_name,
                                 contract_is_counter, fee, txid);
        break;
    case 5: /* contract_call */
        jget_str(body, "ref_id",              ref_id,          sizeof(ref_id));
        jget_str(body, "contract_method",     contract_method, sizeof(contract_method));
        contract_arg_amount = (long long)jget_dbl(body, "contract_arg_amount", 0.0);
        jget_str(body, "contract_arg_key",   arg_key,   sizeof(arg_key));
        jget_str(body, "contract_arg_value", arg_value, sizeof(arg_value));
        ok = no_contract_call(l, sender, ref_id, contract_method,
                               contract_arg_amount, arg_key, arg_value, fee);
        if (ok) snprintf(txid, sizeof(txid), "%s",
                          l->mempool[l->mempool_count - 1].txid);
        break;
    }

    if (ok) {
        char resp[256];
        snprintf(resp, sizeof(resp), "{\"txid\":\"%s\"}", txid);
        http_send(s, 201, resp);
    } else {
        snprintf(errbuf, sizeof(errbuf), "{\"error\":\"%s\"}", no_last_error(l));
        http_send(s, 400, errbuf);
    }
}

static void route_post_ai_payment(sock_t s, NoNodeConfig *cfg, const char *body) {
    char service_id[NO_SERVICE_LEN], customer[NO_ADDRESS_LEN];
    int units;
    NoAiPayment pay;
    char buf[2048]; JB j = {buf,0,sizeof(buf)};
    char errbuf[256];

    jget_str(body, "service_id",       service_id, sizeof(service_id));
    jget_str(body, "customer_address", customer,   sizeof(customer));
    units = jget_int(body, "units", 1);

    if (!no_ai_create_payment(cfg->ledger, service_id, customer,
                               cfg->ai_merchant_address, units, &pay)) {
        snprintf(errbuf, sizeof(errbuf), "{\"error\":\"%s\"}",
                 no_last_error(cfg->ledger));
        http_send(s, 400, errbuf); return;
    }
    /* persist */
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/ai_payments.json", cfg->data_dir);
        no_save_payments(cfg->ledger, path);
    }
    jb_payment(&j, &pay); jb_finish(&j);
    http_send(s, 201, buf);
}

static void route_post_ai_verify(sock_t s, NoNodeConfig *cfg,
                                   const char *payment_id) {
    char buf[2048]; JB j = {buf,0,sizeof(buf)};
    char errbuf[256];
    NoAiPayment pay;
    if (!no_ai_verify_payment(cfg->ledger, payment_id, &pay)) {
        snprintf(errbuf, sizeof(errbuf), "{\"error\":\"%s\"}",
                 no_last_error(cfg->ledger));
        http_send(s, 404, errbuf); return;
    }
    /* persist updated status */
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/ai_payments.json", cfg->data_dir);
        no_save_payments(cfg->ledger, path);
    }
    jb_payment(&j, &pay); jb_finish(&j);
    http_send(s, 200, buf);
}

static void route_post_ai_consume(sock_t s, NoNodeConfig *cfg,
                                    const char *payment_id, const char *body) {
    char prompt[512], response[512];
    char buf[4096]; JB j = {buf,0,sizeof(buf)};
    char errbuf[256];
    NoAiPayment pay;
    jget_str(body, "prompt", prompt, sizeof(prompt));

    if (!no_ai_consume(cfg->ledger, payment_id, prompt,
                        response, sizeof(response))) {
        snprintf(errbuf, sizeof(errbuf), "{\"error\":\"%s\"}",
                 no_last_error(cfg->ledger));
        http_send(s, 400, errbuf); return;
    }
    no_ai_verify_payment(cfg->ledger, payment_id, &pay);
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/ai_payments.json", cfg->data_dir);
        no_save_payments(cfg->ledger, path);
    }
    jb_char(&j, '{');
    jb_key(&j, "payment", 0); jb_payment(&j, &pay);
    jb_kstr(&j, "result", response, 1);
    jb_char(&j, '}'); jb_finish(&j);
    http_send(s, 200, buf);
}

/* ── request dispatcher ───────────────────────────────────────────────────── */

typedef struct {
    char method[8];
    char path[512];
    char query[256];
    char body[65536];
    size_t body_len;
} HttpReq;

static int http_parse(sock_t s, HttpReq *req) {
    char buf[8192]; char line[1024];
    int content_length = 0;
    size_t buf_pos = 0, buf_len = 0;
    int header_done = 0;
    char *p;

    /* read header */
    memset(req, 0, sizeof(*req));
    while (!header_done) {
        int n;
        if (buf_pos >= sizeof(buf) - 1) break;
        n = (int)sock_read(s, buf + buf_len, sizeof(buf) - 1 - buf_len);
        if (n <= 0) return 0;
        buf_len += (size_t)n;
        buf[buf_len] = '\0';
        if (strstr(buf, "\r\n\r\n")) header_done = 1;
    }
    buf[buf_len] = '\0';

    /* parse first line */
    p = strchr(buf, '\r');
    if (!p) return 0;
    *p = '\0';
    strncpy(line, buf, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    {
        char *sp1 = strchr(line, ' ');
        char *sp2 = sp1 ? strchr(sp1+1, ' ') : NULL;
        if (!sp1) return 0;
        *sp1 = '\0';
        strncpy(req->method, line, sizeof(req->method) - 1);
        req->method[sizeof(req->method) - 1] = '\0';
        if (sp2) {
            *sp2 = '\0';
            /* split path and query */
            char *q = strchr(sp1+1, '?');
            if (q) {
                *q = '\0';
                snprintf(req->query, sizeof(req->query), "%s", q+1);
            }
            snprintf(req->path, sizeof(req->path), "%s", sp1+1);
        }
    }
    *p = '\r'; /* restore */

    /* find Content-Length */
    {
        const char *cl = strstr(buf, "Content-Length:");
        if (!cl) cl = strstr(buf, "content-length:");
        if (cl) content_length = atoi(cl + 15);
    }

    /* read body */
    if (content_length > 0) {
        const char *body_start = strstr(buf, "\r\n\r\n");
        int already;
        if (!body_start) return 0;
        body_start += 4;
        already = (int)(buf_len - (size_t)(body_start - buf));
        if (already > 0) {
            int take = already < content_length ? already : content_length;
            memcpy(req->body, body_start, (size_t)take);
            req->body_len = (size_t)take;
        }
        while ((int)req->body_len < content_length &&
               req->body_len < sizeof(req->body) - 1) {
            int n = (int)sock_read(s, req->body + req->body_len,
                                    (size_t)content_length - req->body_len);
            if (n <= 0) break;
            req->body_len += (size_t)n;
        }
        req->body[req->body_len] = '\0';
    }
    return 1;
}

/* split path "/a/b/c" into parts[3] */
static int path_parts(const char *path, char parts[8][128]) {
    int n = 0;
    const char *p = path;
    while (*p == '/') p++;
    while (*p && n < 8) {
        const char *e = strchr(p, '/');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len >= 128) len = 127;
        memcpy(parts[n], p, len);
        parts[n][len] = '\0';
        n++;
        if (!e) break;
        p = e + 1;
    }
    return n;
}

/* get query param value */
static void qget(const char *query, const char *key, char *out, size_t sz) {
    char search[128]; const char *p; const char *e;
    size_t len;
    snprintf(search, sizeof(search), "%s=", key);
    out[0] = '\0';
    p = strstr(query, search);
    if (!p) return;
    p += strlen(search);
    e = strchr(p, '&');
    len = e ? (size_t)(e - p) : strlen(p);
    if (len >= sz) len = sz - 1;
    memcpy(out, p, len); out[len] = '\0';
}

static void dispatch(sock_t s, NoNodeConfig *cfg, const HttpReq *req) {
    char parts[8][128];
    int n = path_parts(req->path, parts);
    const char *m = req->method;

    /* root */
    if (strcmp(req->path, "/") == 0) {
        http_send(s, 200, "{\"service\":\"NewOrder C Explorer API\"}"); return;
    }

    if (strcmp(m, "GET") == 0) {
        if (n==1 && strcmp(parts[0],"stats")==0)    { route_stats(s,cfg);   return; }
        if (n==1 && strcmp(parts[0],"chain")==0)    { route_chain(s,cfg);   return; }
        if (n==1 && strcmp(parts[0],"mempool")==0)  { route_mempool(s,cfg); return; }
        if (n==1 && strcmp(parts[0],"tokens")==0)   { route_tokens(s,cfg);  return; }
        if (n==1 && strcmp(parts[0],"contracts")==0){ route_contracts(s,cfg);return; }
        if (n==2 && strcmp(parts[0],"blocks")==0)   { route_block(s,cfg,parts[1]);   return; }
        if (n==2 && strcmp(parts[0],"tx")==0)        { route_tx(s,cfg,parts[1]);      return; }
        if (n==2 && strcmp(parts[0],"address")==0)  { route_address(s,cfg,parts[1]); return; }
        if (n==2 && strcmp(parts[0],"tokens")==0)   { route_token(s,cfg,parts[1]);   return; }
        if (n==4 && strcmp(parts[0],"tokens")==0 && strcmp(parts[2],"balances")==0)
            { route_token_balance(s,cfg,parts[1],parts[3]); return; }
        if (n==2 && strcmp(parts[0],"contracts")==0){ route_contract(s,cfg,parts[1]);return; }
        if (n==2 && strcmp(parts[0],"ai")==0 && strcmp(parts[1],"services")==0)
            { route_ai_services(s); return; }
        if (n==3 && strcmp(parts[0],"ai")==0 && strcmp(parts[1],"payments")==0)
            { route_ai_payment_get(s,cfg,parts[2]); return; }
        if (n==1 && strcmp(parts[0],"mine")==0) {
            char addr[NO_ADDRESS_LEN];
            qget(req->query, "address", addr, sizeof(addr));
            route_mine(s,cfg,addr); return;
        }
        if (n==1 && strcmp(parts[0],"peers")==0) {
            http_send(s, 200, "{\"peers\":[]}"); return;
        }
    }

    if (strcmp(m, "POST") == 0) {
        if (n==1 && strcmp(parts[0],"transactions")==0)
            { route_post_transaction(s,cfg,req->body); return; }
        if (n==2 && strcmp(parts[0],"ai")==0 && strcmp(parts[1],"payments")==0)
            { route_post_ai_payment(s,cfg,req->body); return; }
        if (n==4 && strcmp(parts[0],"ai")==0 && strcmp(parts[1],"payments")==0
            && strcmp(parts[3],"verify")==0)
            { route_post_ai_verify(s,cfg,parts[2]); return; }
        if (n==4 && strcmp(parts[0],"ai")==0 && strcmp(parts[1],"payments")==0
            && strcmp(parts[3],"consume")==0)
            { route_post_ai_consume(s,cfg,parts[2],req->body); return; }
        if (n==1 && strcmp(parts[0],"peers")==0)
            { http_send(s, 200, "{\"ok\":true,\"note\":\"P2P not implemented in C node\"}"); return; }
    }

    http_send(s, 404, "{\"error\":\"route not found\"}");
}

/* ── server loop ──────────────────────────────────────────────────────────── */

void no_http_serve(NoNodeConfig *cfg) {
    struct sockaddr_in addr;
    sock_t listen_sock;
    int opt = 1;

    net_init();

    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == SOCK_INVALID) {
        fprintf(stderr, "socket() failed\n"); return;
    }
#ifdef _WIN32
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&opt, sizeof(opt));
#else
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons((uint16_t)cfg->port);

    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "bind() failed on port %d\n", cfg->port);
        sock_close(listen_sock); return;
    }
    if (listen(listen_sock, 16) != 0) {
        fprintf(stderr, "listen() failed\n");
        sock_close(listen_sock); return;
    }
    printf("NewOrder C node listening on http://127.0.0.1:%d\n", cfg->port);
    fflush(stdout);

    for (;;) {
        HttpReq req;
        sock_t client = accept(listen_sock, NULL, NULL);
        if (client == SOCK_INVALID) continue;
        if (http_parse(client, &req))
            dispatch(client, cfg, &req);
        sock_close(client);
    }
}
