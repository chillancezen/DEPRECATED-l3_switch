#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/queue.h>
	 
#include <rte_memory.h>
#include <rte_memzone.h>
#include <rte_launch.h>
#include <rte_tailq.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_lcore.h>
#include <rte_debug.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_malloc.h>
#include <rte_ring.h>
#include <rte_atomic.h>

#pragma pack(1)
#define CONF_FILE_NAME  "./l3switch.conf"
#define CPU_FREQ 2494259000ULL
//#define CPU_FREQ 3300000000ULL//my cpu frequency is 2494.279 MHZ
#define TSC_SECOND CPU_FREQ

//#define CPU_FREQ 2494279ULL//my cpu frequency is 2494.279 MHZ
//#define TSC_MILI_SECOND CPU_FREQ

#define STAT_SUCCESS 0
#define STAT_ERROR -1

#define MIN(a,b) (((a)>(b))?(b):(a))
#define MAX(a,b) (((a)<(b))?(b):(a))

#define TRUE 1
#define FALSE 0
#define FOREVER TRUE
#define MAIN_ENTRY main
#define dbg_local  __attribute__((unused))  

#define MBUF_SIZE (2048 + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define NB_MBUF   1024*16 //16K
#define MAX_PORT_NB 16 
#define MAX_TX_RING_NB 16 
#define MAX_LCORE_NB 64
#define MAX_NETENTRY_ENTRY_NB 64
#define MAX_RX_BURST_NB 32
#define MAX_DEQUEUE_BURST_NB 32
#define MAX_MAC_IP_EXCEPTION_LENGTH 16
#define MAX_SCHED_MOD_NB 16
#define DEF_FIR_LEV_QUE_LEN 2048
#define DEF_SEC_LEV_QUE_LEN 2048

#define DEF_RATE_ADJ 1
#define DEF_PEEK_INFO_RATE 14500*1024*DEF_RATE_ADJ //default PIR=200KB/s
#define DEF_COMMIT_INFO_RATE 10*1024*DEF_RATE_ADJ //default CIR=100KB/s
#define DEF_PEEK_BURST_SIZE 400*1024 //default PBS=400KB
#define DEF_COMMIT_BURST_SIZE 200*1024 //default CBS=200KB
#define DEF_UPDATE_THRESHOLD_MS 1 //if duration less than such thrshold ,we will not update token.
#define IS_MAC_BROADCAST(arr) (!(\
	(0xff^(uint8_t)(arr)[0])|\
	(0xff^(uint8_t)(arr)[1])|\
	(0xff^(uint8_t)(arr)[2])|\
	(0xff^(uint8_t)(arr)[3])|\
	(0xff^(uint8_t)(arr)[4])|\
	(0xff^(uint8_t)(arr)[5])))
#define IS_MAC_EQUAL(arr1,arr2) (\
	((arr1)[0]==(arr2)[0])&&\
	((arr1)[1]==(arr2)[1])&&\
	((arr1)[2]==(arr2)[2])&&\
	((arr1)[3]==(arr2)[3])&&\
	((arr1)[4]==(arr2)[4])&&\
	((arr1)[5]==(arr2)[5]))
#define MAKEUINT32FROMUINT8ARR(arr) (\
	((((arr)[0])<<0)&0x000000ff)|\
	((((arr)[1])<<8)&0x0000ff00)|\
	((((arr)[2])<<16)&0x00ff0000)|\
	((((arr)[3])<<24)&0xff000000))
#define COPYMAC(dst,src) \
	(dst)[0]=(src)[0],\
	(dst)[1]=(src)[1],\
	(dst)[2]=(src)[2],\
	(dst)[3]=(src)[3],\
	(dst)[4]=(src)[4],\
	(dst)[5]=(src)[5];

#define MAKEBYTE(hinib,lonib) ((((uint8_t)(hinib)<<4)&0xf0)|(((uint8_t)(lonib))&0x0f))
#define GETHINIB(b)  ((uint8_t)((b)>>4)&0x0f)
#define GETLONIB(b)  ((uint8_t)((b))&0x0f)

typedef int lcore_job_ptr(void *lpvoid);
enum PORT_ROLE{
	PORT_ROLE_PE=1,//for non-transparent deploy
	PORT_ROLE_CE,//for non-transparent deploy
	PORT_ROLE_INTERIOR,//for transparent deploy
	PORT_ROLE_EXTERIOR,//for transparent deploy
};
struct statistic_data{//fields extendable
	rte_atomic64_t ui_cnt_rx;
	rte_atomic64_t ui_cnt_tx;
	rte_atomic64_t ui_byte_rx;
	rte_atomic64_t ui_byte_tx;
	//below this line is for periodic statistic 
	rte_atomic64_t ui_cnt_timer_rx;
	rte_atomic64_t ui_cnt_timer_tx;
	rte_atomic64_t ui_cnt_timer_last;
	rte_atomic64_t ui_cnt_timer_cur;
};
struct port_usr_para_conf{//user level port parameter conf   
	int portid;//port  index
	int nb_rxd;//rx ring descriptors nb
 	int nb_txd;//tx ring descriptors nb
	enum PORT_ROLE ePortRole;//port role,,though this filed may not be used for policy actually 
	int port_grp;//port group which this port belongs to ,,
	//.ePortRole and .port_grp is used for broadcast policy making
	int bEnabled;

	int inb_tx_ring;//tx ring
	int itx_ring_div_factor[MAX_TX_RING_NB];//used to customize TX queues
	int itx_ring_nb_arr[MAX_TX_RING_NB];
	struct rte_ring *tx_ring_arr[MAX_TX_RING_NB];//used for congestion mng
	
	struct statistic_data sd_port;//port statistic data struct 
	
};

struct qos_sched_mod{
	int modid;
	int bEnabled;
	int iFirLevRingLength;
	struct rte_ring * rrFirLev;
	int iSecLevRingLength;
	struct rte_ring * rrSecLev;
	
};
enum LCORE_ROLE{
	LCORE_ROLE_IDLE=0,
	LCORE_ROLE_RECV,
	LCORE_ROLE_SCHED,
	LCORE_ROLE_SEND,
};
struct lcore_usr_para_conf{
	int lcoreid;
	enum LCORE_ROLE eLcoreRole;
	int bEnabled;
	lcore_job_ptr * jobentry;//entry for lcore task
	void * lparg;//related arg
	union {
		struct {
			int inb_port;
			int iport_arr[MAX_PORT_NB];
		}rx_inner_str;
		struct{
			int inb_port;
			int iport_arr[MAX_PORT_NB];
		}tx_inner_str;
		struct{
			struct qos_sched_mod* sched_mod_ptr;
		}qos_inner_str;
	};
		
};


struct netentry{//little endian  byte order ,not netwok byte order
	uint32_t uiIP;
	uint32_t uiMask;
	int inb_of_entry;
	int ientry_point;
};//when we  configure net entry  tables ,attention must be paid to entry priority that with higher priority,earlier it is been matched 
enum TOKEN_COLOR{
	TC_UNDEF=1,
	TC_GREEN,
	TC_YELLOW,
	TC_RED
};
enum METRE_MODE{
	COLOR_UNDEF=0,
	COLOR_BLIND,
	COLOR_AWARE,
};
struct token_bucket{
	enum METRE_MODE eMeterMode;//default we use Color-Blind mode
	uint32_t uiPIR;//measured in bytes per second
	uint32_t uiPBS;//measured in bytes 
	uint32_t uiCIR;//measured in bytes per second
	uint32_t uiCBS;//measured in bytes 
	uint64_t uiCntLast;//TSC counter
	uint64_t uiCntCur;//TSC counter
	uint64_t uiPToken;
	uint64_t uiCToken;
};

struct net_flow_item{
	uint8_t b_mac_learned:1;//reset to 0,when mac learned,set to 1
	uint8_t b_flow_enabled:1;//be Enabled
	uint8_t b_preempty_enabled:1;//once mac leaned ,if this bit is set,it can be modified,otherwise it's a static arp-ip-port item,never be altered
	uint8_t b_dynamic:1;
	uint8_t b_tb_initialized:1;//indicate whether token buckets are initialized
	uint8_t b_sd_initialized:1;//indicate whether statistic data is initialized
	uint32_t uiInnerIP;//interior IP address
	struct ether_addr eaHostMAC;//interior mac address
	uint8_t portid;//uport_id 
	struct token_bucket tb_uptream;
	struct token_bucket tb_downstream;
	struct statistic_data sd_data;
	//struct statistic_data sd_upstream;
};
enum PORT_POLICY_MAP_CONST{
	PORT_POLICY_MAP_UNDEFINE=1,
	PORT_POLICY_MAP_DIRECT,
	PORT_POLICY_MAP_QOS
};
enum RX_MOD_INDEX{
	RX_MOD_IDLE=0,//not used
	RX_MOD_L2DECAP,//used for layer 2 decapsulation,arp or ip [or vlan] 
	RX_MOD_ARP,
	RX_MOD_IP,
	RX_MOD_IPv6,
	RX_MOD_FORWARD,//include mirror policy
	RX_MOD_DROP,
};//we denote RX_MOD_FORWARD or RX_MOD_DROP is an action module

struct mac_ip_exception{
	uint32_t uip;
	uint8_t uimac[6];	
	uint8_t uiport;
};
enum TRAFFIC_DIR{
TRAFFIC_DIR_UNDEF=1,//by default we just forward such packets,though we may never get around such cases
TRAFFIC_DIR_INBOUND,
TRAFFIC_DIR_OUTBOUND
};
struct sched_stat_str{
	int iDiscard;//
	int isched_mod_id;//indicate which sched module this struct belongs to
	int iport_in;
	int iport_out;
	enum TRAFFIC_DIR enDir;
	int iFlowIdx;
	int iPaketType;//IP or ARP...,identified by ether_type.
	uint64_t iPayloadLength;
	enum TOKEN_COLOR tc_color;
	int iPhaze;//indiacte which which phaze module serve,especially for action-module
};
#define PHAZE_PRIMARY 0x1
#define PHAZE_SECONDARY 0x2

int global_configurate(void);
int port_initizlize(void);
int lcore_initialize(void);
int netentry_initialize(void);
int qos_initialize(void);

dbg_local  void check_all_ports_link_status(uint8_t port_num, uint32_t port_mask);
int lcore_rx_job_entry(void* lparg);
int lcore_sched_job_entry(void *lparg);
int lcore_tx_job_entry(void *lparg);

dbg_local enum RX_MOD_INDEX rx_module_l2decap(dbg_local struct rte_mbuf* pktbuf,dbg_local enum RX_MOD_INDEX imodid);
dbg_local enum RX_MOD_INDEX rx_module_drop(struct rte_mbuf*pktbuf,enum RX_MOD_INDEX imodid);
dbg_local enum RX_MOD_INDEX rx_module_arp(dbg_local struct rte_mbuf*ktbuf,dbg_local enum RX_MOD_INDEX imodid);
dbg_local enum RX_MOD_INDEX rx_module_ip(dbg_local struct rte_mbuf*pktbuf,dbg_local enum RX_MOD_INDEX imodid);
dbg_local enum RX_MOD_INDEX rx_module_ipv6(dbg_local struct rte_mbuf*pktbuf,dbg_local enum RX_MOD_INDEX imodid);
dbg_local enum RX_MOD_INDEX rx_module_forward(dbg_local struct rte_mbuf*pktbuf,dbg_local enum RX_MOD_INDEX imodid);



int sched_module_classify(struct rte_mbuf*pktbuf,struct sched_stat_str *lpstat);
int sched_module_tb_init(struct rte_mbuf*pktbuf,struct sched_stat_str *lpstat);
int sched_module_metre(struct rte_mbuf*pktbuf,struct sched_stat_str *lpstat);
int sched_module_action(struct rte_mbuf*pktbuf,struct sched_stat_str*lpstat);
int sched_module_sanity_check(struct rte_mbuf*pktbuf,struct sched_stat_str*lpstat);


int DequeueFromPortQeueue(int iportid,struct rte_mbuf**pktbuf);
int EnqueueIntoPortQueue(int iportid,struct rte_mbuf**pktbuf,int inb);
int DequeueFromPortByQueueID(int iportid,int iqueueid,struct rte_mbuf**pktbuf);//especially used in Sched mod
int EnqueueIntoPortByQueueID(int iportid,int iqueueid_start,struct rte_mbuf*pktbuf);

int FreePktsArray(struct rte_mbuf**pktbuf,int inb);


//functional modules
uint32_t INET_ADDR(const char*szIP);
uint32_t HTONL(uint32_t uiIP);
uint16_t HTONS(uint16_t usPort);
int find_net_entry(uint32_t uiIP,int is_little_endian);
int FormatMACAddress(char *szMAC,char *szStr);

enum TRAFFIC_DIR GetFlowIndexInStatistic(struct rte_mbuf*pktbuf,int *iFlowIdx);
int set_ip_dscp(struct rte_mbuf*pktbuf,uint8_t iVal);
void ShowPortStatistic(void);
void ShowNetflowStatistic(uint32_t uIP);
void SetNetEntry(uint32_t uIP,int isDownStream,uint32_t uiPIR,uint32_t uiCIR,uint32_t uiPBS,uint32_t uiCBS);



//some struct imported from  iphlp
#pragma pack(1)

#define ETH_ALEN				6

typedef struct arphdr
{
	uint16_t	ar_hrd;		/* format of hardware address	*/
	uint16_t	ar_pro;		/* format of protocol address	*/
	uint8_t 	ar_hln;		/* length of hardware address	*/
	uint8_t 	ar_pln;		/* length of protocol address	*/
	uint16_t	ar_op;		/* ARP opcode (command)		*/
} arphdr, *arphdr_ptr;

typedef struct	ether_arp 
{
	struct	arphdr ea_hdr;	/* fixed-size header */
	uint8_t	arp_sha[ETH_ALEN];	/* sender hardware address */
	uint8_t	arp_spa[4];	/* sender protocol address */
	uint8_t	arp_tha[ETH_ALEN];	/* target hardware address */
	uint8_t	arp_tpa[4];	/* target protocol address */
} ether_arp, *ether_arp_ptr;

typedef struct iphdr 
{
	struct{
		uint8_t     ip_hl:4;		/* header length */
		uint8_t	    ip_v:4;			/* version */
		uint8_t  	ip_dscp:6;		/* DSCP*/
		uint8_t     ip_reserved:2;
	}sh_word;//used for IP-checksum calculation
	
	uint16_t    ip_len;			/* total length */
	uint16_t 	ip_id;			/* identification */
	uint16_t	ip_off;			/* fragment offset field */
#define	IP_DF 0x4000		    /* dont fragment flag */
#define	IP_MF 0x2000		    /* more fragments flag */
	uint8_t 	ip_ttl;			/* time to live */
	uint8_t 	ip_p;			/* protocol */
	uint16_t	ip_sum;			/* checksum */
	uint32_t    ip_src;
	uint32_t    ip_dst;     	/* source and dest address */
} iphdr, *iphdr_ptr;

