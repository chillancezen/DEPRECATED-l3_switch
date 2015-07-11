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

/*
funcational description:
schedual module ,extract some information about this flow
author:jzheng
date:2014-06-01
*/
int sched_module_classify(struct rte_mbuf*pktbuf,struct sched_stat_str *lpstat)
{
	dbg_local struct ether_hdr *eh;
	dbg_local struct ether_arp *ea;
	dbg_local struct iphdr *iph;
	dbg_local uint32_t uiSIP;
	dbg_local uint32_t uiDIP;
	
	memset(lpstat,0x0,sizeof(struct sched_stat_str));//reset all fields
	lpstat->iport_in=GETLONIB(pktbuf->pkt.in_port);
	lpstat->iport_out=GETHINIB(pktbuf->pkt.in_port);
	lpstat->iDiscard=FALSE;
	if(gPortParaConf[lpstat->iport_in].port_grp!=gPortParaConf[lpstat->iport_out].port_grp){
		lpstat->enDir=TRAFFIC_DIR_UNDEF;
	}else{
		lpstat->enDir=TRAFFIC_DIR_UNDEF;
		
		switch(gPortParaConf[lpstat->iport_in].ePortRole)
		{
			case PORT_ROLE_INTERIOR:
				if(gPortParaConf[lpstat->iport_out].ePortRole==PORT_ROLE_EXTERIOR)
					lpstat->enDir=TRAFFIC_DIR_OUTBOUND;
				break;
			case PORT_ROLE_EXTERIOR:
				if(gPortParaConf[lpstat->iport_out].ePortRole==PORT_ROLE_INTERIOR)
					lpstat->enDir=TRAFFIC_DIR_INBOUND;
				break;
			case PORT_ROLE_PE:
				if(gPortParaConf[lpstat->iport_out].ePortRole==PORT_ROLE_CE)
					lpstat->enDir=TRAFFIC_DIR_INBOUND;
				break;
			case PORT_ROLE_CE:
				if(gPortParaConf[lpstat->iport_out].ePortRole==PORT_ROLE_PE)
					lpstat->enDir=TRAFFIC_DIR_OUTBOUND;
				break;
		}
	}
	eh=rte_pktmbuf_mtod(pktbuf,struct ether_hdr *);
	lpstat->iPayloadLength=pktbuf->pkt.pkt_len-sizeof(struct ether_hdr);//payload length do not include datalink layer header
	switch(HTONS(eh->ether_type))
	{
		case ETH_TYPE_ARP:
			ea=(struct ether_arp *)(sizeof(struct ether_hdr)+(char*)eh);
			uiSIP=HTONL(MAKEUINT32FROMUINT8ARR(ea->arp_spa));
			uiDIP=HTONL(MAKEUINT32FROMUINT8ARR(ea->arp_tpa));
			lpstat->iPaketType=ETH_TYPE_ARP;
			break;
		case ETH_TYPE_IP:
			iph=(struct iphdr *)(sizeof(struct ether_hdr)+(char*)eh);
			uiSIP=HTONL(iph->ip_src);
			uiDIP=HTONL(iph->ip_dst);
			lpstat->iPaketType=ETH_TYPE_IP;
			break;
		default:
			uiSIP=uiDIP=0;
			lpstat->iPaketType=HTONS(eh->ether_type);
			break;
	}
	switch(lpstat->enDir)
	{
		case TRAFFIC_DIR_INBOUND:
			lpstat->iFlowIdx=find_net_entry(uiDIP,TRUE);
			break;
		case TRAFFIC_DIR_OUTBOUND:
			lpstat->iFlowIdx=find_net_entry(uiSIP,TRUE);
			break;
		default:
			lpstat->iDiscard=TRUE;
			lpstat->iFlowIdx=-1;
			break;
	}
	return 0;	
}
/*
functional description:
token-bucket initialization module
author:jzheng
date:2014-06-02
*/
int sched_module_tb_init(dbg_local struct rte_mbuf*pktbuf,struct sched_stat_str *lpstat)
{
	if(lpstat->iDiscard==TRUE)
		return -1;
	#if 0
	if(lpstat->iFlowIdx==-1)
		return -1;
	#endif
	if(gFlow[lpstat->iFlowIdx].b_tb_initialized==TRUE)
		return 1;
	gFlow[lpstat->iFlowIdx].tb_uptream.eMeterMode=COLOR_BLIND;
	gFlow[lpstat->iFlowIdx].tb_uptream.uiCBS=DEF_COMMIT_BURST_SIZE;
	gFlow[lpstat->iFlowIdx].tb_uptream.uiCIR=DEF_COMMIT_INFO_RATE;
	gFlow[lpstat->iFlowIdx].tb_uptream.uiPBS=DEF_PEEK_BURST_SIZE;
	gFlow[lpstat->iFlowIdx].tb_uptream.uiPIR=DEF_PEEK_INFO_RATE;
	gFlow[lpstat->iFlowIdx].tb_uptream.uiCToken=gFlow[lpstat->iFlowIdx].tb_uptream.uiCBS;
	gFlow[lpstat->iFlowIdx].tb_uptream.uiPToken=gFlow[lpstat->iFlowIdx].tb_uptream.uiPBS;
	gFlow[lpstat->iFlowIdx].tb_uptream.uiCntLast=rte_rdtsc();
	
	gFlow[lpstat->iFlowIdx].tb_downstream.eMeterMode=COLOR_BLIND;
	gFlow[lpstat->iFlowIdx].tb_downstream.uiCBS=DEF_COMMIT_BURST_SIZE;
	gFlow[lpstat->iFlowIdx].tb_downstream.uiCIR=DEF_COMMIT_INFO_RATE;
	gFlow[lpstat->iFlowIdx].tb_downstream.uiPBS=DEF_PEEK_BURST_SIZE;
	gFlow[lpstat->iFlowIdx].tb_downstream.uiPIR=DEF_PEEK_INFO_RATE;
	gFlow[lpstat->iFlowIdx].tb_downstream.uiCToken=gFlow[lpstat->iFlowIdx].tb_downstream.uiCBS;
	gFlow[lpstat->iFlowIdx].tb_downstream.uiPToken=gFlow[lpstat->iFlowIdx].tb_downstream.uiPBS;
	gFlow[lpstat->iFlowIdx].tb_downstream.uiCntLast=rte_rdtsc();

	gFlow[lpstat->iFlowIdx].b_tb_initialized=TRUE;
	return 0;
}

/*
functional description:
packet meters and colors
author:jzheng
date:2014-06-03
*/
int sched_module_metre(dbg_local struct rte_mbuf*pktbuf,struct sched_stat_str *lpstat)
{
	
	uint64_t ui_cnt_diff;
	uint64_t uiToken;
	if(lpstat->iDiscard==TRUE)
		return -1;
	
	#if 0
	if(lpstat->iFlowIdx==-1)
		return -1;
	#endif
	switch(lpstat->enDir)
	{
		case TRAFFIC_DIR_INBOUND://apply downstream QoS scheduler  
		
			gFlow[lpstat->iFlowIdx].tb_downstream.uiCntCur=rte_rdtsc();
			ui_cnt_diff=gFlow[lpstat->iFlowIdx].tb_downstream.uiCntCur-gFlow[lpstat->iFlowIdx].tb_downstream.uiCntLast;
			/*if((ui_cnt_diff*1000/TSC_SECOND)>=DEF_UPDATE_THRESHOLD_MS)*/{
				//update C token bucket
				uiToken=ui_cnt_diff*gFlow[lpstat->iFlowIdx].tb_downstream.uiCIR/TSC_SECOND;
				//printf("c:%"PRIu64",%"PRIu64"\n",(uint64_t)(ui_cnt_diff*1000/TSC_SECOND),uiToken);
				gFlow[lpstat->iFlowIdx].tb_downstream.uiCToken+=uiToken;
				gFlow[lpstat->iFlowIdx].tb_downstream.uiCToken=MIN(gFlow[lpstat->iFlowIdx].tb_downstream.uiPToken,gFlow[lpstat->iFlowIdx].tb_downstream.uiPBS);
				//update P token bucket
				uiToken=ui_cnt_diff*gFlow[lpstat->iFlowIdx].tb_downstream.uiPIR/TSC_SECOND;
				//printf("p:%"PRIu64"\n\n",uiToken);
				gFlow[lpstat->iFlowIdx].tb_downstream.uiPToken+=uiToken;
				gFlow[lpstat->iFlowIdx].tb_downstream.uiPToken=MIN(gFlow[lpstat->iFlowIdx].tb_downstream.uiPToken,gFlow[lpstat->iFlowIdx].tb_downstream.uiPBS);
				//update TSC counter
				gFlow[lpstat->iFlowIdx].tb_downstream.uiCntLast=gFlow[lpstat->iFlowIdx].tb_downstream.uiCntCur;
			}
			if(lpstat->iPayloadLength>gFlow[lpstat->iFlowIdx].tb_downstream.uiPToken){
				lpstat->tc_color=TC_RED;
			}else if(lpstat->iPayloadLength>gFlow[lpstat->iFlowIdx].tb_downstream.uiCToken){
				lpstat->tc_color=TC_YELLOW;
				gFlow[lpstat->iFlowIdx].tb_downstream.uiPToken-=lpstat->iPayloadLength;
			}else{
				lpstat->tc_color=TC_GREEN;
				gFlow[lpstat->iFlowIdx].tb_downstream.uiPToken-=lpstat->iPayloadLength;
				gFlow[lpstat->iFlowIdx].tb_downstream.uiCToken-=lpstat->iPayloadLength;
			}
			break;
		case TRAFFIC_DIR_OUTBOUND://apply upstream QoS scheduler
		
			gFlow[lpstat->iFlowIdx].tb_uptream.uiCntCur=rte_rdtsc();
			ui_cnt_diff=gFlow[lpstat->iFlowIdx].tb_uptream.uiCntCur-gFlow[lpstat->iFlowIdx].tb_uptream.uiCntLast;
			/*if((ui_cnt_diff*1000/TSC_SECOND)>=DEF_UPDATE_THRESHOLD_MS)*/{
				//update C token bucket
				uiToken=ui_cnt_diff*gFlow[lpstat->iFlowIdx].tb_uptream.uiCIR/TSC_SECOND;
				//printf("c:%"PRIu64",%"PRIu64"\n",(uint64_t)(ui_cnt_diff*1000/TSC_SECOND),uiToken);
				gFlow[lpstat->iFlowIdx].tb_uptream.uiCToken+=uiToken;
				gFlow[lpstat->iFlowIdx].tb_uptream.uiCToken=MIN(gFlow[lpstat->iFlowIdx].tb_uptream.uiPToken,gFlow[lpstat->iFlowIdx].tb_uptream.uiPBS);
				//update P token bucket
				uiToken=ui_cnt_diff*gFlow[lpstat->iFlowIdx].tb_uptream.uiPIR/TSC_SECOND;
				//printf("p:%"PRIu64"\n\n",uiToken);
				gFlow[lpstat->iFlowIdx].tb_uptream.uiPToken+=uiToken;
				gFlow[lpstat->iFlowIdx].tb_uptream.uiPToken=MIN(gFlow[lpstat->iFlowIdx].tb_uptream.uiPToken,gFlow[lpstat->iFlowIdx].tb_uptream.uiPBS);
				//update TSC counter
				gFlow[lpstat->iFlowIdx].tb_uptream.uiCntLast=gFlow[lpstat->iFlowIdx].tb_uptream.uiCntCur;
			}
			if(lpstat->iPayloadLength>gFlow[lpstat->iFlowIdx].tb_uptream.uiPToken){
				lpstat->tc_color=TC_RED;
			}else if(lpstat->iPayloadLength>gFlow[lpstat->iFlowIdx].tb_uptream.uiCToken){
				lpstat->tc_color=TC_YELLOW;
				gFlow[lpstat->iFlowIdx].tb_uptream.uiPToken-=lpstat->iPayloadLength;
			}else{
				lpstat->tc_color=TC_GREEN;
				gFlow[lpstat->iFlowIdx].tb_uptream.uiPToken-=lpstat->iPayloadLength;
				gFlow[lpstat->iFlowIdx].tb_uptream.uiCToken-=lpstat->iPayloadLength;
			}
			break;
		case TRAFFIC_DIR_UNDEF:
			
			break;
	}
	return 0;
}
/*
functional description:
scheduler action module,which acts as forwarding and(or) buffering and(or)discarding
author:jzheng
date:2014-06-03
*/

int sched_module_action(struct rte_mbuf*pktbuf,struct sched_stat_str*lpstat)
{
	int rc;
	#if 0
	if(lpstat->iFlowIdx==-1)
		return -1;
	#endif
	if(lpstat->iDiscard==TRUE)
		return -1;
	switch(lpstat->tc_color)
	{
		case TC_GREEN://enqueue pkt into queue with higher priority
			rc=EnqueueIntoPortByQueueID(lpstat->iport_out,0,pktbuf);
			if(!rc)
				lpstat->iDiscard=TRUE;
			break;
		case TC_YELLOW://enqueue pkt into queue with less priority
			rc=EnqueueIntoPortByQueueID(lpstat->iport_out,(gPortParaConf[lpstat->iport_out].inb_tx_ring>=2)?1:0,pktbuf);
			if(!rc)
				lpstat->iDiscard=TRUE;
			break;
		case TC_RED:
			switch(lpstat->iPhaze)
			{
				case PHAZE_PRIMARY://re-enqueue into secondary sched queue
					rc=rte_ring_mp_enqueue(sched_mod_list[lpstat->isched_mod_id].rrSecLev,pktbuf);
					if(rc==-ENOBUFS)
						lpstat->iDiscard=TRUE;
					break;
				case PHAZE_SECONDARY://discard re-metred and red-colored pkts 
					lpstat->iDiscard=TRUE;
					break;
				default://may this case will never occur
					lpstat->iDiscard=TRUE;
					break;
			}
			break;
		case TC_UNDEF:
			lpstat->iDiscard=TRUE;
			break;
	}
	return 0;
}
/*
funcational dscription:
do some work like freeing packets that are discarded
author:jzheng
date:2014-06-03
*/
	//int icnt=0;

int sched_module_sanity_check(struct rte_mbuf*pktbuf,struct sched_stat_str*lpstat)
{
	if(lpstat->iDiscard==FALSE)
		return 1;
	//icnt++;
	//if(icnt%100==0)
		//printf("q:%d\n",icnt);
	FreePktsArray(&pktbuf,1);
	return 0;
}
/*
funcational description:
update IP header DiffServ Code Point Value,and recalculate IP header checksum
author:jzheng
date:2014-06-02
*/
int set_ip_dscp(struct rte_mbuf*pktbuf,dbg_local uint8_t iVal)
{
	uint32_t uiSum;
	uint16_t usWord; 
	struct _inner_str{
		uint8_t a:4;
		uint8_t b:4;
		uint8_t c:6;
		uint8_t d:2;
	}inner_str;;
	struct iphdr *iph;
	struct ether_hdr*eh=rte_pktmbuf_mtod(pktbuf,struct ether_hdr*);
	if(HTONS(eh->ether_type)!=ETH_TYPE_IP)
		return -1;
	iph=(struct iphdr *)(sizeof(struct ether_hdr)+(char*)eh);
	memcpy(&inner_str,&iph->sh_word,sizeof(iph->sh_word));//backup original short word
	memcpy(&usWord,&iph->sh_word,sizeof(iph->sh_word));
	//revert this short word to ZERO
	usWord=(~usWord)&0x0000ffff;
	uiSum=(~iph->ip_sum)&0x0000ffff;
	uiSum+=usWord;
	while(uiSum>>16)
		uiSum=((uiSum>>16)&0x0000ffff)+(uiSum&0x0000ffff);
	//Stuff this short word field with new value
	inner_str.c=iVal;
	memcpy(&iph->sh_word,&inner_str,sizeof(iph->sh_word));
	memcpy(&usWord,&iph->sh_word,sizeof(iph->sh_word));
	usWord=(usWord)&0x0000ffff;
	uiSum+=usWord;
	while(uiSum>>16)
		uiSum=((uiSum>>16)&0x0000ffff)+(uiSum&0x0000ffff);
	iph->ip_sum=~uiSum;
	return 0;
}
