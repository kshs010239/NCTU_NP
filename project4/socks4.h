
typedef struct _Request {
	uint8_t vn;
	uint8_t cd;
	uint16_t dst_port;
	struct in_addr dst_ip;
} Request;


typedef struct _Reply {
	uint8_t vn;
	uint8_t cd;
	uint16_t dst_port;
	struct in_addr dst_ip;
} Reply;


