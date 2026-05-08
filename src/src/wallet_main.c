/*
 * NewOrder C wallet CLI — creates and manages wallets, submits transactions.
 *
 * Key scheme (educational, not production):
 *   private_key = 32 OS-random bytes, hex-encoded ("sha:{64hex}")
 *   public_key  = SHA-256(private_key_bytes) ("shapub:{64hex}")
 *   address     = "NO" + SHA-256("neworder-address:" + pubkey_hex)[:38]
 *
 * Signatures are SHA-256(privkey_bytes || signing_payload).
 * The C node accepts transactions without verifying signatures.
 */
#define _POSIX_C_SOURCE 200809L

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef int sock_t;
#define SOCK_INVALID     -1
#define sock_close(s)    close(s)
#define sock_recv(s,b,n) read((s),(b),(n))
static void sock_send(sock_t s, const void *buf, size_t n) {
    ssize_t r = write(s, buf, n); (void)r;
}
static void net_init(void) {}

/* ── OS entropy ───────────────────────────────────────────────────────────── */

static int os_random_bytes(unsigned char *buf, size_t n) {
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return 0;
    size_t r = fread(buf, 1, n, f);
    fclose(f);
    return (r == n);
}

/* ── key / address helpers ────────────────────────────────────────────────── */

static void bytes_to_hex(const unsigned char *b, size_t n, char *out) {
    static const char h[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; i++) { out[i*2]=h[b[i]>>4]; out[i*2+1]=h[b[i]&15]; }
    out[n*2] = '\0';
}

static void hex_to_bytes(const char *hex, unsigned char *out, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned int v;
        sscanf(hex + i*2, "%02x", &v);
        out[i] = (unsigned char)v;
    }
}

/* generate a new keypair and derive the NO address */
static void wallet_keygen(char priv_out[140], char pub_out[76],
                           char addr_out[48]) {
    unsigned char raw[32];
    char raw_hex[65];
    unsigned char pub_bytes[32];
    char pub_hex[65];
    char addr_input[128];
    char addr_hex[65];

    os_random_bytes(raw, 32);
    bytes_to_hex(raw, 32, raw_hex);
    snprintf(priv_out, 140, "sha:%s", raw_hex);

    /* public key = SHA-256 of raw private bytes */
    sha256_hex(raw, 32, pub_hex);
    snprintf(pub_out, 76, "shapub:%s", pub_hex);

    /* address = "NO" + SHA-256("neworder-address:" + pub_hex)[:38] */
    snprintf(addr_input, sizeof(addr_input), "neworder-address:%s", pub_hex);
    sha256_hex(addr_input, strlen(addr_input), addr_hex);
    snprintf(addr_out, 48, "NO%.38s", addr_hex);

    (void)pub_bytes; /* suppress warning */
}

/* derive address from stored private key string "sha:{hex}" */
__attribute__((unused))
static void wallet_address_from_priv(const char *priv, char addr_out[48]) {
    const char *hex;
    unsigned char raw[32];
    char pub_hex[65];
    char addr_input[128];
    char addr_hex[65];

    if (strncmp(priv, "sha:", 4) != 0) {
        snprintf(addr_out, 48, "INVALID_KEY");
        return;
    }
    hex = priv + 4;
    hex_to_bytes(hex, raw, 32);
    sha256_hex(raw, 32, pub_hex);
    snprintf(addr_input, sizeof(addr_input), "neworder-address:%s", pub_hex);
    sha256_hex(addr_input, strlen(addr_input), addr_hex);
    snprintf(addr_out, 48, "NO%.38s", addr_hex);
}

/* sign message: SHA-256(privkey_raw || message) */
static void wallet_sign(const char *priv, const char *message, char sig_out[65]) {
    const char *hex;
    unsigned char raw[32];
    SHA256 ctx;
    uint8_t digest[32];
    char hex_out[65];

    if (strncmp(priv, "sha:", 4) != 0) { sig_out[0]='\0'; return; }
    hex = priv + 4;
    hex_to_bytes(hex, raw, 32);

    sha256_init(&ctx);
    sha256_update(&ctx, raw, 32);
    sha256_update(&ctx, message, strlen(message));
    sha256_final(&ctx, digest);
    bytes_to_hex(digest, 32, hex_out);
    snprintf(sig_out, 65, "%s", hex_out);
}

/* ── wallet file I/O ──────────────────────────────────────────────────────── */

typedef struct {
    char private_key[140];
    char public_key[76];
    char address[48];
} Wallet;

static int wallet_save(const char *path, const Wallet *w) {
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return 0; }
    fprintf(f, "{\n  \"coin\": \"NewOrder\",\n  \"symbol\": \"NO\",\n");
    fprintf(f, "  \"address\": \"%s\",\n", w->address);
    fprintf(f, "  \"public_key\": \"%s\",\n", w->public_key);
    fprintf(f, "  \"private_key\": \"%s\"\n}\n", w->private_key);
    fclose(f); return 1;
}

static int wallet_load(const char *path, Wallet *w) {
    FILE *f; char *buf; long sz;
    const char *p, *e;

    f = fopen(path, "r");
    if (!f) { fprintf(stderr, "wallet not found: %s\n", path); return 0; }
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return 0; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return 0; }
    fclose(f);
    buf[sz] = '\0';

    /* extract fields */
    memset(w, 0, sizeof(*w));
#define EXTRACT(field, key) do { \
    char search[64]; \
    snprintf(search, sizeof(search), "\"" key "\""); \
    p = strstr(buf, search); \
    if (p) { \
        p += strlen(search); \
        while (*p == ':' || *p == ' ') p++; \
        if (*p == '"') { p++; e = strchr(p, '"'); \
            if (e) { size_t l = (size_t)(e-p); \
                if (l >= sizeof(w->field)) l = sizeof(w->field)-1; \
                memcpy(w->field, p, l); w->field[l] = '\0'; } } \
    } } while(0)

    EXTRACT(address,     "address");
    EXTRACT(public_key,  "public_key");
    EXTRACT(private_key, "private_key");
#undef EXTRACT

    free(buf);
    if (!w->address[0] || !w->private_key[0]) {
        fprintf(stderr, "malformed wallet file: %s\n", path); return 0;
    }
    return 1;
}

/* ── simple HTTP client ───────────────────────────────────────────────────── */

static int http_request(const char *method, const char *host, int port,
                         const char *path, const char *body,
                         char *resp_buf, size_t resp_sz) {
    struct sockaddr_in addr;
    sock_t s;
    char hdr[1024];
    char tmp[65536];
    int n; size_t total = 0;
    const char *body_start;

    net_init();
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == (sock_t)SOCK_INVALID) return 0;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    addr.sin_addr.s_addr = inet_addr(host);
    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        sock_close(s); return 0;
    }
    snprintf(hdr, sizeof(hdr),
        "%s %s HTTP/1.0\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n",
        method, path, host, port, body ? strlen(body) : (size_t)0);
    sock_send(s, hdr, strlen(hdr));
    if (body && body[0]) sock_send(s, body, strlen(body));

    /* read full response */
    while ((n = (int)sock_recv(s, tmp + total, sizeof(tmp)-1-total)) > 0)
        total += (size_t)n;
    tmp[total] = '\0';
    sock_close(s);

    /* skip HTTP headers */
    body_start = strstr(tmp, "\r\n\r\n");
    if (body_start) body_start += 4;
    else body_start = tmp;
    snprintf(resp_buf, resp_sz, "%s", body_start);
    return 1;
}

/* parse "http://host:port" or "http://host" */
static void parse_node_url(const char *url, char *host_out, int *port_out,
                            char *base_out) {
    const char *p = url;
    const char *colon;
    snprintf(host_out, 64, "127.0.0.1");
    *port_out = 8080;
    base_out[0] = '\0';

    if (strncmp(p, "http://", 7) == 0) p += 7;
    colon = strchr(p, ':');
    if (colon) {
        size_t hlen = (size_t)(colon - p);
        if (hlen >= 64) hlen = 63;
        memcpy(host_out, p, hlen); host_out[hlen] = '\0';
        *port_out = atoi(colon + 1);
    } else {
        size_t hlen = strlen(p);
        if (hlen >= 64) hlen = 63;
        memcpy(host_out, p, hlen); host_out[hlen] = '\0';
    }
}

static int api_get(const char *node, const char *path,
                    char *resp, size_t rsz) {
    char host[64]; int port;
    char base[128]; /* unused */
    parse_node_url(node, host, &port, base);
    return http_request("GET", host, port, path, NULL, resp, rsz);
}

static int api_post(const char *node, const char *path, const char *body,
                     char *resp, size_t rsz) {
    char host[64]; int port; char base[128];
    parse_node_url(node, host, &port, base);
    return http_request("POST", host, port, path, body, resp, rsz);
}

/* ── build transaction JSON for the C node ────────────────────────────────── */

static void build_transfer_json(const Wallet *w, const char *to,
                                  double amount, double fee, const char *memo,
                                  char *out, size_t sz) {
    char sig[65], payload[512];
    snprintf(payload, sizeof(payload),
             "{\"tx_type\":\"transfer\",\"sender\":\"%s\","
             "\"recipient\":\"%s\",\"amount\":%.8f,\"fee\":%.8f,"
             "\"memo\":\"%s\"}", w->address, to, amount, fee, memo);
    wallet_sign(w->private_key, payload, sig);
    snprintf(out, sz, "%s", payload); /* signature omitted — C node doesn't verify */
    (void)sig;
}

/* ── commands ─────────────────────────────────────────────────────────────── */

static int cmd_create(int argc, char **argv) {
    Wallet w;
    const char *path = "wallet.json";
    int i;
    for (i = 0; i < argc-1; i++)
        if (strcmp(argv[i],"--wallet")==0) path = argv[i+1];
    wallet_keygen(w.private_key, w.public_key, w.address);
    if (!wallet_save(path, &w)) return 1;
    printf("{\"address\":\"%s\",\"wallet\":\"%s\"}\n", w.address, path);
    return 0;
}

static int cmd_address(int argc, char **argv) {
    Wallet w;
    const char *path = "wallet.json";
    int i;
    for (i = 0; i < argc-1; i++)
        if (strcmp(argv[i],"--wallet")==0) path = argv[i+1];
    if (!wallet_load(path, &w)) return 1;
    printf("%s\n", w.address);
    return 0;
}

static int cmd_balance(int argc, char **argv) {
    Wallet w; char resp[4096]; char api_path[128];
    const char *path = "wallet.json", *node = "http://127.0.0.1:8080";
    int i;
    for (i = 0; i < argc-1; i++) {
        if (strcmp(argv[i],"--wallet")==0) path = argv[i+1];
        if (strcmp(argv[i],"--node")==0)   node = argv[i+1];
    }
    if (!wallet_load(path, &w)) return 1;
    snprintf(api_path, sizeof(api_path), "/address/%s", w.address);
    if (!api_get(node, api_path, resp, sizeof(resp))) {
        fprintf(stderr, "cannot reach node at %s\n", node); return 1;
    }
    printf("%s\n", resp);
    return 0;
}

static int cmd_send(int argc, char **argv) {
    Wallet w; char body[2048]; char resp[4096];
    const char *path="wallet.json", *node="http://127.0.0.1:8080";
    const char *to=NULL, *memo="";
    double amount=0.0, fee=0.0;
    int i;
    for (i = 0; i < argc-1; i++) {
        if (strcmp(argv[i],"--wallet")==0) path = argv[i+1];
        if (strcmp(argv[i],"--node")==0)   node = argv[i+1];
        if (strcmp(argv[i],"--to")==0)     to   = argv[i+1];
        if (strcmp(argv[i],"--amount")==0) amount = atof(argv[i+1]);
        if (strcmp(argv[i],"--fee")==0)    fee    = atof(argv[i+1]);
        if (strcmp(argv[i],"--memo")==0)   memo   = argv[i+1];
    }
    if (!to) { fprintf(stderr, "--to is required\n"); return 1; }
    if (!wallet_load(path, &w)) return 1;
    build_transfer_json(&w, to, amount, fee, memo, body, sizeof(body));
    if (!api_post(node, "/transactions", body, resp, sizeof(resp))) {
        fprintf(stderr, "cannot reach node\n"); return 1;
    }
    printf("%s\n", resp);
    return 0;
}

static int cmd_mine(int argc, char **argv) {
    Wallet w; char api_path[256]; char resp[65536];
    const char *path="wallet.json", *node="http://127.0.0.1:8080";
    int i;
    for (i = 0; i < argc-1; i++) {
        if (strcmp(argv[i],"--wallet")==0) path = argv[i+1];
        if (strcmp(argv[i],"--node")==0)   node = argv[i+1];
    }
    if (!wallet_load(path, &w)) return 1;
    snprintf(api_path, sizeof(api_path), "/mine?address=%s", w.address);
    if (!api_get(node, api_path, resp, sizeof(resp))) {
        fprintf(stderr, "cannot reach node\n"); return 1;
    }
    printf("%s\n", resp);
    return 0;
}

static int cmd_ai_services(int argc, char **argv) {
    char resp[4096];
    const char *node = "http://127.0.0.1:8080";
    int i;
    for (i = 0; i < argc-1; i++)
        if (strcmp(argv[i],"--node")==0) node = argv[i+1];
    if (!api_get(node, "/ai/services", resp, sizeof(resp))) {
        fprintf(stderr, "cannot reach node\n"); return 1;
    }
    printf("%s\n", resp); return 0;
}

static int cmd_ai_payment_create(int argc, char **argv) {
    Wallet w; char body[512]; char resp[2048];
    const char *path="wallet.json", *node="http://127.0.0.1:8080";
    const char *service_id = "chat-basic";
    int units = 1, i;
    for (i = 0; i < argc-1; i++) {
        if (strcmp(argv[i],"--wallet")==0)     path       = argv[i+1];
        if (strcmp(argv[i],"--node")==0)        node       = argv[i+1];
        if (strcmp(argv[i],"--service-id")==0)  service_id = argv[i+1];
        if (strcmp(argv[i],"--units")==0)        units      = atoi(argv[i+1]);
    }
    if (!wallet_load(path, &w)) return 1;
    snprintf(body, sizeof(body),
             "{\"service_id\":\"%s\",\"customer_address\":\"%s\",\"units\":%d}",
             service_id, w.address, units);
    if (!api_post(node, "/ai/payments", body, resp, sizeof(resp))) {
        fprintf(stderr, "cannot reach node\n"); return 1;
    }
    printf("%s\n", resp); return 0;
}

static int cmd_ai_pay(int argc, char **argv) {
    Wallet w; char get_resp[2048]; char api_path[256];
    char body[1024]; char post_resp[2048];
    const char *path="wallet.json", *node="http://127.0.0.1:8080";
    const char *payment_id = NULL;
    double fee = 0.0; int i;
    /* simple JSON field extract */
    double amount_due; char merchant[64]; char memo[96];

    for (i = 0; i < argc-1; i++) {
        if (strcmp(argv[i],"--wallet")==0)     path       = argv[i+1];
        if (strcmp(argv[i],"--node")==0)        node       = argv[i+1];
        if (strcmp(argv[i],"--payment-id")==0) payment_id = argv[i+1];
        if (strcmp(argv[i],"--fee")==0)         fee        = atof(argv[i+1]);
    }
    if (!payment_id) { fprintf(stderr, "--payment-id required\n"); return 1; }
    if (!wallet_load(path, &w)) return 1;

    snprintf(api_path, sizeof(api_path), "/ai/payments/%s", payment_id);
    if (!api_get(node, api_path, get_resp, sizeof(get_resp))) {
        fprintf(stderr, "cannot reach node\n"); return 1;
    }

    /* extract fields from JSON response */
    amount_due = 0.0; merchant[0] = '\0'; memo[0] = '\0';
    {
        const char *p;
        p = strstr(get_resp, "\"amount_due\":");
        if (p) amount_due = strtod(p + 13, NULL);
        p = strstr(get_resp, "\"merchant_address\":\"");
        if (p) { p += 20; const char *e = strchr(p,'"');
                 if (e) { size_t l=(size_t)(e-p); if(l>=sizeof(merchant))l=sizeof(merchant)-1;
                          memcpy(merchant,p,l); merchant[l]='\0'; } }
        p = strstr(get_resp, "\"memo\":\"");
        if (p) { p += 8; const char *e = strchr(p,'"');
                 if (e) { size_t l=(size_t)(e-p); if(l>=sizeof(memo))l=sizeof(memo)-1;
                          memcpy(memo,p,l); memo[l]='\0'; } }
    }
    if (!merchant[0]) { fprintf(stderr, "could not parse payment\n"); return 1; }

    snprintf(body, sizeof(body),
             "{\"tx_type\":\"transfer\",\"sender\":\"%s\","
             "\"recipient\":\"%s\",\"amount\":%.8f,\"fee\":%.8f,"
             "\"memo\":\"%s\"}",
             w.address, merchant, amount_due, fee, memo);
    if (!api_post(node, "/transactions", body, post_resp, sizeof(post_resp))) {
        fprintf(stderr, "cannot reach node\n"); return 1;
    }
    printf("%s\n", post_resp); return 0;
}

static int cmd_ai_verify(int argc, char **argv) {
    char api_path[256]; char resp[2048];
    const char *node="http://127.0.0.1:8080", *payment_id=NULL;
    int i;
    for (i = 0; i < argc-1; i++) {
        if (strcmp(argv[i],"--node")==0)        node       = argv[i+1];
        if (strcmp(argv[i],"--payment-id")==0) payment_id = argv[i+1];
    }
    if (!payment_id) { fprintf(stderr, "--payment-id required\n"); return 1; }
    snprintf(api_path, sizeof(api_path), "/ai/payments/%s/verify", payment_id);
    if (!api_post(node, api_path, "{}", resp, sizeof(resp))) {
        fprintf(stderr, "cannot reach node\n"); return 1;
    }
    printf("%s\n", resp); return 0;
}

static int cmd_ai_consume(int argc, char **argv) {
    char api_path[256]; char body[1024]; char resp[4096];
    const char *node="http://127.0.0.1:8080", *payment_id=NULL, *prompt="";
    int i;
    for (i = 0; i < argc-1; i++) {
        if (strcmp(argv[i],"--node")==0)        node       = argv[i+1];
        if (strcmp(argv[i],"--payment-id")==0) payment_id = argv[i+1];
        if (strcmp(argv[i],"--prompt")==0)      prompt     = argv[i+1];
    }
    if (!payment_id) { fprintf(stderr, "--payment-id required\n"); return 1; }
    snprintf(api_path, sizeof(api_path), "/ai/payments/%s/consume", payment_id);
    snprintf(body, sizeof(body), "{\"prompt\":\"%s\"}", prompt);
    if (!api_post(node, api_path, body, resp, sizeof(resp))) {
        fprintf(stderr, "cannot reach node\n"); return 1;
    }
    printf("%s\n", resp); return 0;
}

static int cmd_token_create(int argc, char **argv) {
    Wallet w; char body[512]; char resp[2048];
    const char *path="wallet.json", *node="http://127.0.0.1:8080";
    const char *name=NULL, *symbol=NULL;
    double supply=0.0, fee=0.0; int decimals=0, i;
    for (i = 0; i < argc-1; i++) {
        if (strcmp(argv[i],"--wallet")==0)   path    = argv[i+1];
        if (strcmp(argv[i],"--node")==0)      node    = argv[i+1];
        if (strcmp(argv[i],"--name")==0)      name    = argv[i+1];
        if (strcmp(argv[i],"--symbol")==0)    symbol  = argv[i+1];
        if (strcmp(argv[i],"--supply")==0)    supply  = atof(argv[i+1]);
        if (strcmp(argv[i],"--decimals")==0)  decimals= atoi(argv[i+1]);
        if (strcmp(argv[i],"--fee")==0)       fee     = atof(argv[i+1]);
    }
    if (!name||!symbol) { fprintf(stderr,"--name and --symbol required\n"); return 1; }
    if (!wallet_load(path, &w)) return 1;
    snprintf(body, sizeof(body),
             "{\"tx_type\":\"token_create\",\"sender\":\"%s\","
             "\"token_name\":\"%s\",\"token_symbol\":\"%s\","
             "\"token_total_supply\":%.8f,\"token_decimals\":%d,\"fee\":%.8f}",
             w.address, name, symbol, supply, decimals, fee);
    if (!api_post(node, "/transactions", body, resp, sizeof(resp))) {
        fprintf(stderr, "cannot reach node\n"); return 1;
    }
    printf("%s\n", resp); return 0;
}

static int cmd_token_send(int argc, char **argv) {
    Wallet w; char body[512]; char resp[2048];
    const char *path="wallet.json", *node="http://127.0.0.1:8080";
    const char *token_id=NULL, *to=NULL;
    double amount=0.0, fee=0.0; int i;
    for (i = 0; i < argc-1; i++) {
        if (strcmp(argv[i],"--wallet")==0)    path     = argv[i+1];
        if (strcmp(argv[i],"--node")==0)       node     = argv[i+1];
        if (strcmp(argv[i],"--token-id")==0)  token_id = argv[i+1];
        if (strcmp(argv[i],"--to")==0)         to       = argv[i+1];
        if (strcmp(argv[i],"--amount")==0)     amount   = atof(argv[i+1]);
        if (strcmp(argv[i],"--fee")==0)        fee      = atof(argv[i+1]);
    }
    if (!token_id||!to) { fprintf(stderr,"--token-id and --to required\n"); return 1; }
    if (!wallet_load(path, &w)) return 1;
    snprintf(body, sizeof(body),
             "{\"tx_type\":\"token_transfer\",\"sender\":\"%s\","
             "\"ref_id\":\"%s\",\"token_to\":\"%s\","
             "\"token_amount\":%.8f,\"fee\":%.8f}",
             w.address, token_id, to, amount, fee);
    if (!api_post(node, "/transactions", body, resp, sizeof(resp))) {
        fprintf(stderr, "cannot reach node\n"); return 1;
    }
    printf("%s\n", resp); return 0;
}

static int cmd_contract_deploy(int argc, char **argv) {
    Wallet w; char body[512]; char resp[2048];
    const char *path="wallet.json", *node="http://127.0.0.1:8080";
    const char *name=NULL, *contract_type="counter";
    double fee=0.0; int i, is_counter=1;
    for (i = 0; i < argc-1; i++) {
        if (strcmp(argv[i],"--wallet")==0)        path          = argv[i+1];
        if (strcmp(argv[i],"--node")==0)           node          = argv[i+1];
        if (strcmp(argv[i],"--name")==0)           name          = argv[i+1];
        if (strcmp(argv[i],"--contract-type")==0) contract_type = argv[i+1];
        if (strcmp(argv[i],"--fee")==0)            fee           = atof(argv[i+1]);
    }
    if (!name) { fprintf(stderr,"--name required\n"); return 1; }
    is_counter = strcmp(contract_type,"key_value") != 0;
    if (!wallet_load(path, &w)) return 1;
    snprintf(body, sizeof(body),
             "{\"tx_type\":\"contract_deploy\",\"sender\":\"%s\","
             "\"contract_name\":\"%s\",\"contract_is_counter\":%d,\"fee\":%.8f}",
             w.address, name, is_counter, fee);
    if (!api_post(node, "/transactions", body, resp, sizeof(resp))) {
        fprintf(stderr, "cannot reach node\n"); return 1;
    }
    printf("%s\n", resp); return 0;
}

static int cmd_contract_call(int argc, char **argv) {
    Wallet w; char body[1024]; char resp[2048];
    const char *path="wallet.json", *node="http://127.0.0.1:8080";
    const char *contract_id=NULL, *method=NULL;
    const char *arg_key="", *arg_value="";
    double fee=0.0, arg_amount=0.0; int i;
    for (i = 0; i < argc-1; i++) {
        if (strcmp(argv[i],"--wallet")==0)       path        = argv[i+1];
        if (strcmp(argv[i],"--node")==0)          node        = argv[i+1];
        if (strcmp(argv[i],"--contract-id")==0)  contract_id = argv[i+1];
        if (strcmp(argv[i],"--method")==0)        method      = argv[i+1];
        if (strcmp(argv[i],"--arg-amount")==0)   arg_amount  = atof(argv[i+1]);
        if (strcmp(argv[i],"--arg-key")==0)      arg_key     = argv[i+1];
        if (strcmp(argv[i],"--arg-value")==0)    arg_value   = argv[i+1];
        if (strcmp(argv[i],"--fee")==0)           fee         = atof(argv[i+1]);
    }
    if (!contract_id||!method) {
        fprintf(stderr,"--contract-id and --method required\n"); return 1;
    }
    if (!wallet_load(path, &w)) return 1;
    snprintf(body, sizeof(body),
             "{\"tx_type\":\"contract_call\",\"sender\":\"%s\","
             "\"ref_id\":\"%s\",\"contract_method\":\"%s\","
             "\"contract_arg_amount\":%.0f,"
             "\"contract_arg_key\":\"%s\","
             "\"contract_arg_value\":\"%s\",\"fee\":%.8f}",
             w.address, contract_id, method, arg_amount, arg_key, arg_value, fee);
    if (!api_post(node, "/transactions", body, resp, sizeof(resp))) {
        fprintf(stderr, "cannot reach node\n"); return 1;
    }
    printf("%s\n", resp); return 0;
}

/* ── main ─────────────────────────────────────────────────────────────────── */

static void usage_wallet(const char *prog) {
    printf("NewOrder C wallet\n");
    printf("Usage: %s <command> [options]\n\n", prog);
    printf("Commands:\n");
    printf("  create           --wallet FILE\n");
    printf("  address          --wallet FILE\n");
    printf("  balance          --wallet FILE [--node URL]\n");
    printf("  send             --wallet FILE --to ADDR --amount N [--fee F] [--memo M] [--node URL]\n");
    printf("  mine             --wallet FILE [--node URL]\n");
    printf("  ai-services      [--node URL]\n");
    printf("  ai-payment-create --wallet FILE --service-id ID [--units N] [--node URL]\n");
    printf("  ai-pay           --wallet FILE --payment-id AIP... [--fee F] [--node URL]\n");
    printf("  ai-verify        --payment-id AIP... [--node URL]\n");
    printf("  ai-consume       --payment-id AIP... [--prompt TEXT] [--node URL]\n");
    printf("  token-create     --wallet FILE --name NAME --symbol SYM --supply N [--decimals D] [--fee F] [--node URL]\n");
    printf("  token-send       --wallet FILE --token-id NOT... --to ADDR --amount N [--fee F] [--node URL]\n");
    printf("  contract-deploy  --wallet FILE --name NAME --contract-type counter|key_value [--fee F] [--node URL]\n");
    printf("  contract-call    --wallet FILE --contract-id NOC... --method M [--arg-amount N] [--arg-key K] [--arg-value V] [--fee F] [--node URL]\n");
    printf("\nNote: This wallet uses SHA-256-based keys (not RSA). Use the Python wallet\n");
    printf("      for RSA keys compatible with the Python node.\n");
}

int main(int argc, char **argv) {
    if (argc < 2) { usage_wallet(argv[0]); return 1; }
    if (strcmp(argv[1],"create")==0)           return cmd_create(argc-1,argv+1);
    if (strcmp(argv[1],"address")==0)          return cmd_address(argc-1,argv+1);
    if (strcmp(argv[1],"balance")==0)          return cmd_balance(argc-1,argv+1);
    if (strcmp(argv[1],"send")==0)             return cmd_send(argc-1,argv+1);
    if (strcmp(argv[1],"mine")==0)             return cmd_mine(argc-1,argv+1);
    if (strcmp(argv[1],"ai-services")==0)      return cmd_ai_services(argc-1,argv+1);
    if (strcmp(argv[1],"ai-payment-create")==0)return cmd_ai_payment_create(argc-1,argv+1);
    if (strcmp(argv[1],"ai-pay")==0)           return cmd_ai_pay(argc-1,argv+1);
    if (strcmp(argv[1],"ai-verify")==0)        return cmd_ai_verify(argc-1,argv+1);
    if (strcmp(argv[1],"ai-consume")==0)       return cmd_ai_consume(argc-1,argv+1);
    if (strcmp(argv[1],"token-create")==0)     return cmd_token_create(argc-1,argv+1);
    if (strcmp(argv[1],"token-send")==0)       return cmd_token_send(argc-1,argv+1);
    if (strcmp(argv[1],"contract-deploy")==0)  return cmd_contract_deploy(argc-1,argv+1);
    if (strcmp(argv[1],"contract-call")==0)    return cmd_contract_call(argc-1,argv+1);
    fprintf(stderr, "unknown command: %s\n", argv[1]);
    usage_wallet(argv[0]); return 1;
}
