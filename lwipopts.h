#ifndef LWIP_HDR_OPTS_H
#define LWIP_HDR_OPTS_H

/* NETBIOS name service */
#define LWIP_NETBIOS_RESPOND_NAME_QUERY   1

/* Multicast DNS */
#define LWIP_MDNS_RESPONDER              1
#define MDNS_MAX_SERVICES                2
#define MEMP_NUM_UDP_PCB                 (MEMP_NUM_UDP_PCB + 1)
#define LWIP_NUM_NETIF_CLIENT_DATA       (LWIP_NUM_NETIF_CLIENT_DATA + 1)

/* Required for IPv4 multicast */
#define LWIP_IGMP                       1

/* IPv6 support - disabled */
#define LWIP_IPV6                       0
#define LWIP_IPV6_MLD                   0

/* JSON configuration */
#define LWIP_JSON_CONFIG                1
#define LWIP_JSON_CONFIG_DEBUG          0

#endif /* LWIP_HDR_OPTS_H */ 