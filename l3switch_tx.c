#include"l3switch_main.h"
#define ETH_TYPE_ARP 0x0806
#define ETH_TYPE_IP  0x0800
#define ETH_TYPE_IPV6 0x86dd //we decap here,if IPv6 is supported,we enter ipv6 module for details

extern struct port_usr_para_conf gPortParaConf[MAX_PORT_NB];
extern struct lcore_usr_para_conf gLcoreParaConf[MAX_LCORE_NB];
extern struct netentry gNetEntry[MAX_NETENTRY_ENTRY_NB];
extern struct mac_ip_exception gMACIPExcept[MAX_MAC_IP_EXCEPTION_LENGTH];
extern struct net_flow_item *gFlow;
extern enum PORT_POLICY_MAP_CONST port_policy_map[MAX_PORT_NB][MAX_PORT_NB];
extern int sched_mod_map[MAX_PORT_NB][MAX_PORT_NB];
extern struct qos_sched_mod sched_mod_list[MAX_SCHED_MOD_NB];

extern int port_brdcst_map[MAX_PORT_NB][MAX_PORT_NB];
extern int port_def_frd_map[MAX_PORT_NB];

extern struct rte_mempool* gmempool;

extern int gNBPort;
extern int gNBFlow;
extern int gNBMACExcept;



