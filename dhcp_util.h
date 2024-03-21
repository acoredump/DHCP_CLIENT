#ifndef _dhcp_util_h_
#define _dhcp_util_h_

#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <fcntl.h>
#include <pthread.h>
#include <map>
#include <cassert>
using namespace std;

#define MAX_POS_NUM 64
#define MAX_THREAD_NUM 32
#define MAX_SINGLE_THREAD_QPS 6000
#define USLEEP_GAP 0.005
#define CONF_FILE "./dhcp_client.conf"

#define INIT_STATUS 0
#define FREE_STATUS 1
#define USED_STATUS 2

#define RUN_MODE_TEST 0
#define RUN_MODE_PERF 1

#define SMA_PACKAGE_SIZE 512
#define MID_PACKAGE_SIZE 694
#define MAX_PACKAGE_SIZE 812

//dhcp_msg_type elem:
#define DHCPDISCOVER		1
#define DHCPOFFER			2
#define DHCPREQUEST			3
#define DHCPDECLINE			4
#define DHCPACK				5
#define DHCPNAK				6
#define DHCPRELEASE			7
#define DHCPINFORM			8
#define DHCPLEASEQUERY		10
#define DHCPLEASEUNASSIGNED	11
#define DHCPLEASEUNKNOWN	12
#define DHCPLEASEACTIVE		13

#define DHCPREQUESTSELECT	101
#define DHCPREQUESTRENEW	102
#define DHCPREQUESTREBIND	103
#define DHCPREQUESTREBOOT	104

#define CFG_DHCP_RUN_MODE    "run_mode"
#define CFG_DHCP_LOOP_MODE   "loop_mode"
#define CFG_DHCP_PORT        "dhcp_port"
#define CFG_DHCP_SERVER      "dhcp_server"
#define CFG_DHCP_MSG_TYPE    "dhcp_msg_type"
#define CFG_CLIENT_IP        "client_ip"
#define CFG_YOUR_IP          "your_ip"
#define CFG_INTERFACE_IP     "interface_ip"
#define CFG_CLIENT_MAC       "client_mac"
#define CFG_CLIENT_NUM       "client_num"
#define CFG_TOTAL_QPS        "total_qps"
#define CFG_THREAD_NUM       "thread_num"


#define SUBMIT_BYTES(ptr, val, n)	        \
        do {                                \
            if (n == 2)                     \
            {                               \
                uint16_t i = htons(val);    \
                memcpy(ptr, &i, n);	        \
            }                               \
            else if (n == 4)                \
            {                               \
                uint32_t i = htonl(val);    \
                memcpy(ptr, &i, n);	        \
            }                               \
            else                            \
                memcpy(ptr, &val, n);	    \
            ptr+=(n);                       \
        }while(0) 

#define MEMCPY_BYTES(ptr, val, n)	        \
        do {                                \
            memcpy(ptr, &val, n);	        \
            ptr+=(n);                       \
        }while(0)        

#define MEMSET_BYTES(ptr, val, n)	        \
        do {                                \
            memset(ptr, val, n);	        \
            ptr+=(n);                       \
        }while(0)         


#define GET_BYTES(ptr, val, n)	                \
        do {                                    \
            if (n == 2)                         \
            {                                   \
                val = ntohs(*(uint16_t *)ptr);  \
            }                                   \
            else if (n == 4)                    \
            {                                   \
                val = ntohl(*(uint32_t *)ptr);  \
            }                                   \
            else                                \
                memcpy(&val, ptr, n);           \
        }while(0) 

/*convert a string,which length is 18, to a macaddress data type.*/
//mac[i] = (a2x(str[i*3]) << 4) + a2x(str[i*3 + 1])  假设 str[i*3 + 1]或者 str[i*3] 有一个是":"
//系统就直接退出了？？？ 没有core文件也没有其他任何线索
#define MAC_LEN_IN_BYTE 6
#define COPY_STR2MAC(mac,str)  \
        do { \
                for(int i = 0; i < MAC_LEN_IN_BYTE; i++) {\
                        assert(strlen(str) == 17);\
                        assert(str[i*3] != ':');\
                        assert(str[i*3 + 1] != ':');\
                        mac[i] = (a2x(str[i*3]) << 4) + a2x(str[i*3 + 1]);\
                }\
        } while(0)

struct base_map
{
    uint8_t tag;
    uint8_t *_p_elem;   //指向从mutil_queue中取出来的元素
};

//mutil_base ：存访在队列 mutil_queue中的对象 需要包含的基础成员变量
typedef struct 
{
    //空闲队列指针
    uint8_t    *p_next;         //当前前向指针只在空闲队列中使用
    //lru队列指针（双向）
    uint8_t    *p_lru_prev;     //当前前向指针只在lru中会被使用
    uint8_t    *p_lru_next;     //当前后向指针只在lru中会被使用
    uint32_t   id;
    //IPADDRESS  src_ip;
    //uint16_t   src_port;
}mutil_base;

typedef struct
{
    mutil_base base;
    uint32_t   flag;
    uint32_t   transid;
    uint32_t   offer_addr;
    uint8_t    next_step;
    char       mac_addr[1][MAX_POS_NUM];
}mutil_transid_context;

class mutil_queue {
public:
    //offset_id是指的mutil_transid->data与mutil_transid->base->id之间的偏移量
    mutil_queue(int max_ele_num, uint32_t offset_id);
    virtual ~mutil_queue(){};

    bool init();
    bool add_elem(uint8_t *elem);
    uint8_t * fitch_elem();
    uint8_t * search_elem(uint32_t tid);
    void release_elem(uint8_t *elem);
private:
    pthread_spinlock_t _lock;
    //空闲队列
    uint8_t *_p_header;
    uint8_t *_p_tailer;

    base_map *_used_maps;
    uint32_t _offset_id;

    uint32_t _max_ele_num;
    uint32_t _cur_ele_num;
    uint32_t _cur_ele_num_lru;

    //超时队列，不记录时间的超时队列，当前队列如果可用ele不足，即开始强制回收lru中的ele
    uint8_t *_p_header_lru;
    uint8_t *_p_tailer_lru;

};

struct elem_base
{
    uint8_t   *p_next;
    uint8_t      step;
    uint32_t  transid;
    uint16_t data_len;
};

struct queue_elem_s
{
    elem_base base;
    char  buff[SMA_PACKAGE_SIZE];
};

struct queue_elem_m
{
    elem_base base;
    char  buff[MID_PACKAGE_SIZE];
};

struct queue_elem_b
{
    elem_base base;
    char  buff[MAX_PACKAGE_SIZE];
};

//simple queue 不关心 队列元素内存空间的管理，元素空间由使用者自行管理
class simple_queue {
public:
    simple_queue(int max_ele_num);
    virtual ~simple_queue(){};

    bool push_elem(uint8_t *elem);
    uint8_t * fitch_elem();
private:
    //pthread_mutex_t lock;
    pthread_spinlock_t lock;
    uint8_t *_p_header;
    uint8_t *_p_tailer;
    uint32_t _max_ele_num;
    uint32_t _cur_ele_num;
};

struct thread_cxt
{
    uint32_t proc_qps;
    simple_queue *proc_queue;
    volatile uint64_t proc_req_qps;
    volatile uint64_t proc_rsp_qps;
    volatile uint64_t proc_req_qps_pre;
    volatile uint64_t proc_rsp_qps_pre;
};

class mini_qps_clock
{
public:
    mini_qps_clock(uint32_t r_qps);
    virtual ~mini_qps_clock(){};
    
    void init();
    void clock();
private:
    uint32_t _clock_gap;
    uint32_t _clock_qps;
    struct timespec _next_mo;
};

#endif