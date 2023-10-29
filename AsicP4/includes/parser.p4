#define ETHERTYPE_IPV4 		0x0800
#define IPV4_TRANSPORT_TCP 	0x06
#define IPV4_TRANSPORT_UDP 	0x11

header ethernet_t ethernet;
header ipv4_t ipv4;
header tcp_t tcp;
header udp_t udp;

metadata five_tuple_t five_tuple;

header_type five_tuple_t {
    fields {
     	srcAddr : 32;
        dstAddr : 32;
        srcPort : 16;
        dstPort : 16;
        protocol : 8;
    }
}

parser start {
    return parse_ethernet;
}

parser parse_ethernet {
    extract(ethernet);
    return select(latest.etherType) {
        ETHERTYPE_IPV4 : parse_ipv4;
    }
}

parser parse_ipv4 {
    extract(ipv4);
    set_metadata(five_tuple.srcAddr, latest.srcAddr);
    set_metadata(five_tuple.dstAddr, latest.dstAddr);
    set_metadata(five_tuple.protocol, latest.protocol);
	return select(latest.protocol) {
		IPV4_TRANSPORT_TCP : parse_tcp;
		IPV4_TRANSPORT_UDP : parse_udp;
	}
}

parser parse_tcp {
	extract(tcp);
	set_metadata(five_tuple.srcPort, latest.srcPort);
	set_metadata(five_tuple.dstPort, latest.dstPort);
	return ingress;
}

parser parse_udp {
	extract(udp);
	set_metadata(five_tuple.srcPort, latest.srcPort);
	set_metadata(five_tuple.dstPort, latest.dstPort);	
	return ingress;
}

