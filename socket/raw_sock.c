#include "compile.h"
#include "ether.h"
#include "socket.h"
#include "sock.h"
#include "netif.h"
#include "raw.h"
#include "ip.h"
#include "lib.h"
#include "list.h"
#include "wait.h"
#include <string.h>

static unsigned short raw_id;
static struct hlist_head raw_hash_table[IP_P_MAX];

static _inline int __raw_hash_func(int protocol)
{
    return (protocol & IP_P_RAW);
}

static _inline int raw_hash_func(struct sock *sk)
{
    return __raw_hash_func(sk->protocol);
}

static void raw_unhash(struct sock *sk)
{
    sock_del_hash(sk);
}

static int raw_hash(struct sock *sk)
{
    struct hlist_head *hash_head = &raw_hash_table[raw_hash_func(sk)];
    sock_add_hash(sk, hash_head);
    return 0;
}

static void raw_init_pkb(struct sock *sk, struct pkbuf *pkb,
                    struct sock_addr *sk_addr)
{
    struct ip *ip_hdr = pkb2ip(pkb);
    ip_hdr->ip_hlen = IP_HDR_SZ >> 2;
    ip_hdr->ip_ver = IP_VERSION_4;
    ip_hdr->ip_tos= 0;
    ip_hdr->ip_len = _htons(pkb->pk_len - ETH_HDR_SZ);
    ip_hdr->ip_id = _htons(raw_id);
    ip_hdr->ip_fragoff = 0;
    ip_hdr->ip_ttl = RAW_DEFAULT_TTL;
    ip_hdr->ip_pro = sk->protocol;
    ip_hdr->ip_src = sk->sk_saddr;
    ip_hdr->ip_dst = sk_addr->dst_addr;
}

static int raw_send_pkb(struct sock *sk, struct pkbuf *pkb)
{
    ip_send_out(pkb);
    return pkb->pk_len - ETH_HDR_SZ - IP_HDR_SZ;
}

static int raw_send_buf(struct sock *sk, void *buf, int size,
                    struct sock_addr *sk_addr)
{
    struct pkbuf *pkb;
    if (size < 0 || size > RAW_MAX_BUFSZ) {
        return -1;
    }
    pkb = alloc_pkb(ETH_HDR_SZ + IP_HDR_SZ + size);
    memcpy(pkb->pk_data + ETH_HDR_SZ + IP_HDR_SZ, buf, size);
    raw_init_pkb(sk, pkb, sk_addr);
    if (sk->sk_ops->send_pkb) {
        return sk->sk_ops->send_pkb(sk, pkb);
    } else {
        return raw_send_pkb(sk, pkb);
    }
}

static struct sock_ops raw_ops = {
    .recv_notify = sock_recv_notify,
    .recv = sock_recv_pkb,
    .send_pkb = raw_send_pkb,
    .send_buf = raw_send_buf,
    .hash = raw_hash,
    .unhash = raw_unhash,
    .close = sock_close,
};

struct sock *raw_alloc_sock(int protocol)
{
    struct raw_sock *raw_sk;
    if (protocol == IP_P_IP) {
        return NULL;
    }
    raw_sk = xzalloc(sizeof(*raw_sk));
    alloc_socks++;
    raw_sk->sk.sk_ops = &raw_ops;
    raw_sk->sk.hash = protocol;
    raw_id++;
    return &raw_sk->sk;
}

void raw_init(void)
{
    int i;
    for (i = 0; i < IP_P_MAX; i++) {
        hlist_head_init(&raw_hash_table[i]);
    }
    raw_id = 0;
    dbg("raw ip init");
}

static _inline struct sock *__raw_lookup_sock(struct hlist_head *head,
        unsigned int src, unsigned int dst, int proto)
{
    struct sock *sk;
    struct hlist_node *node;
    hlist_for_each_sock(sk, node, head) {
        if ((sk->protocol == proto) &&
            (!sk->sk_saddr || sk->sk_saddr == src) &&
            (!sk->sk_daddr || sk->sk_daddr == dst))
            return sk;
    }
    return NULL;
}

struct sock *raw_lookup_sock_next(struct sock *sk,
        unsigned int src, unsigned int dst, int proto)
{
    struct sock *bak;
    bak = __raw_lookup_sock((struct hlist_head *)&sk->hash_list, 
                        src, dst, proto);
    return bak;
}

struct sock *raw_look_up(unsigned int src, unsigned int dst, int proto)
{
    struct hlist_head *hash_head = &raw_hash_table[__raw_hash_func(proto)];
    struct sock *sk;
    if (hlist_empty(hash_head)) {
        return NULL;
    }
    sk = __raw_lookup_sock(hash_head, src, dst, proto);
    return sk;
}
