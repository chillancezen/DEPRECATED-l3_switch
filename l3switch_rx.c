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
functional description:
layer-2 decapsulate to decide which mod should recv it,
input mod:any
output  mod:
	RX_MOD_ARP
	RX_MOD_IP
	RX_MOD_IPv6
	RX_MOD_DROP:we do not deal with other kind of pkt
mod stack pos:first module
date :2014-05-10
author:jzheng
*/
dbg_local enum RX_MOD_INDEX rx_module_l2decap(dbg_local struct rte_mbuf* pktbuf,dbg_local enum RX_MOD_INDEX imodid)
{
	enum RX_MOD_INDEX nextmodid=RX_MOD_DROP;//default mod is droping 
	struct ether_hdr*eh;
	uint16_t eth_type;

	
	int iport_in;//for RX statistic 
	iport_in=pktbuf->pkt.in_port;
	rte_atomic64_inc(&gPortParaConf[iport_in].sd_port.ui_cnt_rx);
	rte_atomic64_add(&gPortParaConf[iport_in].sd_port.ui_byte_rx,pktbuf->pkt.pkt_len-sizeof(struct ether_hdr));
	rte_atomic64_add(&gPortParaConf[iport_in].sd_port.ui_cnt_timer_rx,pktbuf->pkt.pkt_len-sizeof(struct ether_hdr));
	
	eh=rte_pktmbuf_mtod(pktbuf,struct ether_hdr*);
	eth_type=HTONS(eh->ether_type);
	if(eth_type==ETH_TYPE_ARP)
		nextmodid=RX_MOD_ARP;
	else if(eth_type==ETH_TYPE_IP)
		nextmodid=RX_MOD_IP;
	else if(eth_type==ETH_TYPE_IPV6)
		nextmodid=RX_MOD_IPv6;
	else 
		nextmodid=RX_MOD_DROP;
	//printf("flag:%d %04x\n",nextmodid,eth_type);
	return nextmodid;
}

/*
functional description:
input mod:RX_MOD_ARP
output mod:
	RX_MOD_DROP
	RX_MOD_FRWARD
mod stack pos:
	below l2decap-mod
	above any action-mod
date :2014-05-14
author:jzheng
*/
dbg_local enum RX_MOD_INDEX rx_module_arp(dbg_local struct rte_mbuf*pktbuf,dbg_local enum RX_MOD_INDEX imodid)
{//check arp request /response format and content legality,we just check ,not act as ARP proxy though we do snoop mac-ip-port tables,and we even learn mac-ip mapping even when  depoyed in transparent environment 
	dbg_local enum RX_MOD_INDEX nextmodid=imodid;
	dbg_local struct  ether_hdr * eh;
	dbg_local struct  ether_arp * ea;
	dbg_local int idx;
	dbg_local int iFlowIdx;
	if(imodid!=RX_MOD_ARP)
		goto local_ret;
	eh=rte_pktmbuf_mtod(pktbuf,struct ether_hdr*);
	ea=(struct ether_arp*)(sizeof(struct ether_hdr)+(char*)eh);
	uint32_t uip_src=HTONL(MAKEUINT32FROMUINT8ARR(ea->arp_spa));//trans to little endian
   // printf("flag:%08x,,%x\n",uip_src,pktbuf->pkt.in_port);
	switch(HTONS(ea->ea_hdr.ar_op))
	{
		case 0x0001://active learning
			nextmodid=RX_MOD_FORWARD;
			if(!IS_MAC_BROADCAST(eh->d_addr.addr_bytes))//disable uni-cast  arp request packet,because rarely no such formated packet will be sent into an Ethernet
				goto exception_tag;
			if(!IS_MAC_EQUAL(eh->s_addr.addr_bytes,ea->arp_sha))
				goto exception_tag;
			#if 0
			for(idx=0;idx<gNBMACExcept;idx++)//check mandatory mac-ip entrusted domain white list
				if(uip_src==gMACIPExcept[idx].uip){
					if((!IS_MAC_EQUAL(ea->arp_sha,gMACIPExcept[idx].uimac))||(pktbuf->pkt.in_port!=gMACIPExcept[idx].uiport))
						goto exception_tag;
					break;
					}
			#endif
			iFlowIdx=find_net_entry(uip_src,1);
			if(iFlowIdx==-1)
				goto exception_tag;
			#if 0
			if(gFlow[iFlowIdx].uiInnerIP!=uip_src)//redundant ip check
				goto exception_tag;
			#endif
			if(gFlow[iFlowIdx].b_flow_enabled==FALSE)
				goto exception_tag;
			
			if(gFlow[iFlowIdx].b_mac_learned==FALSE){
				if(gFlow[iFlowIdx].b_dynamic){//can be update for dynamic at 1st time
					COPYMAC(gFlow[iFlowIdx].eaHostMAC.addr_bytes,ea->arp_sha);
					gFlow[iFlowIdx].portid=pktbuf->pkt.in_port;
					gFlow[iFlowIdx].b_mac_learned=TRUE;
				}
			}else{
				if((!IS_MAC_EQUAL(gFlow[iFlowIdx].eaHostMAC.addr_bytes,ea->arp_sha))||(gFlow[iFlowIdx].portid!=pktbuf->pkt.in_port)){//conflict detected,warning will be raised
					if(gFlow[iFlowIdx].b_preempty_enabled){
						COPYMAC(gFlow[iFlowIdx].eaHostMAC.addr_bytes,ea->arp_sha);
						gFlow[iFlowIdx].portid=pktbuf->pkt.in_port;
					}else{//if preempty policy is disabled,,this arp request learning do not take any signifinance
						if(gFlow[iFlowIdx].b_dynamic==FALSE)//and if IP to be protected it is,,then ARP spoofing detected,,log and warning
						goto exception_tag;//we discard this kind of packet
					}
				}
			}
			break;
		case 0x0002://we also  learning mapping-relationship from arp-response packet
			nextmodid=RX_MOD_FORWARD;
			#if 0
			if(IS_MAC_BROADCAST(eh->s_addr.addr_bytes))
				goto exception_tag;
			#endif
			if(IS_MAC_BROADCAST(eh->d_addr.addr_bytes))
				goto exception_tag;
			if(!IS_MAC_EQUAL(eh->s_addr.addr_bytes,ea->arp_sha))
				goto exception_tag;
			if(!IS_MAC_EQUAL(eh->d_addr.addr_bytes,ea->arp_tha))
				goto exception_tag;
			iFlowIdx=find_net_entry(uip_src,1);
			if(iFlowIdx==-1)
				goto exception_tag;
			if(gFlow[iFlowIdx].b_flow_enabled==FALSE)
				goto exception_tag;
			if(gFlow[iFlowIdx].b_mac_learned==FALSE){
				if(gFlow[iFlowIdx].b_dynamic){
					COPYMAC(gFlow[iFlowIdx].eaHostMAC.addr_bytes,ea->arp_sha);
					gFlow[iFlowIdx].portid=pktbuf->pkt.in_port;
					gFlow[iFlowIdx].b_mac_learned=TRUE;
				}
			}else{
				if((!IS_MAC_EQUAL(gFlow[iFlowIdx].eaHostMAC.addr_bytes,ea->arp_sha))||(gFlow[iFlowIdx].portid!=pktbuf->pkt.in_port)){
					if(gFlow[iFlowIdx].b_preempty_enabled){
						COPYMAC(gFlow[iFlowIdx].eaHostMAC.addr_bytes,ea->arp_sha);
						gFlow[iFlowIdx].portid=pktbuf->pkt.in_port;
					}else{
						if(gFlow[iFlowIdx].b_dynamic==FALSE)
						goto exception_tag;
					}
				}
			}
			break;
		default:
			goto exception_tag;
			break;
	}
	local_ret:
	return nextmodid;
	exception_tag://here we set next module id
		nextmodid=RX_MOD_DROP;
		goto local_ret;
}
/*
functional description:
IP RX module check,even  we do nothing here
input mod:
	RX_MOD_IP
output mod:
	RX_MOD_DROP
	RX_MOD_FRWARD
module stack pos:
	below l2decap-mod
	above any action-mod
date :2014-05-28
author:jzheng
*/
dbg_local enum RX_MOD_INDEX rx_module_ip(dbg_local struct rte_mbuf*pktbuf,dbg_local enum RX_MOD_INDEX imodid)
{
	dbg_local enum RX_MOD_INDEX nextmodid=imodid;

	if(imodid!=RX_MOD_IP)
		goto local_ret;
	nextmodid=RX_MOD_FORWARD;
	//for now we do not filter such pkts with their invalid dst mac-ip ,
	local_ret:
	return nextmodid;
}


/*
functional description:
reservred IPv6 Entry Interface,actually we just forward or block  such IPv6 Packets
input mod:
	RX_MOD_IPv6
output mod:
	RX_MOD_DROP
	RX_MOD_FRWARD
module stack pos:
	below l2decap-mod
	above any action-mod
date :2014-05-28
author:jzheng

*/
dbg_local enum RX_MOD_INDEX rx_module_ipv6(dbg_local struct rte_mbuf*pktbuf,dbg_local enum RX_MOD_INDEX imodid)
{
	dbg_local enum RX_MOD_INDEX nextmodid=imodid;

	if(imodid!=RX_MOD_IPv6)
		goto local_ret;
	nextmodid=RX_MOD_DROP;

	local_ret:
	return nextmodid;
}

/*
functional description:
packet forward(enqueue) module,we push packet into QoS queue or TX queue
input mod:
	RX_MOD_FORWARD
output mod:
	RX_MOD_DROP
	RX_MOD_IDLE
module stack pos:
	as 1st action ,before drop module,and below any policy module
*/
dbg_local enum RX_MOD_INDEX rx_module_forward(dbg_local struct  rte_mbuf*pktbuf,dbg_local enum RX_MOD_INDEX imodid)
{
	dbg_local int iPortIn;
	dbg_local int iPortOut;
	dbg_local int iFlowIdx;
	dbg_local int iDstIdx;
	dbg_local int uiSIP;
	dbg_local int uiDIP;
	dbg_local int idx;
	dbg_local int rc;
	dbg_local int iUsed;
	dbg_local int iMod;
	dbg_local struct ether_hdr *eh;
	dbg_local struct ether_arp *ea;
	dbg_local struct iphdr *iph;
	dbg_local int iPortForwardFlag[MAX_PORT_NB];
	dbg_local struct rte_mbuf*rmPortForwardMbuf[MAX_PORT_NB];
	
	dbg_local enum RX_MOD_INDEX nextmodid=imodid;
	
	if(imodid!=RX_MOD_FORWARD)
		goto local_ret;
	//phaze 1,we filter  packet with illegal l2 and l3 address,and drop it

	//printf("input port:%d\n",(int)pktbuf->pkt.in_port);
	eh=rte_pktmbuf_mtod(pktbuf,struct ether_hdr*);
	uiSIP=0xffffffff;
	switch(HTONS(eh->ether_type))
	{
		case ETH_TYPE_ARP:
			ea=(struct ether_arp *)(sizeof(struct ether_hdr)+(char*)eh);
			uiSIP=HTONL(MAKEUINT32FROMUINT8ARR(ea->arp_spa));
			uiDIP=HTONL(MAKEUINT32FROMUINT8ARR(ea->arp_tpa));
			break;
		case ETH_TYPE_IP:
			iph=(struct iphdr *)(sizeof(struct ether_hdr)+(char*)eh);
			uiSIP=HTONL(iph->ip_src);
			uiDIP=HTONL(iph->ip_dst);
			break;
		default:
			goto exception_tag;
			break;
	}
	
	iFlowIdx=find_net_entry(uiSIP,TRUE);
	//pkt source legality checking
	if(iFlowIdx==-1)
		goto exception_tag;
	if(gFlow[iFlowIdx].b_mac_learned==FALSE)
		goto exception_tag;
	if(gFlow[iFlowIdx].b_flow_enabled==FALSE)
		goto exception_tag;
	if(gFlow[iFlowIdx].portid!=pktbuf->pkt.in_port)
		goto exception_tag;
	if(!IS_MAC_EQUAL(eh->s_addr.addr_bytes,gFlow[iFlowIdx].eaHostMAC.addr_bytes))
		goto exception_tag;

	iDstIdx=find_net_entry(uiDIP,TRUE);

		/*printf("%02x,%02x,%02x,%02x,%02x,%02x,\n",eh->d_addr.addr_bytes[0]
			,eh->d_addr.addr_bytes[1]
			,eh->d_addr.addr_bytes[2]
			,eh->d_addr.addr_bytes[3]
			,eh->d_addr.addr_bytes[4]
			,eh->d_addr.addr_bytes[5]
			);*/
	if(IS_MAC_BROADCAST(eh->d_addr.addr_bytes)){//link broadcast
		
		iUsed=FALSE;
		for(idx=0;idx<gNBPort;idx++)
			if(port_brdcst_map[(int)pktbuf->pkt.in_port][idx]==TRUE){
				iPortForwardFlag[idx]=1;
				if(iUsed==FALSE){
					rmPortForwardMbuf[idx]=pktbuf;
					iUsed=TRUE;
			 	}else{
			 		rmPortForwardMbuf[idx]=rte_pktmbuf_clone(pktbuf,gmempool);
					if(!rmPortForwardMbuf[idx])
						iPortForwardFlag[idx]=0;
			 	}	
			}else iPortForwardFlag[idx]=0;
				
	}else{//link uicast
		for(idx=0;idx<gNBPort;idx++)
			iPortForwardFlag[idx]=0;
		if(iDstIdx!=-1){
		//	printf(".......idstidx!=-1.%08x\n",gFlow[iDstIdx].uiInnerIP);
			
			if(gFlow[iDstIdx].b_mac_learned==FALSE)
				goto exception_tag;
		//	printf("Heror1\n");
			if(gFlow[iDstIdx].b_flow_enabled==FALSE)
				goto exception_tag;
		//	printf("Heror2\n");
			iPortForwardFlag[(int)gFlow[iDstIdx].portid]=1;
			rmPortForwardMbuf[(int)gFlow[iDstIdx].portid]=pktbuf;
		}
		else {
		//	printf(".......idstidx==-1\n");
			if(port_def_frd_map[(int)pktbuf->pkt.in_port]!=-1){
				iPortForwardFlag[port_def_frd_map[(int)pktbuf->pkt.in_port]]=1;
				rmPortForwardMbuf[port_def_frd_map[(int)pktbuf->pkt.in_port]]=pktbuf;
			}
			else goto exception_tag;
		}
	}
	//mirror--pkt--cloning
		
	for(idx=0;idx<gNBPort;idx++)
	{
		if(!iPortForwardFlag[idx])continue;
		switch(port_policy_map[(int)pktbuf->pkt.in_port][idx])
		{
			case PORT_POLICY_MAP_DIRECT:
				//printf("...direct:%d->%d\n",(int)pktbuf->pkt.in_port,idx);
				rc=EnqueueIntoPortQueue(idx,&rmPortForwardMbuf[idx],1);
				//printf("...rc:%d\n",rc);
				if(!rc) goto exception_tag;
				break;
			case PORT_POLICY_MAP_QOS:
				
				//high nibble:dst port
				//low nibble :src port
				iMod=sched_mod_map[(int)rmPortForwardMbuf[idx]->pkt.in_port][idx];
				rmPortForwardMbuf[idx]->pkt.in_port=MAKEBYTE(idx,rmPortForwardMbuf[idx]->pkt.in_port);
				rc=rte_ring_mp_enqueue(sched_mod_list[iMod].rrFirLev,rmPortForwardMbuf[idx]);
				if(rc==-ENOBUFS){
					goto exception_tag;
				}
				break;
			case PORT_POLICY_MAP_UNDEFINE:
				goto exception_tag;
				break;
		}
	}

		
	
	local_ret:
	return nextmodid;
	exception_tag:
	nextmodid=RX_MOD_DROP;
	goto local_ret;
}
/*
functional description:
pkt which not fulfil  forwarding  rules will be drop here ,and sys rc will deallocated
input mod:RX_MOD_DROP
output mod:RX_MOD_IDLE
module stack pos:last module
date :2014-05-10
author:jzheng

*/
dbg_local enum RX_MOD_INDEX rx_module_drop(dbg_local struct rte_mbuf*pktbuf,dbg_local enum RX_MOD_INDEX imodid)
{
	dbg_local enum RX_MOD_INDEX nextmodid=RX_MOD_IDLE;//default mod id can be assigned to any mod ,as this mod is last one
	int iport_in;
	if(imodid!=RX_MOD_DROP)//check entry legality
			goto local_ret;
	iport_in=pktbuf->pkt.in_port;
	rte_pktmbuf_free(pktbuf);
	local_ret:
	return nextmodid;
}

