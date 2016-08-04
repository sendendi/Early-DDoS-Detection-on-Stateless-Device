#include "includes/headers.p4"
#include "includes/parser.p4"

#define USE_SAMPLING			0
#define SAMPLE_X2Y_TRUNC_MASK 	0x03FF
#define SAMPLE_Y2U_ARR_SIZE		128

#define CARD_BUCKETS_TRUNC_MASK 0x000000001F
#define CARD_BUCKETS_ARR_SIZE 	32
#define CARD_USE_HARMONIC_MEAN	1

#define REG_PACKET_COUNTER 		0
#define REG_PACKET_TO_SAMPLE 	1

register sample_x2y_register {
	width : 16;
	instance_count : 2;
}

register cardinality_buckets_register {
	width : 8;
	instance_count : CARD_BUCKETS_ARR_SIZE;
}

register cardinality_mean_register {
	width : 40;
	instance_count : 1;
}

register sample_y2u_data_register {
	width : 40;
	instance_count : SAMPLE_Y2U_ARR_SIZE;
}

register sample_y2u_index_register {
	width : 8;
	instance_count : 1;
}

metadata sample_x2y_data_t sample_x2y_data;
metadata cardinality_data_t cardinality_data;

header_type sample_x2y_data_t {
	fields {
		hash1 : 40;
		packetNumber : 16;
		packetToSample : 16;
	}
}

header_type cardinality_data_t {
	fields {
		hash1 : 40;
		hash2 : 8;
		rank : 8;
		updateMean : 8;
	}
}

table sample_x2y {
	actions {
		sample_x2y_action;
	}
}

action sample_x2y_action() {
	// calculate hash1 value:
	modify_field(sample_x2y_data.hash1, (five_tuple.dstAddr * 59) ^ (five_tuple.srcAddr) ^ (five_tuple.srcPort << 16) ^ (five_tuple.dstPort));
	// read the number of the packet to sample:
	register_read(sample_x2y_data.packetToSample, sample_x2y_register, REG_PACKET_TO_SAMPLE);
	// update sample_x2y counter:
	register_read(sample_x2y_data.packetNumber, sample_x2y_register, REG_PACKET_COUNTER);
	modify_field(sample_x2y_data.packetNumber, (sample_x2y_data.packetNumber + 1) & SAMPLE_X2Y_TRUNC_MASK);
	register_write(sample_x2y_register, REG_PACKET_COUNTER, sample_x2y_data.packetNumber);
}

table reset_sample_x2y {
	actions {
		reset_sample_x2y_action;
	}
}

action reset_sample_x2y_action() {
	// calculate the number of the next packet to sample:
	modify_field(sample_x2y_data.packetToSample, (five_tuple.dstAddr + five_tuple.srcAddr + five_tuple.dstPort + five_tuple.srcPort) & SAMPLE_X2Y_TRUNC_MASK);
	register_write(sample_x2y_register, REG_PACKET_TO_SAMPLE, sample_x2y_data.packetToSample);
}

table get_cardinality {
	actions {
		get_cardinality_action;
	}
}

action get_cardinality_action() {
	#if USE_SAMPLING == 0
		// calculate hash1 value:
		modify_field(cardinality_data.hash1, (five_tuple.dstAddr * 59) ^ (five_tuple.srcAddr) ^ (five_tuple.srcPort << 16) ^ (five_tuple.dstPort));	
	#else
		// copy hash1 value:
		modify_field(cardinality_data.hash1, sample_x2y_data.hash1);
	#endif
	// calculate hash2 value:
	modify_field(cardinality_data.hash2, cardinality_data.hash1 & CARD_BUCKETS_TRUNC_MASK);
	// calculate the rank of the current packet:
	get_cardinality_rank(cardinality_data.rank, cardinality_data.hash1);
	// update the buckets array with the current rank:
	update_cardinality_rank(cardinality_buckets_register, cardinality_data.hash2, cardinality_data.rank, cardinality_data.updateMean);
	// update the mean of the buckets array:
	update_cardinality_mean(cardinality_data.updateMean, cardinality_mean_register, cardinality_buckets_register, CARD_BUCKETS_ARR_SIZE, CARD_USE_HARMONIC_MEAN);
}

table sample_y2u {
	actions {
		sample_y2u_action;
	}
}

action sample_y2u_action() {
	update_sample_y2u(cardinality_data.hash1, sample_y2u_index_register, sample_y2u_data_register, SAMPLE_Y2U_ARR_SIZE);
}

table drop_packet {
	actions {
		drop_packet_action;
	}
}

action drop_packet_action() {
	drop();
}

control ingress {
	#if USE_SAMPLING == 0
		apply(get_cardinality);
	#else
		apply(sample_x2y);
		if (sample_x2y_data.packetNumber == 0) {
			apply(reset_sample_x2y);
		}
		if (sample_x2y_data.packetNumber == sample_x2y_data.packetToSample) {
			apply(get_cardinality);
			apply(sample_y2u);		
		}	
	#endif
	apply(drop_packet);
}

