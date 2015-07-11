/*
@project:layer-3 switch based on Intel DPDK 
@author:jzheng from UICT&BJTU
@date:2014-05-07-?
@file description: this source file include app entry and some initial function encapsulation
*/

#include"l3switch_main.h"




//--------------below this line is global can be initialized from .conf file---
int gNBMbuf=NB_MBUF;
int gNBPort;
int gNBLcore;
int gNBnetentry;
int gNBFlow;
int gNBMACExcept;
struct net_flow_item *gFlow;
struct ether_addr gport_add_arr[MAX_PORT_NB];

struct rte_mempool* gmempool;
static const struct rte_eth_conf port_conf = {//port conf struct,,imported from l2fwd main.c
	.link_speed=ETH_LINK_SPEED_AUTONEG,
	.link_duplex=ETH_LINK_AUTONEG_DUPLEX,
	.rxmode = {
		.split_hdr_size = 0,
		.header_split   = 0, /**< Header Split disabled */
		.hw_ip_checksum = 0, /**< IP checksum offload disabled */
		.hw_vlan_filter = 0, /**< VLAN filtering disabled */
		.jumbo_frame    = 0, /**< Jumbo Frame Support disabled */
		.hw_strip_crc   = 0, /**< CRC stripped by hardware */
	},
	.txmode = {
		.mq_mode = ETH_MQ_TX_NONE,
	},
};
int gnbRxd=128;//descriptors nb of rx ring,imported from l2fwd
int gnbTxd=512;//descriptors nb of tx ring,imported from l2fwd

#define RX_PTHRESH 8 /**< Default values of RX prefetch threshold reg.,imported from l2fwd */
#define RX_HTHRESH 8 /**< Default values of RX host threshold reg. ,imported from l2fwd*/
#define RX_WTHRESH 4 /**< Default values of RX write-back threshold reg.,imported from l2fwd */
struct rte_eth_rxconf rx_conf={//imported from l2fwd
.rx_thresh={
	.pthresh=RX_PTHRESH,
	.hthresh=RX_HTHRESH,
	.wthresh=RX_WTHRESH,
},
};
#define TX_PTHRESH 36 /**< Default values of TX prefetch threshold reg.,imported from l2fwd*/
#define TX_HTHRESH 0  /**< Default values of TX host threshold reg.,imported from l2fwd */
#define TX_WTHRESH 0  /**< Default values of TX write-back threshold reg.,imported from l2fwd */
static const struct rte_eth_txconf tx_conf = {//imported from l2fwd
	.tx_thresh = {
		.pthresh = TX_PTHRESH,
		.hthresh = TX_HTHRESH,
		.wthresh = TX_WTHRESH,
	},
	.tx_free_thresh = 0, /* Use PMD default values */
	.tx_rs_thresh = 0, /* Use PMD default values */
	.txq_flags = ETH_TXQ_FLAGS_NOMULTSEGS | ETH_TXQ_FLAGS_NOOFFLOADS,
};
struct port_usr_para_conf gPortParaConf[MAX_PORT_NB];
struct lcore_usr_para_conf gLcoreParaConf[MAX_LCORE_NB];
struct netentry gNetEntry[MAX_NETENTRY_ENTRY_NB];
struct mac_ip_exception gMACIPExcept[MAX_MAC_IP_EXCEPTION_LENGTH];
enum PORT_POLICY_MAP_CONST port_policy_map[MAX_PORT_NB][MAX_PORT_NB];
int sched_mod_map[MAX_PORT_NB][MAX_PORT_NB];
struct qos_sched_mod sched_mod_list[MAX_SCHED_MOD_NB];

int port_brdcst_map[MAX_PORT_NB][MAX_PORT_NB];
int port_def_frd_map[MAX_PORT_NB];


//--------------end of lines
int MAIN_ENTRY (dbg_local int argc,dbg_local char**argv)
{

	dbg_local int ret;
	//dbg_local int port_nb;
	dbg_local int idx;
	
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_panic("Cannot init EAL\n");
	ret=rte_pmd_init_all();
	if(ret<0)
		rte_panic("can not init pmd driver");
	ret=rte_eal_pci_probe();
	if(ret<0)
		rte_panic("can not init pmd driver");
	gNBPort=rte_eth_dev_count();
	gNBLcore=rte_lcore_count();
	printf(">>>:%d ports available\n",gNBPort);
	printf(">>>:%d lcores available\n",gNBLcore);
	gmempool=rte_mempool_create("my_mem_pool",//create memory pool  ,size 16k,all lcore share one mempool?
	gNBMbuf,
	MBUF_SIZE,
	32,
	sizeof(struct rte_pktmbuf_pool_private),
	rte_pktmbuf_pool_init,NULL,
	rte_pktmbuf_init,NULL,
	rte_socket_id(),
	0);
	if(!gmempool)
		rte_panic(">>>can not create memory pool\n");
	
	global_configurate();
	port_initizlize();
	netentry_initialize();
	qos_initialize();
	lcore_initialize();

#if 0
	dbg_local uint64_t prev_tsc, diff_tsc, cur_tsc,timer_tsc;
	prev_tsc=rte_rdtsc();//init 
	timer_tsc=0;
	
	while(1)
	{
		cur_tsc=rte_rdtsc();
		diff_tsc=cur_tsc-prev_tsc;
		prev_tsc=cur_tsc;
		timer_tsc+=diff_tsc;
		if(timer_tsc>=TSC_SECOND){
			printf("bingo:%"PRIu64"\n",timer_tsc);
			timer_tsc=0;
		}
	}
	printf("prev:%"PRIu64"\ncur:%"PRIu64"\ndiff:%"PRIu64"\n",prev_tsc,cur_tsc,diff_tsc);

	printf("sizeof(int) is %d\n",(int)sizeof(int));
	printf("sizeof(long) is %d\n",(int)sizeof(long));
	printf("sizeof(long long) is %d\n",(int)sizeof(long long));
#endif
	dbg_local char szCmd[256];
	dbg_local char *lptr,*lpkeep;
	#define MAX_ARGC_NB 32
	dbg_local int iArgc;
	dbg_local char * szArgv[MAX_ARGC_NB];
	dbg_local char szCmdLineSeq[][MAX_ARGC_NB][MAX_ARGC_NB]={
			{"show","link","status",},//00
			{"show","port","statistics",},//01
			{"show","usr","statistics",},//02
			{"set","downstream","pir"},//03
			{"set","downstream","cir"},//04
			{"set","downstream","pbs"},//05
			{"set","downstream","cbs"},//06
			{"set","upstream","pir"},//07
			{"set","upstream","cir"},//08
			{"set","upstream","pbs"},//09
			{"set","upstream","cbs"},//10

		};
	dbg_local int iCmdLineLen[]={3,3,3,3,3,3,3,3,3,3};
	
	dbg_local int idx_tmp;
	dbg_local int iCmdLineNo=sizeof(szCmdLineSeq)/sizeof(szCmdLineSeq[0]);
	for(idx=0;idx<iCmdLineNo;idx++)
	{
		
	}
	while(TRUE)
	{
		printf(">>>");
		memset(szCmd,0x0,sizeof(szCmd));
		//resove arguments into array
		//int iptr=0;
		//char ch;
		fgets(szCmd,sizeof(szCmd),stdin);
	
		lptr=szCmd;
		//while(*lptr==' ')lptr++;
		for(iArgc=0;*lptr&&*lptr!='\xa';iArgc++)
		{	
			while(*lptr==' ')lptr++;
			for(lpkeep=lptr;*lpkeep&&*lpkeep!='\xa'&&*lpkeep!=' ';lpkeep++);
			if(lpkeep==lptr)break;
			*lpkeep='\0';
			szArgv[iArgc]=lptr;
			//strcpy((char*)szArgv[iArgc],lptr);//,lpkeep-lptr);
			lptr=lpkeep+1;
		}
		for(idx=0;idx<iCmdLineNo;idx++){
			
			for(idx_tmp=0;idx_tmp<iCmdLineLen[idx];idx_tmp++)
				if(strcmp(szArgv[idx_tmp],szCmdLineSeq[idx][idx_tmp]))break;
			if(idx_tmp==iCmdLineLen[idx])break;
		}
		if(idx==iCmdLineNo)goto cmd_tag;
		
		switch(idx)
		{
			case 0:
				check_all_ports_link_status(gNBPort,0xff);
				break;
			case 1:
				ShowPortStatistic();
				break;
			case 2:
				if(!szArgv[3]) goto cmd_tag2;
				ShowNetflowStatistic(HTONL(INET_ADDR(szArgv[3])));
				break;
			case 3://downstream pir
				if(!szArgv[3]||!szArgv[4]) goto cmd_tag2;
				SetNetEntry(HTONL(INET_ADDR(szArgv[3])),TRUE,atoi(szArgv[4]),0,0,0);
				break;
			case 4://downstream cir
				if(!szArgv[3]||!szArgv[4]) goto cmd_tag2;
				SetNetEntry(HTONL(INET_ADDR(szArgv[3])),TRUE,0,atoi(szArgv[4]),0,0);
				break;
			case 5://downstream pbs
				if(!szArgv[3]||!szArgv[4]) goto cmd_tag2;
				SetNetEntry(HTONL(INET_ADDR(szArgv[3])),TRUE,0,0,atoi(szArgv[4]),0);
				break;
			case 6://downstream cbs
				if(!szArgv[3]||!szArgv[4]) goto cmd_tag2;
				SetNetEntry(HTONL(INET_ADDR(szArgv[3])),TRUE,0,0,0,atoi(szArgv[4]));
				break;
			case 7://upstream pir
				if(!szArgv[3]||!szArgv[4]) goto cmd_tag2;
				SetNetEntry(HTONL(INET_ADDR(szArgv[3])),FALSE,atoi(szArgv[4]),0,0,0);
				break;
			case 8://upstream cir
				if(!szArgv[3]||!szArgv[4]) goto cmd_tag2;
				SetNetEntry(HTONL(INET_ADDR(szArgv[3])),FALSE,0,atoi(szArgv[4]),0,0);
				break;
			case 9://upstream pbs
				if(!szArgv[3]||!szArgv[4]) goto cmd_tag2;
				SetNetEntry(HTONL(INET_ADDR(szArgv[3])),FALSE,0,0,atoi(szArgv[4]),0);
				break;
			case 10://upstream cbs
				if(!szArgv[3]||!szArgv[4]) goto cmd_tag2;
				SetNetEntry(HTONL(INET_ADDR(szArgv[3])),FALSE,0,0,0,atoi(szArgv[4]));
				break;
			default:
				
				break;
		}

		
		
		continue;
		cmd_tag:
			printf(">>>unknown command\n");
			continue;
		cmd_tag2:
			printf(">>>incomplete command\n");
		
		//();
		//ShowNetflowStatistic(HTONL(INET_ADDR("192.168.0.6")));
	}
	RTE_LCORE_FOREACH_SLAVE(idx){
		ret=rte_eal_wait_lcore((idx));
		if(ret<0)
			printf("...wait lcore %d fail\n",(idx));
		else 
			printf("...wait lcore %d finish\n",(idx));
		}

	return 0;
}
/*
functional description:
initialize port related structure according to configured global data
author:jzheng
date:2014-5-16
*/
int port_initizlize(void)
{
	int idx,idx_buff;
	int ret;
	char szNameBuff[256];
	for(idx=0;idx<gNBPort;idx++)
	{
		if(gPortParaConf[idx].bEnabled==FALSE)continue;
		//sys queue setup
		ret=rte_eth_dev_configure(idx,1,1,&port_conf);
		if(ret<0)
			printf(">>>port %d rte_eth_dev_configure fails\n",idx);
		rte_eth_macaddr_get(idx,&gport_add_arr[idx]);
		printf(">>>:port %d mac:%02x:%02x:%02x:%02x:%02x:%02x\n",idx,gport_add_arr[idx].addr_bytes[0]
			,gport_add_arr[idx].addr_bytes[1]
			,gport_add_arr[idx].addr_bytes[2]
			,gport_add_arr[idx].addr_bytes[3]
			,gport_add_arr[idx].addr_bytes[4]
			,gport_add_arr[idx].addr_bytes[5]);
		ret=rte_eth_rx_queue_setup(idx,0,gPortParaConf[idx].nb_rxd,rte_eth_dev_socket_id(idx),&rx_conf,gmempool);
		if(ret<0)
			printf(">>>:port %d rte_eth_rx_queue_setup fails\n",idx);
		ret=rte_eth_tx_queue_setup(idx,0,gPortParaConf[idx].nb_txd,rte_eth_dev_socket_id(idx),&tx_conf);
		if(ret<0)
			printf(">>>:port %d rte_eth_tx_queue_setup fails\n",idx);
		//user queue setup
		for(idx_buff=0;idx_buff<gPortParaConf[idx].inb_tx_ring;idx_buff++)
		{
			sprintf(szNameBuff,"tx_ring_%d.%d",idx,idx_buff);
			gPortParaConf[idx].tx_ring_arr[idx_buff]=rte_ring_create(szNameBuff,gPortParaConf[idx].itx_ring_nb_arr[idx_buff],SOCKET_ID_ANY,RING_F_SC_DEQ);//multi-producer and single-consumer
			if(!gPortParaConf[idx].tx_ring_arr[idx_buff])
				printf(">>>:rte_ring_create %s fails\n",szNameBuff);
			//printf(">>>:ring %s cnt:%d\n",szNameBuff,rte_ring_count(gPortParaConf[idx].tx_ring_arr[idx_buff]));
		}
		//start port
		ret=rte_eth_dev_start(idx);
		if(ret<0)
			printf(">>>:start dev fails on port %d\n",idx);
		rte_eth_promiscuous_enable(idx);
		
	}
	for(idx=0;idx<gNBPort;idx++)
		for(idx_buff=0;idx_buff<gNBPort;idx_buff++)
		{
			switch(gPortParaConf[idx].ePortRole)
			{
			case PORT_ROLE_PE:
				port_brdcst_map[idx][idx_buff]=((gPortParaConf[idx_buff].ePortRole==PORT_ROLE_CE)
					&&(gPortParaConf[idx_buff].port_grp==gPortParaConf[idx].port_grp))?TRUE:FALSE;
				break;
			case PORT_ROLE_CE:
				port_brdcst_map[idx][idx_buff]=((gPortParaConf[idx_buff].ePortRole==PORT_ROLE_PE)
					&&(gPortParaConf[idx_buff].port_grp==gPortParaConf[idx].port_grp))?TRUE:FALSE;
				break;
			case PORT_ROLE_INTERIOR:
				port_brdcst_map[idx][idx_buff]=((gPortParaConf[idx_buff].ePortRole==PORT_ROLE_EXTERIOR)
					&&(gPortParaConf[idx_buff].port_grp==gPortParaConf[idx].port_grp))?TRUE:FALSE;
				break;
			case PORT_ROLE_EXTERIOR:
				port_brdcst_map[idx][idx_buff]=((gPortParaConf[idx_buff].ePortRole==PORT_ROLE_INTERIOR)
					&&(gPortParaConf[idx_buff].port_grp==gPortParaConf[idx].port_grp))?TRUE:FALSE;
				break;
			}
		}
	for(idx=0;idx<gNBPort;idx++){
		switch(gPortParaConf[idx].ePortRole)
		{
			case PORT_ROLE_PE:
				port_def_frd_map[idx]=-1;//not mapped
				break;
			case PORT_ROLE_CE://defalut tx port is PE port,such as gateway 
				for(idx_buff=0;idx_buff<gNBPort;idx_buff++)
					if((gPortParaConf[idx_buff].ePortRole==PORT_ROLE_PE)&&
						(gPortParaConf[idx_buff].port_grp==gPortParaConf[idx].port_grp))
						break;
				port_def_frd_map[idx]=idx_buff<gNBPort?idx_buff:-1;
				break;
			case PORT_ROLE_INTERIOR:
				for(idx_buff=0;idx_buff<gNBPort;idx_buff++)
					if((gPortParaConf[idx_buff].ePortRole==PORT_ROLE_EXTERIOR)&&
						(gPortParaConf[idx_buff].port_grp==gPortParaConf[idx].port_grp))
				port_def_frd_map[idx]=idx_buff<gNBPort?idx_buff:-1;
				break;
			case PORT_ROLE_EXTERIOR:
				for(idx_buff=0;idx_buff<gNBPort;idx_buff++)
					if((gPortParaConf[idx_buff].ePortRole==PORT_ROLE_INTERIOR)&&
						(gPortParaConf[idx_buff].port_grp==gPortParaConf[idx].port_grp))
				port_def_frd_map[idx]=idx_buff<gNBPort?idx_buff:-1;
					
				break;
		}
	}
		
	//check_all_ports_link_status(4,0xf);
	return STAT_SUCCESS;
}
/*
functional description:
initialize QoS schedualer related structure according to configured global data
author:jzheng
date:2014-5-16
*/

int qos_initialize(void)
{
	char szName[64];
	int idx;
	#if 0
	for(idx=0;idx<MAX_SCHED_MOD_NB;idx++){
		sched_mod_list[idx].modid=idx;
		sched_mod_list[idx].bEnabled=(sched_mod_list[idx].bEnabled!=TRUE)?FALSE:TRUE;
		sched_mod_list[idx].iFirLevRingLength=DEF_FIR_LEV_QUE_LEN;
		sched_mod_list[idx].iSecLevRingLength=DEF_SEC_LEV_QUE_LEN;
	}
	#endif
	
	for(idx=0;idx<MAX_SCHED_MOD_NB;idx++){
		if(sched_mod_list[idx].bEnabled==FALSE)continue;
		sprintf(szName,"qos_mod_%d_ring_1st",idx);
		sched_mod_list[idx].rrFirLev=rte_ring_create(szName,sched_mod_list[idx].iFirLevRingLength,SOCKET_ID_ANY,RING_F_SC_DEQ);
		sprintf(szName,"qos_mod_%d_ring_2nd",idx);
		sched_mod_list[idx].rrSecLev=rte_ring_create(NULL,sched_mod_list[idx].iSecLevRingLength,SOCKET_ID_ANY,RING_F_SC_DEQ);
		if(!sched_mod_list[idx].rrFirLev||!sched_mod_list[idx].rrSecLev){
			printf(">>>Error:QoS Sched Ring Creation Fails\n");
			exit(0);
		}
	}
	
	return STAT_SUCCESS;
}
/*
functional description:
initialize lcore related structure according to configured global data
author:jzheng
date:2014-5-16
*/

int lcore_initialize(void)
{
	int idx;
	for(idx=0;idx<gNBLcore;idx++)
	{
	   
		if(gLcoreParaConf[idx].bEnabled==FALSE)continue;
		
		switch(gLcoreParaConf[idx].eLcoreRole)
		{
			case LCORE_ROLE_RECV:
				rte_eal_remote_launch(gLcoreParaConf[idx].jobentry,gLcoreParaConf[idx].lparg,gLcoreParaConf[idx].lcoreid);
				break;
			case LCORE_ROLE_SCHED:
				rte_eal_remote_launch(gLcoreParaConf[idx].jobentry,gLcoreParaConf[idx].lparg,gLcoreParaConf[idx].lcoreid);
				break;
			case LCORE_ROLE_SEND:
				rte_eal_remote_launch(gLcoreParaConf[idx].jobentry,gLcoreParaConf[idx].lparg,gLcoreParaConf[idx].lcoreid);
				break;
			case LCORE_ROLE_IDLE:
				
				break;
		}
	}
	return STAT_SUCCESS;
}
/*
functional description:
initialize net-entry related structure according to configured global data
author:jzheng
date:2014-5-16
*/

int netentry_initialize(void)
{
	int idx;
	int idx_sub;
	int idx_item=0;
	for(idx=0;idx<gNBnetentry;idx++)
	{
		gNetEntry[idx].inb_of_entry=~gNetEntry[idx].uiMask;
		gNetEntry[idx].ientry_point=(idx==0)?0:(gNetEntry[idx-1].ientry_point+gNetEntry[idx-1].inb_of_entry);
		printf(">>>:net entry:%08x/%08x  %-8d %-8d\n",gNetEntry[idx].uiIP,gNetEntry[idx].uiMask,gNetEntry[idx].ientry_point,gNetEntry[idx].inb_of_entry);
	}
	//allocate mem rc
	gNBFlow=gNetEntry[gNBnetentry-1].ientry_point+gNetEntry[gNBnetentry-1].inb_of_entry;
	gFlow=(struct net_flow_item*)rte_zmalloc("net_flow_mem",sizeof(struct net_flow_item)*gNBFlow,0);
	if(!gFlow)
		printf(">>>:can not allocate %d bytes for net flow\n",(int)sizeof(struct net_flow_item)*gNBFlow);
	gFlow[0].b_flow_enabled=FALSE;//0-index disabled
	
	for(idx=0;idx<gNBnetentry;idx++)
	{//initialize net flow item 
		for(idx_sub=1;idx_sub<=gNetEntry[idx].inb_of_entry;idx_sub++)
		{
			idx_item=gNetEntry[idx].ientry_point+idx_sub;
			gFlow[idx_item].uiInnerIP=(gNetEntry[idx].uiIP&gNetEntry[idx].uiMask)+idx_sub;
			gFlow[idx_item].b_flow_enabled=TRUE;
			gFlow[idx_item].b_mac_learned=FALSE;
			gFlow[idx_item].b_preempty_enabled=TRUE;
			gFlow[idx_item].b_dynamic=TRUE;
			gFlow[idx_item].b_tb_initialized=FALSE;
			#if 0
			#endif
		}
	}
	//set static net entry which even triggled by arp reqs pkt,the entry will never be altered
	//note: we should directed broadcast address will be prohibited 
	for(idx=0;idx<gNBMACExcept;idx++)
	{
		idx_item=find_net_entry(gMACIPExcept[idx].uip,1);
	//	printf("mac_ex:%08x\n",gFlow[idx_item].uiInnerIP);
		if(idx_item==-1)
			continue;
		//printf("mac_ex:%08x\n",gFlow[idx_item].uiInnerIP);
		COPYMAC(gFlow[idx_item].eaHostMAC.addr_bytes,gMACIPExcept[idx].uimac);
		gFlow[idx_item].portid=gMACIPExcept[idx].uiport;
		gFlow[idx_item].b_dynamic=FALSE;
		gFlow[idx_item].b_mac_learned=TRUE;
		gFlow[idx_item].b_preempty_enabled=FALSE;
		gFlow[idx_item].b_flow_enabled=TRUE;
	}
	return STAT_SUCCESS;
}
int global_configurate(void)
{//for debug purpose we designate mannually ,later in release edition,config from .conf file

//#define CONF_DEBUG

	#ifndef CONF_DEBUG

	char szBuffer[256];
	char szModName[64];
	char szKey[128];
	char szVal[128];
	char *lptr,*lplow,*lphigh;;
	int idx,idx_buff;
	int iptr=0;
	FILE* fp=fopen(CONF_FILE_NAME,"r");
	if(!fp){
		printf(">>>error:can not open conf file:%s\n",CONF_FILE_NAME);
		return -1;
	}
	while(!feof(fp))
	{
		memset(szBuffer,0x0,sizeof(szBuffer));
		memset(szModName,0x0,sizeof(szModName));
		fgets(szBuffer,sizeof(szBuffer),fp);
		for(lptr=szBuffer;*lptr;lptr++)//trim right most \a char
			if(*lptr=='\x0a'){
				*lptr='\0';
				break;
			}
		//get identifier
		for(lptr=szBuffer,iptr=0;*lptr;lptr++){
			if(isalpha(*lptr)||isdigit(*lptr)||*lptr=='-'||*lptr=='_')
				szModName[iptr++]=*lptr;
			else break;
			}
		if(!strcmp(szModName,"port")){//resolve port segment
			int iportid=-1;
			printf(">>>port configuring... ...\n");
			while(!feof(fp)){
				memset(szBuffer,0x0,sizeof(szBuffer));
				memset(szKey,0x0,sizeof(szKey));
				memset(szVal,0x0,sizeof(szVal));	
				fgets(szBuffer,sizeof(szBuffer),fp);
				lptr=strstr(szBuffer,":");
				if(!lptr)
					goto loop_check;
				for(lplow=szBuffer;(*lplow)&&(*lplow!='"');lplow++);lplow++;
				for(iptr=0;*lplow&&*lplow!='"';lplow++)szKey[iptr++]=*lplow;
				for(lphigh=lptr++;(*lphigh)&&(*lphigh!='"');lphigh++);lphigh++;
				for(iptr=0;*lphigh&&*lphigh!='"';lphigh++)szVal[iptr++]=*lphigh;
				if(!strcmp(szKey,"portid")){
					iportid=atoi(szVal);
					gPortParaConf[iportid].portid=iportid;
					printf("   %-15s:%-d\n","iportid",iportid);
					rte_atomic64_init(&gPortParaConf[iportid].sd_port.ui_cnt_rx);//=0;
					rte_atomic64_init(&gPortParaConf[iportid].sd_port.ui_cnt_tx);//=0;
					rte_atomic64_init(&gPortParaConf[iportid].sd_port.ui_byte_rx);//=0;
					rte_atomic64_init(&gPortParaConf[iportid].sd_port.ui_byte_tx);//=0;
					rte_atomic64_init(&gPortParaConf[iportid].sd_port.ui_cnt_timer_rx);//=0;
					rte_atomic64_init(&gPortParaConf[iportid].sd_port.ui_cnt_timer_tx);//=0;
					rte_atomic64_init(&gPortParaConf[iportid].sd_port.ui_cnt_timer_last);//=0;
					rte_atomic64_init(&gPortParaConf[iportid].sd_port.ui_cnt_timer_cur);//=rte_rdtsc();
					int64_t i64_tmp=(int64_t)rte_rdtsc();
					rte_atomic64_set(&gPortParaConf[iportid].sd_port.ui_cnt_timer_cur,i64_tmp);
				}else if(!strcmp(szKey,"nb_rxd")){
					if(iportid!=-1){
						gPortParaConf[iportid].nb_rxd=atoi(szVal);
						printf("   %-15s:%-d\n","nb_rxd",gPortParaConf[iportid].nb_rxd);
						}
				}else if(!strcmp(szKey,"nb_txd")){
					if(iportid!=-1){
						gPortParaConf[iportid].nb_txd=atoi(szVal);
						printf("   %-15s:%-d\n","nb_txd",gPortParaConf[iportid].nb_txd);
						}
				}else if(!strcmp(szKey,"port_role")){
					if(iportid!=-1){
						gPortParaConf[iportid].ePortRole=
							(!strcmp(szVal,"PORT_ROLE_PE"))?PORT_ROLE_PE:(
							(!strcmp(szVal,"PORT_ROLE_CE"))?PORT_ROLE_CE:(
							(!strcmp(szVal,"PORT_ROLE_INTERIOR"))?PORT_ROLE_INTERIOR:PORT_ROLE_EXTERIOR));
						printf("   %-15s:%-d\n","port_role",gPortParaConf[iportid].ePortRole);
					}
				}else if(!strcmp(szKey,"port_grp")){
					if(iportid!=-1){
						gPortParaConf[iportid].port_grp=atoi(szVal);
						printf("   %-15s:%-d\n","port_group",gPortParaConf[iportid].port_grp);
						}
				}else if(!strcmp(szKey,"enabled")){
					if(iportid!=-1){
						gPortParaConf[iportid].bEnabled=(!strcmp(szVal,"true"))?TRUE:FALSE;
						printf("   %-15s:%-d\n","port_enabled",gPortParaConf[iportid].bEnabled);
						}
				}else if(!strcmp(szKey,"nb_tx_ring")){
					if(iportid!=-1){
						gPortParaConf[iportid].inb_tx_ring=atoi(szVal);
						printf("   %-15s:%-d\n","nb_of_tx-ring",gPortParaConf[iportid].inb_tx_ring);
						}
				}else if(!strcmp(szKey,"tx_ring_conf")){
					if(iportid!=-1){
						int iring_idx;
						int iring_len;
						int iring_div;
						lptr=szVal;
						iring_idx=atoi(lptr);
						while(*lptr&&*lptr!=',')lptr++;lptr++;
						iring_len=atoi(lptr);
						while(*lptr&&*lptr!=',')lptr++;lptr++;
						iring_div=atoi(lptr);
						gPortParaConf[iportid].itx_ring_nb_arr[iring_idx]=iring_len;
						gPortParaConf[iportid].itx_ring_div_factor[iring_idx]=iring_div;
						printf("   %-15s:%d,%d,%d\n","conf_of_tx-ring",iring_idx,gPortParaConf[iportid].itx_ring_nb_arr[iring_idx],gPortParaConf[iportid].itx_ring_div_factor[iring_idx]);
					}
				}
				
				loop_check:
				if(strstr(szBuffer,"]")){
					printf("\n");
					break;
					}
			}
		}
		else if(!strcmp(szModName,"lcore")){
			int ilcoreid=-1;
			printf(">>>lcore configuring... ...\n");
			while(!feof(fp)){
				memset(szBuffer,0x0,sizeof(szBuffer));
				memset(szKey,0x0,sizeof(szKey));
				memset(szVal,0x0,sizeof(szVal));	
				fgets(szBuffer,sizeof(szBuffer),fp);
				lptr=strstr(szBuffer,":");
				if(!lptr)
					goto loop_check1;
				for(lplow=szBuffer;(*lplow)&&(*lplow!='"');lplow++);lplow++;
				for(iptr=0;*lplow&&*lplow!='"';lplow++)szKey[iptr++]=*lplow;
				for(lphigh=lptr++;(*lphigh)&&(*lphigh!='"');lphigh++);lphigh++;
				for(iptr=0;*lphigh&&*lphigh!='"';lphigh++)szVal[iptr++]=*lphigh;

				if(!strcmp(szKey,"lcoreid")){
					ilcoreid=atoi(szVal);
					gLcoreParaConf[ilcoreid].lcoreid=ilcoreid;
					printf("   %-15s:%-d\n","lcoreid",ilcoreid);
				}else if(!strcmp(szKey,"enabled")){
					if(ilcoreid!=-1){
						gLcoreParaConf[ilcoreid].bEnabled=(!strcmp(szVal,"true"))?TRUE:FALSE;
						printf("   %-15s:%-d\n","enabled",gLcoreParaConf[ilcoreid].bEnabled);
						}
				}
				else if(!strcmp(szKey,"lcore_role")){
					if(ilcoreid!=-1){
						gLcoreParaConf[ilcoreid].eLcoreRole=(!strcmp(szVal,"LCORE_ROLE_RECV"))?LCORE_ROLE_RECV:
						((!strcmp(szVal,"LCORE_ROLE_SCHED"))?LCORE_ROLE_SCHED:LCORE_ROLE_SEND);
						printf("   %-15s:%-d\n","lcore_role",gLcoreParaConf[ilcoreid].eLcoreRole);
						}
				}
				else if(!strcmp(szKey,"inner_str")){
					if(ilcoreid!=-1){
						int iarr[16];
						int iarridx=0;
						lptr=szVal;
						do{
							iarr[iarridx++]=atoi(lptr);
							while(*lptr&&isdigit(*lptr))lptr++;
							if(*lptr)lptr++;
							else break;
						}while(*lptr);
						switch(gLcoreParaConf[ilcoreid].eLcoreRole)
						{
							case LCORE_ROLE_RECV:
								printf("   %-15s:","str_of_rx");
								gLcoreParaConf[ilcoreid].jobentry=lcore_rx_job_entry;
								gLcoreParaConf[ilcoreid].lparg=(void*)&gLcoreParaConf[ilcoreid];
								gLcoreParaConf[ilcoreid].rx_inner_str.inb_port=iarridx;
								for(idx=0;idx<iarridx;idx++){
									gLcoreParaConf[ilcoreid].rx_inner_str.iport_arr[idx]=iarr[idx];
									printf("%d,",gLcoreParaConf[ilcoreid].rx_inner_str.iport_arr[idx]);
									}
								puts("");
								break;
							case LCORE_ROLE_SCHED:
								gLcoreParaConf[ilcoreid].jobentry=lcore_sched_job_entry;
								gLcoreParaConf[ilcoreid].lparg=(void*)&gLcoreParaConf[ilcoreid];
								gLcoreParaConf[ilcoreid].qos_inner_str.sched_mod_ptr=&sched_mod_list[iarr[0]];
								printf("   %-15s:%d\n","str_of_sched",iarr[0]);
								break;
							case LCORE_ROLE_SEND:
								printf("   %-15s:","str_of_tx");
								gLcoreParaConf[ilcoreid].jobentry=lcore_tx_job_entry;
								gLcoreParaConf[ilcoreid].lparg=(void*)&gLcoreParaConf[ilcoreid];
								gLcoreParaConf[ilcoreid].tx_inner_str.inb_port=iarridx;
								for(idx=0;idx<iarridx;idx++){
									gLcoreParaConf[ilcoreid].tx_inner_str.iport_arr[idx]=iarr[idx];
									printf("%d,",gLcoreParaConf[ilcoreid].tx_inner_str.iport_arr[idx]);
									}
								puts("");
								break;
							default:
								break;
						}
					}
				}


				//printf("   %-15s:%-s\n",szKey,szVal);
				loop_check1:
				if(strstr(szBuffer,"]")){
					puts("");
					break;
				}
			}
		}
		else if(!strcmp(szModName,"netentry")){
			int ientry_idx=-1;
			printf(">>>net-entry configuring... ...\n");
			while(!feof(fp)){
				memset(szBuffer,0x0,sizeof(szBuffer));
				memset(szKey,0x0,sizeof(szKey));
				memset(szVal,0x0,sizeof(szVal));	
				fgets(szBuffer,sizeof(szBuffer),fp);
				lptr=strstr(szBuffer,":");
				if(!lptr)
					goto loop_check2;
				for(lplow=szBuffer;(*lplow)&&(*lplow!='"');lplow++);lplow++;
				for(iptr=0;*lplow&&*lplow!='"';lplow++)szKey[iptr++]=*lplow;
				for(lphigh=lptr++;(*lphigh)&&(*lphigh!='"');lphigh++);lphigh++;
				for(iptr=0;*lphigh&&*lphigh!='"';lphigh++)szVal[iptr++]=*lphigh;
				if(!strcmp(szKey,"nb_of_entry")){
					gNBnetentry=atoi(szVal);
					ientry_idx=0;
					printf("   %-15s:%-d\n","nb_of_entry",gNBnetentry);
				}else if(!strcmp(szKey,"entry")){
					lptr=strstr(szVal,",");
					if(!lptr) goto loop_check2;
					lplow=szVal;
					*lptr='\0';
					lphigh=lptr+1;
					gNetEntry[ientry_idx].uiIP=HTONL(INET_ADDR(lplow));
					gNetEntry[ientry_idx].uiMask=HTONL(INET_ADDR(lphigh));
					ientry_idx++;
					*lptr=',';
					printf("   %-15s:%08x:%08x\n","entry",gNetEntry[ientry_idx-1].uiIP,gNetEntry[ientry_idx-1].uiMask);
				}
				//printf("   %-15s:%-s\n",szKey,szVal);
				loop_check2:
				if(strstr(szBuffer,"]")){
					puts("");
					break;
				}
			}
		}
		else if(!strcmp(szModName,"exception_entry")){
			int ientry_idx=-1;
			printf(">>>exception_entry configuring... ...\n");
			while(!feof(fp)){
				memset(szBuffer,0x0,sizeof(szBuffer));
				memset(szKey,0x0,sizeof(szKey));
				memset(szVal,0x0,sizeof(szVal));	
				fgets(szBuffer,sizeof(szBuffer),fp);
				lptr=strstr(szBuffer,":");
				if(!lptr)
					goto loop_check3;
				for(lplow=szBuffer;(*lplow)&&(*lplow!='"');lplow++);lplow++;
				for(iptr=0;*lplow&&*lplow!='"';lplow++)szKey[iptr++]=*lplow;
				for(lphigh=lptr++;(*lphigh)&&(*lphigh!='"');lphigh++);lphigh++;
				for(iptr=0;*lphigh&&*lphigh!='"';lphigh++)szVal[iptr++]=*lphigh;
				if(!strcmp(szKey,"nb_of_entry")){
					gNBMACExcept=atoi(szVal);
					ientry_idx=0;
					printf("   %-15s:%-d\n","nb_of_entry",gNBMACExcept);
				}else if(!strcmp(szKey,"entry")){
				
					lptr=strstr(szVal,",");
					if(!lptr)
						goto loop_check3;
					*lptr='\0';
					lplow=lptr+1;//mac start
					lphigh=strstr(lplow,",");
					if(!lphigh)
						goto loop_check3;
					*lphigh='\0';
					lphigh++;
					gMACIPExcept[ientry_idx].uip=HTONL(INET_ADDR(szVal));
					FormatMACAddress((char*)gMACIPExcept[ientry_idx].uimac,lplow);
					gMACIPExcept[ientry_idx].uiport=atoi(lphigh);
					ientry_idx++;
					lplow[-1]=',';
					lphigh[-1]=',';
					printf("   %-15s:%08x-%02x:%02x:%02x:%02x:%02x:%02x-%d\n","entry",gMACIPExcept[ientry_idx-1].uip,
						gMACIPExcept[ientry_idx-1].uimac[0],
						gMACIPExcept[ientry_idx-1].uimac[1],
						gMACIPExcept[ientry_idx-1].uimac[2],
						gMACIPExcept[ientry_idx-1].uimac[3],
						gMACIPExcept[ientry_idx-1].uimac[4],
						gMACIPExcept[ientry_idx-1].uimac[5],
						gMACIPExcept[ientry_idx-1].uiport);
					
					
				}
				//printf("   %-15s:%-s\n",szKey,szVal);
				loop_check3:
				if(strstr(szBuffer,"]")){
					puts("");
					break;
				}
			}
		}
		else if(!strcmp(szModName,"port_policy_map")){
			int iport1,iport2;
			printf(">>>port map policy configuring... ...\n");
			while(!feof(fp)){
				memset(szBuffer,0x0,sizeof(szBuffer));
				memset(szKey,0x0,sizeof(szKey));
				memset(szVal,0x0,sizeof(szVal));	
				fgets(szBuffer,sizeof(szBuffer),fp);
				lptr=strstr(szBuffer,":");
				if(!lptr)
					goto loop_check4;
				for(lplow=szBuffer;(*lplow)&&(*lplow!='"');lplow++);lplow++;
				for(iptr=0;*lplow&&*lplow!='"';lplow++)szKey[iptr++]=*lplow;
				for(lphigh=lptr++;(*lphigh)&&(*lphigh!='"');lphigh++);lphigh++;
				for(iptr=0;*lphigh&&*lphigh!='"';lphigh++)szVal[iptr++]=*lphigh;
				if(!strcmp(szKey,"init")){
					for(idx=0;idx<MAX_PORT_NB;idx++)
						for(idx_buff=0;idx_buff<MAX_PORT_NB;idx_buff++)
							port_policy_map[idx][idx_buff]=PORT_POLICY_MAP_DIRECT;
						printf("   %-15s:%-s\n","initialzing","dummy");
				}else if(!strcmp(szKey,"entry")){
					lplow=strstr(szVal,",");
					if(!lplow)
						goto loop_check4;
					*lplow++='\0';
					lphigh=strstr(lplow,",");
					if(!lphigh)
						goto loop_check4;
					*lphigh++='\0';
					iport1=atoi(szVal);
					iport2=atoi(lplow);
					port_policy_map[iport1][iport2]=(!strcmp(lphigh,"PORT_POLICY_MAP_QOS"))?PORT_POLICY_MAP_QOS:PORT_POLICY_MAP_DIRECT;
					//printf("flag:%d\n",port_policy_map[iport1][iport2]);
					lplow[-1]=',';
					lphigh[-1]=',';
					printf("   %-15s:[%d,%d,%d]\n","entry",iport1,iport2,port_policy_map[iport1][iport2]);
				}
				//printf("   %-15s:%-s\n",szKey,szVal);
				loop_check4:
				if(strstr(szBuffer,"]")){
					puts("");
					break;
				}
			}
		}
		else if(!strcmp(szModName,"sched_mod_list")){
			int imodid=-1;
			printf(">>>sched mod list configuring... ...\n");
			while(!feof(fp)){
				memset(szBuffer,0x0,sizeof(szBuffer));
				memset(szKey,0x0,sizeof(szKey));
				memset(szVal,0x0,sizeof(szVal));	
				fgets(szBuffer,sizeof(szBuffer),fp);
				lptr=strstr(szBuffer,":");
				if(!lptr)
					goto loop_check5;
				for(lplow=szBuffer;(*lplow)&&(*lplow!='"');lplow++);lplow++;
				for(iptr=0;*lplow&&*lplow!='"';lplow++)szKey[iptr++]=*lplow;
				for(lphigh=lptr++;(*lphigh)&&(*lphigh!='"');lphigh++);lphigh++;
				for(iptr=0;*lphigh&&*lphigh!='"';lphigh++)szVal[iptr++]=*lphigh;
				if(!strcmp(szKey,"modid")){
					imodid=atoi(szVal);
					sched_mod_list[imodid].modid=imodid;
					printf("   %-15s:%-d\n","modid",imodid);
				}else if(!strcmp(szKey,"enabled")){
					if(imodid!=-1){
						sched_mod_list[imodid].bEnabled=(!strcmp(szVal,"true"))?TRUE:FALSE;
						printf("   %-15s:%-d\n","enabled",sched_mod_list[imodid].bEnabled);
						}
				}else if(!strcmp(szKey,"1st-que-len")){
					if(imodid!=-1){
						sched_mod_list[imodid].iFirLevRingLength=atoi(szVal);
						printf("   %-15s:%-d\n","1st-que-len",sched_mod_list[imodid].iFirLevRingLength);
						}
				}else if(!strcmp(szKey,"2nd-que-len")){
					if(imodid!=-1){
						sched_mod_list[imodid].iSecLevRingLength=atoi(szVal);
						printf("   %-15s:%-d\n","2nd-que-len",sched_mod_list[imodid].iSecLevRingLength);
						}
				}
				//printf("   %-15s:%-s\n",szKey,szVal);
				loop_check5:
				if(strstr(szBuffer,"]")){
					puts("");
					break;
				}
			}
		}
	}

	#else
	//#if 0 
	int idx,idx_buff;
	int64_t i64_tmp;
	
	for(idx=0;idx<gNBPort;idx++)
	{
		gPortParaConf[idx].portid=idx;//NOTE:we make sure that .portid equal index of array,if we do'not need this port,we disable it by set .bEnabled to FALSE
		gPortParaConf[idx].nb_rxd=gnbRxd;
		gPortParaConf[idx].nb_txd=gnbTxd;
		//gPortParaConf[idx].ePortRole=(idx%2==0)?PORT_ROLE_INTERIOR:PORT_ROLE_EXTERIOR;
		//gPortParaConf[idx].port_grp=0;
		gPortParaConf[idx].bEnabled=TRUE;
		gPortParaConf[idx].inb_tx_ring=3;//we use FIFO based queue schedual,here
		gPortParaConf[idx].itx_ring_nb_arr[0]=2048;//2K queue length for debug
		gPortParaConf[idx].itx_ring_nb_arr[1]=2048;
		gPortParaConf[idx].itx_ring_nb_arr[2]=2048;
		gPortParaConf[idx].itx_ring_div_factor[0]=1;
		gPortParaConf[idx].itx_ring_div_factor[1]=2;
		gPortParaConf[idx].itx_ring_div_factor[2]=3;

		
		rte_atomic64_init(&gPortParaConf[idx].sd_port.ui_cnt_rx);//=0;
		rte_atomic64_init(&gPortParaConf[idx].sd_port.ui_cnt_tx);//=0;
		rte_atomic64_init(&gPortParaConf[idx].sd_port.ui_byte_rx);//=0;
		rte_atomic64_init(&gPortParaConf[idx].sd_port.ui_byte_tx);//=0;
		rte_atomic64_init(&gPortParaConf[idx].sd_port.ui_cnt_timer_rx);//=0;
		rte_atomic64_init(&gPortParaConf[idx].sd_port.ui_cnt_timer_tx);//=0;
		rte_atomic64_init(&gPortParaConf[idx].sd_port.ui_cnt_timer_last);//=0;
		rte_atomic64_init(&gPortParaConf[idx].sd_port.ui_cnt_timer_cur);//=rte_rdtsc();
		i64_tmp=(int64_t)rte_rdtsc();
		rte_atomic64_set(&gPortParaConf[idx].sd_port.ui_cnt_timer_cur,i64_tmp);
		
	}
	gPortParaConf[0].ePortRole=PORT_ROLE_CE;
	gPortParaConf[1].ePortRole=PORT_ROLE_PE;
	//gPortParaConf[2].ePortRole=PORT_ROLE_PE;
	//gPortParaConf[3].ePortRole=PORT_ROLE_CE;

	gPortParaConf[0].port_grp=1;
	gPortParaConf[1].port_grp=1;
	//gPortParaConf[2].port_grp=1;
	//gPortParaConf[3].port_grp=1;
	
	gLcoreParaConf[0].bEnabled=FALSE;//master lcore is disabled
	gLcoreParaConf[1].bEnabled=TRUE;
	gLcoreParaConf[1].lcoreid=1;
	gLcoreParaConf[1].eLcoreRole=LCORE_ROLE_RECV;
	gLcoreParaConf[1].jobentry=lcore_rx_job_entry;
	gLcoreParaConf[1].lparg=(void*)&gLcoreParaConf[1];
	gLcoreParaConf[1].rx_inner_str.inb_port=2;
	gLcoreParaConf[1].rx_inner_str.iport_arr[0]=0;
	gLcoreParaConf[1].rx_inner_str.iport_arr[1]=1;
	//gLcoreParaConf[1].rx_inner_str.iport_arr[2]=2;
	//gLcoreParaConf[1].rx_inner_str.iport_arr[3]=3;
		
	gLcoreParaConf[2].bEnabled=TRUE;
	gLcoreParaConf[2].lcoreid=2;
	gLcoreParaConf[2].eLcoreRole=LCORE_ROLE_SCHED;
	gLcoreParaConf[2].jobentry=lcore_sched_job_entry;
	gLcoreParaConf[2].lparg=(void*)&gLcoreParaConf[2];
	gLcoreParaConf[2].qos_inner_str.sched_mod_ptr=&sched_mod_list[0];

	gLcoreParaConf[3].bEnabled=TRUE;
	gLcoreParaConf[3].lcoreid=3;
	gLcoreParaConf[3].eLcoreRole=LCORE_ROLE_SEND;
	gLcoreParaConf[3].jobentry=lcore_tx_job_entry;
	gLcoreParaConf[3].lparg=(void*)&gLcoreParaConf[3];
	gLcoreParaConf[3].tx_inner_str.inb_port=2;
	gLcoreParaConf[3].tx_inner_str.iport_arr[0]=0;
	gLcoreParaConf[3].tx_inner_str.iport_arr[1]=1;
	//gLcoreParaConf[3].tx_inner_str.iport_arr[2]=2;
	//gLcoreParaConf[3].tx_inner_str.iport_arr[3]=3;

	//notice:byte order,little endian
	gNetEntry[0].uiIP=HTONL(INET_ADDR("130.140.22.35"));//130.140.22.35/16
	gNetEntry[0].uiMask=HTONL(INET_ADDR("255.255.0.0"));
	gNetEntry[1].uiIP=HTONL(INET_ADDR("192.168.0.1"));//192.168.0.1/24
	gNetEntry[1].uiMask=HTONL(INET_ADDR("255.255.255.0"));
	gNetEntry[2].uiIP=HTONL(INET_ADDR("172.16.17.0"));//172.16.17.0/20
	gNetEntry[2].uiMask=HTONL(INET_ADDR("255.255.240.0"));
	gNetEntry[3].uiIP=HTONL(INET_ADDR("10.10.10.3"));//10.10.10.3/28
	gNetEntry[3].uiMask=HTONL(INET_ADDR("255.255.255.240"));
	gNBnetentry=4;


	//exeception table will be static ,when netentry is called
	gMACIPExcept[0].uip=HTONL(INET_ADDR("192.168.0.5"));
	gMACIPExcept[0].uimac[0]='\x00';
	gMACIPExcept[0].uimac[1]='\x00';
	gMACIPExcept[0].uimac[2]='\x00';
	gMACIPExcept[0].uimac[3]='\x00';
	gMACIPExcept[0].uimac[4]='\x00';
	gMACIPExcept[0].uimac[5]='\x55';
	gMACIPExcept[0].uiport=1;

	gMACIPExcept[1].uip=HTONL(INET_ADDR("192.168.0.6"));
	gMACIPExcept[1].uimac[0]='\x12';
	gMACIPExcept[1].uimac[1]='\x34';
	gMACIPExcept[1].uimac[2]='\x56';
	gMACIPExcept[1].uimac[3]='\x78';
	gMACIPExcept[1].uimac[4]='\x9a';
	gMACIPExcept[1].uimac[5]='\xbc';
	gMACIPExcept[1].uiport=0;
	
	gNBMACExcept=2;
	
	//port_policy_map initializization which decides how  traffic  between port pairs will handled
	for(idx=0;idx<MAX_PORT_NB;idx++)
		for(idx_buff=0;idx_buff<MAX_PORT_NB;idx_buff++)
			port_policy_map[idx][idx_buff]=PORT_POLICY_MAP_DIRECT;

	
	port_policy_map[0][1]=PORT_POLICY_MAP_QOS;
	port_policy_map[1][0]=PORT_POLICY_MAP_QOS;

	//port_policy_map[1][2]=PORT_POLICY_MAP_QOS;
	//port_policy_map[2][1]=PORT_POLICY_MAP_QOS;

	sched_mod_map[0][1]=0;
	sched_mod_map[1][0]=0;

	//sched_mod_map[1][2]=0;
	//sched_mod_map[2][1]=0;

	sched_mod_list[0].bEnabled=TRUE;
	sched_mod_list[0].iFirLevRingLength=2048;
	sched_mod_list[0].iSecLevRingLength=2048;
	sched_mod_list[0].modid=0;
	#endif
	//fclose(fp);
	return STAT_SUCCESS;
}
/*
functional description:
do some legality check,,apply policy and decide where packet we hand it to,direct forwarding or sched buffering or just discarding
author:jzheng
date:2014-5-28

*/
int lcore_rx_job_entry(dbg_local void* lparg)//entry for rx and classify
{
	dbg_local struct rte_mbuf* pkt_buff_arr[MAX_RX_BURST_NB];
	dbg_local int inb_pkt;
	dbg_local int iport_idx;
	dbg_local int ipkt_idx;
	dbg_local int portid;
	dbg_local int idx;
	dbg_local int lcoreid=rte_lcore_id();
	dbg_local struct lcore_usr_para_conf *lpLcoreUsrParaConf=(struct lcore_usr_para_conf *)lparg;
	dbg_local enum RX_MOD_INDEX irxmodid;
	printf(">>>:lcore %d RX on %d ports\n",lcoreid,lpLcoreUsrParaConf->rx_inner_str.inb_port);
	for(idx=0;idx<lpLcoreUsrParaConf->rx_inner_str.inb_port;idx++)
		printf("   :::::::port %d\n",lpLcoreUsrParaConf->rx_inner_str.iport_arr[idx]);
	//rx packet burst
	while(FOREVER)
	{
		for(iport_idx=0;iport_idx<lpLcoreUsrParaConf->rx_inner_str.inb_port;iport_idx++)
		{
			portid=lpLcoreUsrParaConf->rx_inner_str.iport_arr[iport_idx];
			inb_pkt=rte_eth_rx_burst((uint8_t)portid,0,pkt_buff_arr,MAX_RX_BURST_NB);//use burst-oriented RX API
			if(!inb_pkt)continue;
			//receive one(more than) packte(s)
			//packet-check-stack modules with module_ID
			for(ipkt_idx=0;ipkt_idx<inb_pkt;ipkt_idx++)
			{//before action-module ,we will  make sanity check to assure no packet will not action-targetd
				irxmodid=RX_MOD_L2DECAP;
				irxmodid=rx_module_l2decap(pkt_buff_arr[ipkt_idx],irxmodid);
				irxmodid=rx_module_arp(pkt_buff_arr[ipkt_idx],irxmodid);
				irxmodid=rx_module_ip(pkt_buff_arr[ipkt_idx],irxmodid);
				irxmodid=rx_module_ipv6(pkt_buff_arr[ipkt_idx],irxmodid);
				irxmodid=rx_module_forward(pkt_buff_arr[ipkt_idx],irxmodid);
				if(irxmodid!=RX_MOD_DROP&&irxmodid!=RX_MOD_FORWARD){//sanity check in case that packet would be not be targeted,,such leading mem-leak
					irxmodid=RX_MOD_DROP;
				}
				irxmodid=rx_module_drop(pkt_buff_arr[ipkt_idx],irxmodid);
			}
		}
	}
	return 0;
}
/*
functional description:
schedualer mod,which classify packets into net-flow,meter packets and color it,,next for forwarding or buffering for recoloring
author:jzheng
date:2014-5-28
*/
int lcore_sched_job_entry(dbg_local void *lparg)//entry for QoS schedual
{
	dbg_local uint64_t cnt_diff;
	dbg_local int rc;
	dbg_local int idx;
	dbg_local struct rte_mbuf* pktbuf[MAX_DEQUEUE_BURST_NB];
	dbg_local struct sched_stat_str ssSata;
	dbg_local int lcoreid=rte_lcore_id();
	dbg_local struct lcore_usr_para_conf *lpLcoreUsrParaConf=(struct lcore_usr_para_conf *)lparg;
	dbg_local struct qos_sched_mod *lpSchedPtr=lpLcoreUsrParaConf->qos_inner_str.sched_mod_ptr;
	printf(">>>:lcore %d QoS-sched on %d module\n",lcoreid,lpSchedPtr->modid);
	
	while(TRUE)
	{
		//PHAZE 1
		rc=rte_ring_sc_dequeue_burst(lpSchedPtr->rrFirLev,(void**)pktbuf,MAX_DEQUEUE_BURST_NB);
		//if(!rc)continue;
		for(idx=0;idx<rc;idx++)
		{
			sched_module_classify(pktbuf[idx],&ssSata);ssSata.isched_mod_id=lpSchedPtr->modid;//after this module handling,we set module id fields to keep track of which sched mod serves  
			sched_module_tb_init(pktbuf[idx],&ssSata);
			sched_module_metre(pktbuf[idx],&ssSata);ssSata.iPhaze=PHAZE_PRIMARY;
			sched_module_action(pktbuf[idx],&ssSata);
			sched_module_sanity_check(pktbuf[idx],&ssSata);
		}
		//PHAZE 2
		rc=rte_ring_sc_dequeue_burst(lpSchedPtr->rrFirLev,(void**)pktbuf,MAX_DEQUEUE_BURST_NB);//we may fetch half the BURST dequeue length,
		//if(!rc)continue;
		for(idx=0;idx<rc;idx++)
		{
			sched_module_classify(pktbuf[idx],&ssSata);ssSata.isched_mod_id=lpSchedPtr->modid;//after this module handling,we set module id fields to keep track of which sched mod serves  
			sched_module_tb_init(pktbuf[idx],&ssSata);
			sched_module_metre(pktbuf[idx],&ssSata);ssSata.iPhaze=PHAZE_SECONDARY;
			//printf("%d",(int)ssSata.tc_color);
			sched_module_action(pktbuf[idx],&ssSata);
			sched_module_sanity_check(pktbuf[idx],&ssSata);
		}
		
		
	}
	
	return 0;
}
/*
functional description:
TX job module,congestion-management buffering and forwarding,we use at least 2-levels-queue
that,GREEN pkts will enqueue into queue with highest priority,if encounter overflow,,we enqueue next queue with lower priority
YELLOW pkts will be directed to lower priority queue,discard pkts when overflow occurs
author:jzheng
date:2014-06-04
*/
int lcore_tx_job_entry(dbg_local void *lparg)//entry for congestion-mgn and tx
{	
	dbg_local int iring_idx;
	dbg_local int iportid;
	dbg_local int iport_idx;
	dbg_local int idx;
	dbg_local int rc;
	dbg_local int iSnd;
	#if 0
	dbg_local int ique;
	#endif
	dbg_local int64_t i64_tmp;
	dbg_local struct rte_mbuf* pktbuf[MAX_DEQUEUE_BURST_NB];
	dbg_local enum TRAFFIC_DIR enDir;
	dbg_local int iFlowIdx;
	dbg_local int lcoreid=rte_lcore_id();
	dbg_local struct port_usr_para_conf* lpPortUsrParaConf;
	dbg_local struct lcore_usr_para_conf *lpLcoreUsrParaConf=(struct lcore_usr_para_conf *)lparg;
	printf(">>>:lcore %d TX on %d ports\n",lcoreid,lpLcoreUsrParaConf->rx_inner_str.inb_port);
	for(idx=0;idx<lpLcoreUsrParaConf->tx_inner_str.inb_port;idx++)
		printf("   :::::::port %d\n",lpLcoreUsrParaConf->tx_inner_str.iport_arr[idx]);
	while(FOREVER)
	{
		for(iport_idx=0;iport_idx<lpLcoreUsrParaConf->tx_inner_str.inb_port;iport_idx++)
		{
			iportid=lpLcoreUsrParaConf->tx_inner_str.iport_arr[iport_idx];
			lpPortUsrParaConf=&gPortParaConf[iportid];
			//we use multi-level queue buffering outgoing packets
			for(iring_idx=0;iring_idx<lpPortUsrParaConf->inb_tx_ring;iring_idx++)
			{
				rc=DequeueFromPortByQueueID(iportid,iring_idx,pktbuf);
				if(!rc)
					continue;
				iSnd=rte_eth_tx_burst(iportid,0,pktbuf,rc);
				for(idx=0;idx<iSnd;idx++){
					//statistic for port 
					rte_atomic64_inc(&lpPortUsrParaConf->sd_port.ui_cnt_tx);
					rte_atomic64_add(&lpPortUsrParaConf->sd_port.ui_byte_tx,pktbuf[idx]->pkt.pkt_len-sizeof(struct ether_hdr));
					rte_atomic64_add(&lpPortUsrParaConf->sd_port.ui_cnt_timer_tx,pktbuf[idx]->pkt.pkt_len-sizeof(struct ether_hdr));
					//statistic for net-flow
					enDir=GetFlowIndexInStatistic(pktbuf[idx],&iFlowIdx);
					switch(enDir)
					{
						case TRAFFIC_DIR_INBOUND:
							if(gFlow[iFlowIdx].b_sd_initialized==FALSE){
								rte_atomic64_init(&gFlow[iFlowIdx].sd_data.ui_cnt_rx);//=0;
								rte_atomic64_init(&gFlow[iFlowIdx].sd_data.ui_cnt_tx);//=0;
								rte_atomic64_init(&gFlow[iFlowIdx].sd_data.ui_byte_rx);//=0;
								rte_atomic64_init(&gFlow[iFlowIdx].sd_data.ui_byte_tx);//=0;
								rte_atomic64_init(&gFlow[iFlowIdx].sd_data.ui_cnt_timer_rx);//=0;
								rte_atomic64_init(&gFlow[iFlowIdx].sd_data.ui_cnt_timer_tx);//=0;
								rte_atomic64_init(&gFlow[iFlowIdx].sd_data.ui_cnt_timer_last);//=0;
								rte_atomic64_init(&gFlow[iFlowIdx].sd_data.ui_cnt_timer_cur);//=rte_rdtsc();
								i64_tmp=(int64_t)rte_rdtsc();
								rte_atomic64_set(&gFlow[iFlowIdx].sd_data.ui_cnt_timer_cur,i64_tmp);
								gFlow[iFlowIdx].b_sd_initialized=TRUE;
							}
							//update RX descriptpr
							rte_atomic64_inc(&gFlow[iFlowIdx].sd_data.ui_cnt_rx);
							rte_atomic64_add(&gFlow[iFlowIdx].sd_data.ui_byte_rx,pktbuf[idx]->pkt.pkt_len-sizeof(struct ether_hdr));
							rte_atomic64_add(&gFlow[iFlowIdx].sd_data.ui_cnt_timer_rx,pktbuf[idx]->pkt.pkt_len-sizeof(struct ether_hdr));
							
							break;
						case TRAFFIC_DIR_OUTBOUND:
							if(gFlow[iFlowIdx].b_sd_initialized==FALSE){
								rte_atomic64_init(&gFlow[iFlowIdx].sd_data.ui_cnt_rx);//=0;
								rte_atomic64_init(&gFlow[iFlowIdx].sd_data.ui_cnt_tx);//=0;
								rte_atomic64_init(&gFlow[iFlowIdx].sd_data.ui_byte_rx);//=0;
								rte_atomic64_init(&gFlow[iFlowIdx].sd_data.ui_byte_tx);//=0;
								rte_atomic64_init(&gFlow[iFlowIdx].sd_data.ui_cnt_timer_rx);//=0;
								rte_atomic64_init(&gFlow[iFlowIdx].sd_data.ui_cnt_timer_tx);//=0;
								rte_atomic64_init(&gFlow[iFlowIdx].sd_data.ui_cnt_timer_last);//=0;
								rte_atomic64_init(&gFlow[iFlowIdx].sd_data.ui_cnt_timer_cur);//=rte_rdtsc();
								i64_tmp=(int64_t)rte_rdtsc();
								rte_atomic64_set(&gFlow[iFlowIdx].sd_data.ui_cnt_timer_cur,i64_tmp);
								gFlow[iFlowIdx].b_sd_initialized=TRUE;
								}
							//update TX descriptor
							rte_atomic64_inc(&gFlow[iFlowIdx].sd_data.ui_cnt_tx);
							rte_atomic64_add(&gFlow[iFlowIdx].sd_data.ui_byte_tx,pktbuf[idx]->pkt.pkt_len-sizeof(struct ether_hdr));
							rte_atomic64_add(&gFlow[iFlowIdx].sd_data.ui_cnt_timer_tx,pktbuf[idx]->pkt.pkt_len-sizeof(struct ether_hdr));
							break;
						default:

							break;
					}
				}
					
				if(iSnd<rc)//here we will not reenqueue those sent and failed 
					FreePktsArray(&pktbuf[iSnd],rc-iSnd);
				
			}
			#if 0
			rc=DequeueFromPortQeueue(iportid,pktbuf);
			if(rc){
				iSnd=rte_eth_tx_burst(iportid,0,pktbuf,rc);
				if(iSnd<rc)
					FreePktsArray(&pktbuf[iSnd],rc-iSnd);
				
				if(iSnd<rc){//not multi-producer safe,,we do not use such re-enqueue policy
					ique=EnqueueIntoPortQueue(iportid,&pktbuf[iSnd],rc-iSnd);
					if(ique<(rc-iSnd)){
						FreePktsArray(&pktbuf[iSnd+ique],rc-iSnd-ique);
					}
					
				}
				
			}
			#endif
		}
	}
	return 0;
}
enum TRAFFIC_DIR GetFlowIndexInStatistic(struct rte_mbuf*pktbuf,int *iFlowIdx)
{
	enum TRAFFIC_DIR td=TRAFFIC_DIR_UNDEF;
	uint32_t uiSIP,uiDIP;
	int idx=-1;
	struct ether_hdr *eh=rte_pktmbuf_mtod(pktbuf,struct ether_hdr*);
	struct iphdr*iph;
	if(HTONS(eh->ether_type)!=0x0800)
		goto local_ret;
	iph=(struct iphdr *)(sizeof(struct ether_hdr)+(char*)eh);
	uiSIP=HTONL(iph->ip_src);
	uiDIP=HTONL(iph->ip_dst);
	idx=find_net_entry(uiSIP,TRUE);
	if(idx!=-1){
		*iFlowIdx=idx;
		td=TRAFFIC_DIR_OUTBOUND;
		goto local_ret;
	}
	idx=find_net_entry(uiDIP,TRUE);
	if(idx!=-1){
		*iFlowIdx=idx;
		td=TRAFFIC_DIR_INBOUND;
	}
	
	local_ret:
	return td;
}
int DequeueFromPortByQueueID(int iportid,int iqueueid,struct rte_mbuf**pktbuf)
{//bulk dequeue
	int iptr=0;
	int iFetchLength;
	struct port_usr_para_conf *lpPortUsrParaConf=&gPortParaConf[iportid];
	iFetchLength=MAX_DEQUEUE_BURST_NB/lpPortUsrParaConf->itx_ring_div_factor[iqueueid];
	iptr=rte_ring_sc_dequeue_burst(lpPortUsrParaConf->tx_ring_arr[iqueueid],(void**)(&pktbuf[iptr]),iFetchLength);
	return iptr;
}
int DequeueFromPortQeueue(int iportid,struct rte_mbuf**pktbuf)
{//fetch MAX_DEQUEUE_BURST_NB as possible,from queues of a port,bulk dequeue
	int iptr=0;
	int iLeft=MAX_DEQUEUE_BURST_NB;
	int rc;
	int idx;
	int inb;
	struct port_usr_para_conf *lpPortUsrParaConf=&gPortParaConf[iportid];
	for(idx=0;idx<lpPortUsrParaConf->inb_tx_ring;idx++)
	{
		inb=rte_ring_count(lpPortUsrParaConf->tx_ring_arr[idx]);
		rc=MIN(inb,iLeft);
		if(!rc)continue;
		rc=rte_ring_sc_dequeue_burst(lpPortUsrParaConf->tx_ring_arr[idx],(void**)(&pktbuf[iptr]),rc);
		iLeft-=rc;
		iptr+=rc;
		if(!iLeft)break;
	}
	return iptr;
	
}
int EnqueueIntoPortByQueueID(int iportid,int iqueueid_start,struct rte_mbuf*pktbuf)
{//single enqueue,if scuccess,return 1,otherwise ,0 is returned ,require queue is multi=producer
	int rc=0;
	int itmp,idx;
	struct port_usr_para_conf *lpPortUsrParaConf=&gPortParaConf[iportid];
	#if 0
	if(iqueueid_start>=lpPortUsrParaConf->inb_tx_ring)
		return 0;
	#endif
	for(idx=iqueueid_start;idx<lpPortUsrParaConf->inb_tx_ring;idx++)
	{
		itmp=rte_ring_mp_enqueue(lpPortUsrParaConf->tx_ring_arr[idx],pktbuf);
		if(itmp!=-ENOBUFS){//on success
			rc=1;
			break;
		}
	}
	return rc;
}
int EnqueueIntoPortQueue(int iportid,struct rte_mbuf**pktbuf,int inb )
{//bulk enqueue
	int idx;
	int iLeft=inb;
	int rc;
	int iptr=0;
	struct port_usr_para_conf *lpPortUsrParaConf=&gPortParaConf[iportid];
	for(idx=0;idx<lpPortUsrParaConf->inb_tx_ring;idx++)
	{
		rc=rte_ring_mp_enqueue_burst(lpPortUsrParaConf->tx_ring_arr[idx],(void**)(&pktbuf[iptr]),iLeft);
		iptr+=rc;
		iLeft-=rc;
		if(!iLeft)break;
		
	}
	return iptr;
}
//int ipkt=0;
int FreePktsArray(struct rte_mbuf**pktbuf,int inb)
{
	int idx;
	for(idx=0;idx<inb;idx++){
		//ipkt++;
		rte_pktmbuf_free(pktbuf[idx]);
		//if(ipkt%100==0)
	//	printf("%d\n",ipkt);
		}
	return inb;
}
/////////////////////////////////////////////uilities function//////////
////////////////////////////////////////////////////////////////////////
void SetNetEntry(uint32_t uIP,int isDownStream,uint32_t uiPIR,uint32_t uiCIR,uint32_t uiPBS,uint32_t uiCBS)
{//-1 indicate keeps value unchanged
	int iFlowIdx=find_net_entry(uIP,TRUE);
	if(iFlowIdx==-1){
		printf("...:configuration for [%08x] is not available\n",uIP);
		return ;
		}
	if(isDownStream){
		gFlow[iFlowIdx].tb_downstream.uiPIR=(!uiPIR)?gFlow[iFlowIdx].tb_downstream.uiPIR:uiPIR;
		gFlow[iFlowIdx].tb_downstream.uiCIR=(!uiCIR)?gFlow[iFlowIdx].tb_downstream.uiCIR:uiCIR;
		gFlow[iFlowIdx].tb_downstream.uiPBS=(!uiPBS)?gFlow[iFlowIdx].tb_downstream.uiPBS:uiPBS;
		gFlow[iFlowIdx].tb_downstream.uiCBS=(!uiCBS)?gFlow[iFlowIdx].tb_downstream.uiCBS:uiCBS;
	}else{
		gFlow[iFlowIdx].tb_uptream.uiPIR=(!uiPIR)?gFlow[iFlowIdx].tb_downstream.uiPIR:uiPIR;
		gFlow[iFlowIdx].tb_uptream.uiCIR=(!uiCIR)?gFlow[iFlowIdx].tb_downstream.uiCIR:uiCIR;
		gFlow[iFlowIdx].tb_uptream.uiPBS=(!uiPBS)?gFlow[iFlowIdx].tb_downstream.uiPBS:uiPBS;
		gFlow[iFlowIdx].tb_uptream.uiCBS=(!uiCBS)?gFlow[iFlowIdx].tb_downstream.uiCBS:uiCBS;
		}
	
}
void ShowPortStatistic(void){
	int idx;
	int64_t i64RxCnt,i64TxCnt;
	int64_t i64Rxbytes,i64Txbytes;
	int64_t i64RxRate,i64TxRate,i64RxPeriodCnt,i64TxPeriodCnt;
	int64_t i64TimerLast,i64TimerCur;
	struct port_usr_para_conf*lpPort;
	printf("portid %15s %15s %15s %15s %15s %15s\n","packets-in(K)","packets-out(K)","bytes-in(MB)","bytes-out(MB)","speed-in(bps)","speed-out(bps)" );
	for(idx=0;idx<gNBPort;idx++){
		if(gPortParaConf[idx].bEnabled==FALSE)continue;
		lpPort=&gPortParaConf[idx];
		i64RxCnt=rte_atomic64_read(&lpPort->sd_port.ui_cnt_rx);
		i64TxCnt=rte_atomic64_read(&lpPort->sd_port.ui_cnt_tx);
		i64Rxbytes=rte_atomic64_read(&lpPort->sd_port.ui_byte_rx);
		i64Txbytes=rte_atomic64_read(&lpPort->sd_port.ui_byte_tx);
		i64RxPeriodCnt=rte_atomic64_read(&lpPort->sd_port.ui_cnt_timer_rx);
		i64TxPeriodCnt=rte_atomic64_read(&lpPort->sd_port.ui_cnt_timer_tx);
		i64TimerLast=rte_atomic64_read(&lpPort->sd_port.ui_cnt_timer_last);
		i64TimerCur=(int64_t)rte_rdtsc();
		i64RxRate=i64RxPeriodCnt*8*TSC_SECOND/(i64TimerCur-i64TimerLast);
		i64TxRate=i64TxPeriodCnt*8*TSC_SECOND/(i64TimerCur-i64TimerLast);

		
		rte_atomic64_set(&lpPort->sd_port.ui_cnt_timer_last,i64TimerCur);
		rte_atomic64_set(&lpPort->sd_port.ui_cnt_timer_rx,0);
		rte_atomic64_set(&lpPort->sd_port.ui_cnt_timer_tx,0);

		printf("%6d %15"PRIu64" %15"PRIu64" %15"PRIu64" %15"PRIu64" %15"PRIu64" %15"PRIu64"\n",idx
			,i64RxCnt/1024,i64TxCnt/1024,
			i64Rxbytes/(1024*1024),i64Txbytes/(1024*1024),
			i64RxRate,i64TxRate);
	}
	puts("");
}
void ShowNetflowStatistic(uint32_t uIP){//UIP should be little-endian
	
	int64_t i64RxCnt,i64TxCnt;
	int64_t i64Rxbytes,i64Txbytes;
	int64_t i64RxRate,i64TxRate,i64RxPeriodCnt,i64TxPeriodCnt;
	int64_t i64TimerLast,i64TimerCur;
	int iFlowIdx=find_net_entry(uIP,TRUE);
	if(iFlowIdx==-1){
		printf("...:statistic data for [%08x] is not available\n",uIP);
		return ;
	}
	if(gFlow[iFlowIdx].b_sd_initialized==FALSE){
		printf("...:statistic data for [%08x] is not initialized\n",uIP);
		return ;
	}
	printf(" bound-mac:%02x:%02x:%02x:%02x:%02x:%02x port:%d\n",
		gFlow[iFlowIdx].eaHostMAC.addr_bytes[0],
		gFlow[iFlowIdx].eaHostMAC.addr_bytes[1],
		gFlow[iFlowIdx].eaHostMAC.addr_bytes[2],
		gFlow[iFlowIdx].eaHostMAC.addr_bytes[3],
		gFlow[iFlowIdx].eaHostMAC.addr_bytes[4],
		gFlow[iFlowIdx].eaHostMAC.addr_bytes[5],
		gFlow[iFlowIdx].portid);
	printf("downstream:PIR(bps):%-15"PRIu64"CIR(bps):%-15"PRIu64"PBS(b):%-15"PRIu64"CBS(b):%-15"PRIu64"\n"
		,(uint64_t)gFlow[iFlowIdx].tb_downstream.uiPIR*8
		,(uint64_t)gFlow[iFlowIdx].tb_downstream.uiCIR*8
		,(uint64_t)gFlow[iFlowIdx].tb_downstream.uiPBS*8
		,(uint64_t)gFlow[iFlowIdx].tb_downstream.uiCBS*8);
	printf("  upstream:PIR(bps):%-15"PRIu64"CIR(bps):%-15"PRIu64"PBS(b):%-15"PRIu64"CBS(b):%-15"PRIu64"\n"
		,(uint64_t)gFlow[iFlowIdx].tb_uptream.uiPIR*8
		,(uint64_t)gFlow[iFlowIdx].tb_uptream.uiCIR*8
		,(uint64_t)gFlow[iFlowIdx].tb_uptream.uiPBS*8
		,(uint64_t)gFlow[iFlowIdx].tb_uptream.uiCBS*8);
	printf("      IP %15s %15s %15s %15s\n","bytes-in(MB)","bytes-out(MB)","speed-in(bps)","speed-out(bps)" );

	i64RxCnt=rte_atomic64_read(&gFlow[iFlowIdx].sd_data.ui_cnt_rx);
	i64TxCnt=rte_atomic64_read(&gFlow[iFlowIdx].sd_data.ui_cnt_tx);
	i64Rxbytes=rte_atomic64_read(&gFlow[iFlowIdx].sd_data.ui_byte_rx);
	i64Txbytes=rte_atomic64_read(&gFlow[iFlowIdx].sd_data.ui_byte_tx);
	i64RxPeriodCnt=rte_atomic64_read(&gFlow[iFlowIdx].sd_data.ui_cnt_timer_rx);
	i64TxPeriodCnt=rte_atomic64_read(&gFlow[iFlowIdx].sd_data.ui_cnt_timer_tx);
	i64TimerLast=rte_atomic64_read(&gFlow[iFlowIdx].sd_data.ui_cnt_timer_last);
	i64TimerCur=(int64_t)rte_rdtsc();
	i64RxRate=i64RxPeriodCnt*8*TSC_SECOND/(i64TimerCur-i64TimerLast);
	i64TxRate=i64TxPeriodCnt*8*TSC_SECOND/(i64TimerCur-i64TimerLast);

	
	rte_atomic64_set(&gFlow[iFlowIdx].sd_data.ui_cnt_timer_last,i64TimerCur);
	rte_atomic64_set(&gFlow[iFlowIdx].sd_data.ui_cnt_timer_rx,0);
	rte_atomic64_set(&gFlow[iFlowIdx].sd_data.ui_cnt_timer_tx,0);

	printf("%08x %15"PRIu64" %15"PRIu64" %15"PRIu64" %15"PRIu64"\n",uIP
		,
		i64Rxbytes/(1024*1024),i64Txbytes/(1024*1024),
		i64RxRate,i64TxRate);

	puts("");
}
dbg_local void check_all_ports_link_status(uint8_t port_num, uint32_t port_mask)
{//imported from Intel DPDK Demo example
#define CHECK_INTERVAL 100 /* 100ms */
#define MAX_CHECK_TIME 90 /* 9s (90 * 100ms) in total */
	uint8_t portid, count, all_ports_up, print_flag = 0;
	struct rte_eth_link link;

	printf("\nChecking link status");
	fflush(stdout);
	for (count = 0; count <= MAX_CHECK_TIME; count++) {
		all_ports_up = 1;
		for (portid = 0; portid < port_num; portid++) {
			if ((port_mask & (1 << portid)) == 0)
				continue;
			memset(&link, 0, sizeof(link));
			rte_eth_link_get_nowait(portid, &link);
			/* print link status if flag set */
			if (print_flag == 1) {
				if (link.link_status)
					printf("Port %d Link Up - speed %u "
						"Mbps - %s\n", (uint8_t)portid,
						(unsigned)link.link_speed,
				(link.link_duplex == ETH_LINK_FULL_DUPLEX) ?
					("full-duplex") : ("half-duplex\n"));
				else
					printf("Port %d Link Down\n",
						(uint8_t)portid);
				continue;
			}
			/* clear all_ports_up flag if any link down */
			if (link.link_status == 0) {
				all_ports_up = 0;
				break;
			}
		}
		/* after finally printing all link status, get out */
		if (print_flag == 1)
			break;

		if (all_ports_up == 0) {
			printf(".");
			fflush(stdout);
			rte_delay_ms(CHECK_INTERVAL);
		}

		/* set the print_flag if all ports up or timeout */
		if (all_ports_up == 1 || count == (MAX_CHECK_TIME - 1)) {
			print_flag = 1;
			printf(" done\n");
		}
	}
}


//some utilities functional module
uint32_t INET_ADDR(const char*szIP)
{//ret network byte order,ie.big endian byte order
	uint32_t ret=0;
	int arr[4];
	sscanf(szIP,"%d.%d.%d.%d",&arr[0],&arr[1],&arr[2],&arr[3]);//built standard C lib
#if 0
	printf("flag:%d\n",arr[0]);
	printf("flag:%d\n",arr[1]);
	printf("flag:%d\n",arr[2]);
	printf("flag:%d\n",arr[3]);
#endif
	ret=(0xff000000&(arr[3]<<24))|
		(0x00ff0000&(arr[2]<<16))|
		(0x0000ff00&(arr[1]<<8))|
		(0x000000ff&(arr[0]));
	return ret;
}
int FormatMACAddress(char *szMAC,char *szStr)
{//requirments:internally we will not check args legality,caller should make right calling
	char *lptr=szStr;
	char chHi;
	char chLo;
	int idx=0;
	do{
		chHi=tolower(lptr[0]);
		chLo=tolower(lptr[1]);
		chHi=isdigit(chHi)?chHi-'0':chHi-'a'+10;
		chLo=isdigit(chLo)?chLo-'0':chLo-'a'+10;
		szMAC[idx++]=((chHi<<4)&0xf0)|(chLo&0xf);
		lptr+=3;
	}while(*lptr&&idx<6);
	return 0;
}
uint32_t HTONL(uint32_t uiIP)
{
	return (0xff000000&(uiIP<<24))|
		(0x00ff0000&(uiIP<<8))|
		(0x0000ff00&(uiIP>>8))|
		(0x000000ff&(uiIP>>24));
}
uint16_t HTONS(uint16_t usPort)
{

	return (0xff00&(usPort<<8))|
		(0x00ff&(usPort>>8));
}
int find_net_entry(uint32_t uip,int is_little_endian)
{//we  MUST know ,,directed broadcast address of last entry eauals current address with all zero in host-bit pos,we filter this out

	uint32_t uip_target=0,uip_tmp=0;
	int idx=0;
	uip=is_little_endian?uip:HTONL(uip);//transform to littlen endian if needed
	for(idx=0;idx<gNBnetentry;idx++)
	{
		uip_target=uip&gNetEntry[idx].uiMask;
		uip_tmp=gNetEntry[idx].uiIP&gNetEntry[idx].uiMask;
		if(uip_target==uip_tmp)break;
	}
	if(!(uip&~gNetEntry[idx].uiMask))//here we force this filter rule...
		return -1;
	if(idx<gNBnetentry)
		return (uip&~gNetEntry[idx].uiMask)+gNetEntry[idx].ientry_point;
	return -1;
}


