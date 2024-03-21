#include "dhcp_util.h"

int g_sockfd = -1;
int g_run_mode = 0;
int g_wait_sockfd = -1;
int g_perf_thread_num = 1;

uint32_t g_transid = 0;
uint32_t g_total_qps = 0;
uint8_t g_perf_types[MAX_POS_NUM];

thread_cxt *run_cxt[MAX_THREAD_NUM];
simple_queue *g_req_queue[MAX_THREAD_NUM];

mutil_queue *g_transid_pool;
map<string, string> g_my_conf;
char g_msg_type_detail[128][64];

static pthread_t thid_req[MAX_THREAD_NUM];
static pthread_t thid_recv[MAX_THREAD_NUM];
static pthread_t thid_stat;

unsigned char a2x(const char c)
{
    switch(c) {
        case '0'...'9':
            return (unsigned char)atoi(&c);
        case 'a'...'f':
            return 0xa + (c-'a');
        case 'A'...'F':
            return 0xa + (c-'A');
        default:
            goto error;
    }
    error:
    exit(0);
}

bool setNonBlock(int fd) {
		int	flags	=	fcntl(fd,	F_GETFL, 0);
        if (flags < 0)
            return false;
		int	r	=	fcntl(fd,	F_SETFL, flags | O_NONBLOCK);
        if (r < 0)
            return false;
        return true;
}

void init_dhcp_type_info()
{
    strcpy(g_msg_type_detail[DHCPDISCOVER], "DHCPDISCOVER");
    strcpy(g_msg_type_detail[DHCPOFFER], "DHCPOFFER");
    strcpy(g_msg_type_detail[DHCPREQUEST], "DHCPREQUEST");
    strcpy(g_msg_type_detail[DHCPDECLINE], "DHCPDECLINE");
    strcpy(g_msg_type_detail[DHCPACK], "DHCPACK");
    strcpy(g_msg_type_detail[DHCPNAK], "DHCPNAK");
    strcpy(g_msg_type_detail[DHCPRELEASE], "DHCPRELEASE");
    strcpy(g_msg_type_detail[DHCPINFORM], "DHCPINFORM");
    strcpy(g_msg_type_detail[DHCPLEASEQUERY], "DHCPLEASEQUERY");
    strcpy(g_msg_type_detail[DHCPLEASEUNASSIGNED], "DHCPLEASEUNASSIGNED");
    strcpy(g_msg_type_detail[DHCPLEASEUNKNOWN], "DHCPLEASEUNKNOWN");
    strcpy(g_msg_type_detail[DHCPLEASEACTIVE], "DHCPLEASEACTIVE");
    strcpy(g_msg_type_detail[DHCPREQUESTSELECT], "DHCPREQUESTSELECT");
    strcpy(g_msg_type_detail[DHCPREQUESTRENEW], "DHCPREQUESTRENEW");
    strcpy(g_msg_type_detail[DHCPREQUESTREBIND], "DHCPREQUESTREBIND");
    strcpy(g_msg_type_detail[DHCPREQUESTREBOOT], "DHCPREQUESTREBOOT");
}

/*
高效的字符串切换函数，切割的主要逻辑：
1. 替换掉所有的空格部分
2. 替换所有 切割目标 为'\0'
3. 使用result记录每一个切割后的字符的首地址、
eg:     dhcp_server = 10.21.1.1'\0'
        dhcp_server'\0'10.21.1.1'\0'
存在一个不足： 切割目标 split_flag 不支持含有 空格 ' '
如果需要还原split之前的字符串，需要记录\0的位置，然后将其设置为切割符号
*/
void split_formate_str(char *src, char *split_flag, char **result, int &num)
{
    //记录首位置
    num = 0;
    result[num] = src;
    num += 1;

    int len = strlen(src);
    int len_sp_f = strlen(split_flag);
    for (int cnt = 0; cnt < len; cnt ++)
    {
        if (src[cnt] == ' ')
        {
            //覆盖 空格，同时连着字符串最后一个'\0'往前移动一位
            for(int tmp = cnt; tmp < len; tmp ++)
            {
                src[tmp] = src[tmp+1];
            }
            //需要重新判断cnt
            cnt --;
            len --;
        }
        else if (memcmp(&src[cnt], split_flag, len_sp_f) == 0)
        {
            // 是切割目标字符/字符串，执行替换操作
            src[cnt] = '\0';
            if (len_sp_f > 1)
            {
                //需要挨个挪动后续的字符串字符
                for(int tmp = cnt + 1; tmp < len; tmp++)
                {
                    src[tmp] = src[tmp + len_sp_f - 1];
                }
                len -= len_sp_f - 1;
            }
            result[num] = src + cnt + 1;
            num += 1;
        }
        if (num >= MAX_POS_NUM)
            break;
    }
    //每行的最后一个元素，是换行符；需要将换行符改为'\0'
    if ((result[num -1])[strlen(result[num -1]) - 1] == '\n')
        (result[num -1])[strlen(result[num -1]) - 1] = '\0';
}

bool get_dhcp_client_conf()
{
    int num = 0;

    char *pos[MAX_POS_NUM];
    char line[1024] = {'\0'};

    FILE *f_conf = NULL;
    f_conf = fopen(CONF_FILE, "r");
    if (!f_conf)
    {
        printf("open conf file %s failed ....\n", CONF_FILE);
        return false;
    }

    //read file and the formate is  XXXXXX = YYYYYY 
    while (NULL != fgets(line, sizeof(line), f_conf))
    {
        if(strstr(line, "#") != NULL || line[0] == '\0' || strlen(line) <= 3)
			continue;
        // formate : dhcp_server = 10.21.1.1
        num = 0;
        split_formate_str(line, "=", pos, num);
        if (num = 2)
        {
            g_my_conf[pos[0]] = pos[1];
        }
    }

    fclose(f_conf);
    //check the CONF_FILE ........

    if (g_my_conf[CFG_DHCP_SERVER].length() == 0    ||
        g_my_conf[CFG_DHCP_PORT].length() == 0      ||
        g_my_conf[CFG_CLIENT_MAC].length() == 0     ||
        g_my_conf[CFG_DHCP_MSG_TYPE].length() == 0  ||
        g_my_conf[CFG_INTERFACE_IP].length() == 0   ||
        g_my_conf[CFG_YOUR_IP].length() ==0         ||
        g_my_conf[CFG_DHCP_RUN_MODE].length() ==0 )
        return false;
    
    return true;
}

/*
  1. 支持混合方式发起 discover - request - renew - release 四种DHCP交互报文
  2. 支持批量发起 DHCP 请求，包含 1 中的各种组合方式的批量发起
  3. Undo
*/
int get_dhcp_req_pkg_v4(char *pkg_buf, const uint32_t transid, const uint8_t msg_type,
                        const char *src_mac_addr, const char *src_client_ip, const char *src_your_ip,
                        const char *src_interface_ip)
{
    int pkg_len = 0;
    uint8_t  tag_1 = 0;
    uint16_t tag_2 = 0;
    uint32_t tag_4 = 0;
    uint8_t  *pos = (uint8_t *)pkg_buf;
    //message_type + handler_type + handler_address_len + hops
    tag_1 = 1;SUBMIT_BYTES(pos, tag_1, 1);
    tag_1 = 1;SUBMIT_BYTES(pos, tag_1, 1);
    tag_1 = 6;SUBMIT_BYTES(pos, tag_1, 1);
    tag_1 = 0;SUBMIT_BYTES(pos, tag_1, 1);

    //transid + secs_elapsed + flags
    SUBMIT_BYTES(pos, transid, 4);
    tag_2 = 0;SUBMIT_BYTES(pos, tag_2, 2);
    tag_2 = 0;SUBMIT_BYTES(pos, tag_2, 2);

    //client: discover与select 阶段都是 0.0.0.0 
    uint32_t addr = 0;
    if (msg_type != DHCPDISCOVER && msg_type != DHCPREQUESTSELECT)
    {
        //inet_addr已经对地址完成网络字节序转换了，所以直接进行内存拷贝即可
        addr = inet_addr(src_client_ip);
    }
    MEMCPY_BYTES(pos, addr, 4);

    //youip
    addr = 0;
    if (msg_type != DHCPDISCOVER && msg_type != DHCPREQUESTSELECT)
    {
        addr = inet_addr(src_your_ip);
    }
    //SUBMIT_BYTES(pos, addr, 4);
    MEMCPY_BYTES(pos, addr, 4);

    //nextip
    addr = 0;
    uint32_t nextip = 0;
    //SUBMIT_BYTES(pos, nextip, 4);
    MEMCPY_BYTES(pos, addr, 4);

    //relayip
    addr = 0;
    addr = inet_addr(src_interface_ip);
    //SUBMIT_BYTES(pos, addr, 4);
    MEMCPY_BYTES(pos, addr, 4);

    //mac 16字节
    //转换mac地址为整形
    uint8_t macaddr[8] = {0};
    COPY_STR2MAC(macaddr,src_mac_addr);
    for (int cnt = 0; cnt < MAC_LEN_IN_BYTE; cnt ++)
    {
        SUBMIT_BYTES(pos, macaddr[cnt], 1);
    }

    memset(pos, 0, 10);
    pos +=10;
    memset(pos, 0, 192);
    pos +=192;

    //magic cook
    tag_1 = 0x63;SUBMIT_BYTES(pos, tag_1, 1);
    tag_1 = 0x82;SUBMIT_BYTES(pos, tag_1, 1);
    tag_1 = 0x53;SUBMIT_BYTES(pos, tag_1, 1);
    tag_1 = 0x63;SUBMIT_BYTES(pos, tag_1, 1);

    //DHCPREQUESTSELECT 需要带入opt 50 以明确最终client选择的ip地址
    if (msg_type == DHCPREQUESTSELECT)
    {
        tag_1 = 50;SUBMIT_BYTES(pos, tag_1, 1);
        tag_1 = 4;SUBMIT_BYTES(pos, tag_1, 1);
        addr = inet_addr(src_your_ip);
        MEMCPY_BYTES(pos, addr, 4);
    }

    //需要带入opt 54 以明确最终client选择的DHCP_SERVER
    if (msg_type != DHCPDISCOVER)
    {
        tag_1 = 54;SUBMIT_BYTES(pos, tag_1, 1);
        tag_1 = 4;SUBMIT_BYTES(pos, tag_1, 1);
        addr = inet_addr(g_my_conf[CFG_DHCP_SERVER].c_str());
        MEMCPY_BYTES(pos, addr, 4);
    }

    //opt 53 带入发起的DHCP详细消息类型
    tag_1 = 53;SUBMIT_BYTES(pos, tag_1, 1);
    tag_1 = 1;SUBMIT_BYTES(pos, tag_1, 1);
    tag_1 = msg_type;
    if (msg_type == DHCPREQUESTREBIND || msg_type == DHCPREQUESTRENEW || 
        msg_type == DHCPREQUESTSELECT || msg_type == DHCPREQUESTREBOOT )
    {
        tag_1 = DHCPREQUEST;
    }
    SUBMIT_BYTES(pos, tag_1, 1);

    //opt 61
    tag_1 = 61;SUBMIT_BYTES(pos, tag_1, 1);
    tag_1 = 7;SUBMIT_BYTES(pos, tag_1, 1);
    tag_1 = 1;SUBMIT_BYTES(pos, tag_1, 1);

    //mac地址
    for (int cnt = 0; cnt < MAC_LEN_IN_BYTE; cnt ++)
    {
        SUBMIT_BYTES(pos, macaddr[cnt], 1);
    }

    //opt 255
    tag_1 = 255;SUBMIT_BYTES(pos, tag_1, 1);
    MEMSET_BYTES(pos, 0, 255);

    return pos - (uint8_t *)pkg_buf;
}

/*
    解包 只解几个关键位置的报文字段
    transid  第5个字节，长度4个字节
    yourip 第17个字节，长度4个字节
*/
void decode_dhcp_rsp_pkg(char *pkg_buf, uint32_t &trans_id, uint32_t &your_ip)
{
    trans_id = 0;
    uint8_t *pos = (uint8_t *)(pkg_buf);
    GET_BYTES((pos + 4), trans_id, 4);

    your_ip = 0;
    memcpy(&your_ip, (pos + 16), 4);
}

//static void *thread_func(void * arg)
int recv_dhcp_rsp_v4( int &sock_fd, const uint32_t tag_trans_id, const uint8_t msg_type )
{
    if (sock_fd == -1)
        return -1;

    //release 报文 服务端不会对客户端进行响应
    if (msg_type == 7)
        return 0;
    
    char *str = NULL;
    uint16_t times = 0;
    struct sockaddr_in src;
    memset(&src, 0, sizeof(src));
    socklen_t src_len = sizeof(src);
    
    uint32_t trans_id = 0;
    uint32_t offer_ip = 0;
    char recv_buf[2048] = {'\0'};
    do{
        if(recvfrom(sock_fd, recv_buf, 2048, 0, (sockaddr*)&src, &src_len) <= 0)
        {
            usleep(USLEEP_GAP);

            times++;
            if (times > 10000)
            {
                printf(">>>> !!!! receive the dhcp rsp timeout .... <<<<\n\n");
                return -1;
            }
            continue;
        }else
        {
            //get the dhcp resp
            str = inet_ntoa(src.sin_addr);
            printf(">>>> receive the rsp from server ip %s, port %d ....\n", str, ntohs(src.sin_port));
            decode_dhcp_rsp_pkg(recv_buf, trans_id, offer_ip);
            uint8_t *pch = (uint8_t *)&offer_ip;
            char your_ip[64] = {0};
            sprintf(your_ip, "%d.%d.%d.%d", *(pch), *(pch+1), *(pch+2), *(pch+3));
            if (tag_trans_id == trans_id)
            {
                if (msg_type == DHCPDISCOVER) 
                {
                    //记录 DHCP_SERVER 分配的 地址
                    g_my_conf[CFG_YOUR_IP] = your_ip;
                    g_my_conf[CFG_CLIENT_IP] = your_ip;
                }
                printf("dhcp server rsp the transid %d allocate with ip %s .... <<<<\n\n", trans_id, your_ip);
            }else
            {
                times++;
                printf("transid not matched, continue to receive .... <<<<\n\n");
                if (times > 10000)
                {
                    printf(">>>> !!!! receive the dhcp rsp timeout .... <<<<\n\n");
                    return -1;
                }
            }
            
            break;
        }

    }while (true);
    return 1;
}

//简单粗暴但相对精确的定时器
void time_to_wait(int time_sec, int time_usec = 0)
{
	struct timeval tv;

	tv.tv_sec = time_sec;
	tv.tv_usec =  0;

    char szBuf[32] = {0};
	setsockopt(g_wait_sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)); 
    recvfrom(g_wait_sockfd, szBuf, sizeof(szBuf) - 1, 0, NULL, NULL);

}

void generate_random_macs(char (*mac_addr)[MAX_POS_NUM], int num) 
{
    uint8_t bytes[MAC_LEN_IN_BYTE];
    for (int j = 0; j < num; j ++)
    {
        for (int i = 0; i < MAC_LEN_IN_BYTE; ++i) {
            bytes[i] = rand() % 256; // 生成0到255的随机数
        }
        sprintf(mac_addr[j], "%02x:%02x:%02x:%02x:%02x:%02x", bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
    }
}

void * run_dhcp_req_process(void *param)
{
    int ret = 0;
    if (param == NULL)
        return NULL;
    
    simple_queue *proc_queue = ((thread_cxt *)(param))->proc_queue;

    // 2. 准备接收方的地址和端口
    struct sockaddr_in sock_addr = {0};
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(atoi(g_my_conf[CFG_DHCP_PORT].c_str()));
    sock_addr.sin_addr.s_addr = inet_addr(g_my_conf[CFG_DHCP_SERVER].c_str());

    mini_qps_clock t_clock(((thread_cxt *)(param))->proc_qps);
    t_clock.init();
    do{
        uint8_t *base = proc_queue->fitch_elem();
        
        if (base)
        {
            t_clock.clock();
            ret = sendto(g_sockfd, (base + sizeof(elem_base)), ((elem_base *)base)->data_len, 0, (struct sockaddr*)&sock_addr, sizeof(sock_addr));
            if (ret > 20)
            {
                ((thread_cxt *)(param))->proc_req_qps++;
                if (((elem_base *)base)->step == DHCPRELEASE)
                {   
                    //DHCPRELEASE 报文 DHCP_SERVER不进行应答；如果需要重复循环压测或者继续后续流程
                    //需要通过g_req_queue重新发起流程；并且释放原始的transid_cxt
                    int next_step = 0;
                    char mac_addr_str[32] = {'\0'};
                    //DHCPRELEASE以后 当前终端就不在拥有IP了 所以此时的your_ip就是 0.0.0.0
                    char your_ip_str[64] = {"0.0.0.0"};
                    int trans_id = ((elem_base *)base)->transid;
                    mutil_transid_context * transid_cxt = (mutil_transid_context *)(g_transid_pool->search_elem(trans_id));

                    if (transid_cxt != NULL)
                    {
                        next_step = transid_cxt->next_step;
                        strcpy(mac_addr_str, transid_cxt->mac_addr[0]);

                    }else
                    {
                        printf(">>>> transid cxt is invalied for time-out ....\n");
                        continue;
                    }
                    //如果g_perf_types[next_step]等于0，说明一个流程已经结束；随后从头开始
                    if (g_perf_types[next_step] == 0)
                    {
                        next_step = 0;
                    }
                    mutil_transid_context * transid_cxt_fitch = (mutil_transid_context *)g_transid_pool->fitch_elem();
                    if (!transid_cxt_fitch)
                    {
                        printf(">>>> g_transid_pool is full ....\n");
                        continue;
                    }
                    
                    queue_elem_m * msg_elem = (queue_elem_m *)calloc(1, sizeof(queue_elem_m));
                    (msg_elem->base).data_len = get_dhcp_req_pkg_v4(msg_elem->buff, 
                                            transid_cxt_fitch->transid, 
                                            g_perf_types[next_step],
                                            mac_addr_str, 
                                            your_ip_str,
                                            your_ip_str,
                                            g_my_conf[CFG_INTERFACE_IP].c_str());
                    strcpy(transid_cxt_fitch->mac_addr[0], mac_addr_str);
                    transid_cxt_fitch->next_step = next_step + 1;

                    (msg_elem->base).step = g_perf_types[next_step];
                    (msg_elem->base).transid = transid_cxt_fitch->transid;
                    proc_queue->push_elem((uint8_t *)msg_elem);

                    //归还 transid cxt
                    g_transid_pool->release_elem((uint8_t *)transid_cxt);
                }
            }
            free(base);
        }else{
            usleep(USLEEP_GAP);
            continue;
        }
    }while (true);
}


void * recv_dhcp_rsp_process(void *param)
{ 
    if (g_sockfd == -1)
        return NULL;
    
    char *str = NULL;
    uint16_t times = 0;
    struct sockaddr_in src;
    memset(&src, 0, sizeof(src));
    socklen_t src_len = sizeof(src);
    
    uint32_t trans_id = 0;
    uint32_t offer_ip = 0;
    char recv_buf[2048] = {'\0'};

    simple_queue *proc_queue = ((thread_cxt *)(param))->proc_queue;

    do{
        if(recvfrom(g_sockfd, recv_buf, 2048, 0, (sockaddr*)&src, &src_len) <= 20)
        {
            usleep(USLEEP_GAP);
            continue;
        }else
        {
            //get the dhcp resp
            ((thread_cxt *)(param))->proc_rsp_qps++;
            int next_step = 0;
            char your_ip_str[64] = {'\0'};
            char mac_addr_str[32] = {'\0'};
            str = inet_ntoa(src.sin_addr);
            //printf(">>>> receive the rsp from server ip %s, port %d ....\n", str, ntohs(src.sin_port));
            decode_dhcp_rsp_pkg(recv_buf, trans_id, offer_ip);
            //获取 transid cxt
            mutil_transid_context * transid_cxt = (mutil_transid_context *)(g_transid_pool->search_elem(trans_id));

            if (transid_cxt != NULL)
            {
                next_step = transid_cxt->next_step;
                uint8_t *pch = (uint8_t *)&offer_ip;
                sprintf(your_ip_str, "%d.%d.%d.%d", *(pch), *(pch+1), *(pch+2), *(pch+3));
                strcpy(mac_addr_str, transid_cxt->mac_addr[0]);
                
            }else
            {
                printf(">>>> transid cxt is invalied for time-out ....\n");
                continue;
            }
            //如果g_perf_types[next_step]等于0，说明一个流程已经结束；随后从头开始
            if (g_perf_types[next_step] == 0)
            {
                next_step = 0;
            }
            mutil_transid_context * transid_cxt_fitch = (mutil_transid_context *)g_transid_pool->fitch_elem();
            if (!transid_cxt_fitch)
            {
                printf(">>>> g_transid_pool is full ....\n");
                continue;
            }
            queue_elem_m * msg_elem = (queue_elem_m *)calloc(1, sizeof(queue_elem_m));
            (msg_elem->base).data_len = get_dhcp_req_pkg_v4(msg_elem->buff, 
                                    transid_cxt_fitch->transid, 
                                    g_perf_types[next_step],
                                    mac_addr_str, 
                                    your_ip_str,
                                    your_ip_str,
                                    g_my_conf[CFG_INTERFACE_IP].c_str());
            strcpy(transid_cxt_fitch->mac_addr[0], mac_addr_str);
            transid_cxt_fitch->next_step = next_step + 1;

            (msg_elem->base).step = g_perf_types[next_step];
            (msg_elem->base).transid = transid_cxt_fitch->transid;
            proc_queue->push_elem((uint8_t *)msg_elem);

            //归还 transid cxt
            g_transid_pool->release_elem((uint8_t *)transid_cxt);
        }

    }while (true);
}

void * dhcp_run_stats(void *param)
{ 
    int wait_times = 1;
    uint64_t req_num_per = 0;
    uint64_t rsp_num_per = 0;
    do {

        time_to_wait(wait_times);
        req_num_per = 0;
        rsp_num_per = 0;
        for (int i = 0; i < g_perf_thread_num; i++)
        {
            req_num_per += run_cxt[i]->proc_req_qps - run_cxt[i]->proc_req_qps_pre;
            rsp_num_per += run_cxt[i]->proc_rsp_qps - run_cxt[i]->proc_rsp_qps_pre;
            run_cxt[i]->proc_req_qps_pre = run_cxt[i]->proc_req_qps;
            run_cxt[i]->proc_rsp_qps_pre = run_cxt[i]->proc_rsp_qps;
        }
        printf(">>>> dhcp_client send %d requests ; recv %d responses in %d seconds <<<<\n ", req_num_per, rsp_num_per, wait_times);
    }while(true);
}

int main(int argc, char** argv)
{
    int ret = -1;
    int queue_size = 0;
    uint32_t rsp_trans_id = 0;
    uint32_t rand_transid_offset = 0;

    init_dhcp_type_info();
    if (!get_dhcp_client_conf())
    {
        return ret;
    }

    srand((unsigned int)time(NULL));
    g_transid = (rand() + queue_size);
    queue_size = atoi(g_my_conf[CFG_TOTAL_QPS].c_str())*2;
    g_run_mode = atoi(g_my_conf[CFG_DHCP_RUN_MODE].c_str());

    //定时器socket
    g_wait_sockfd = socket(AF_INET, SOCK_DGRAM, 0); 
    
    // 1、使用socket()函数获取一个socket文件描述符，并且设置为非阻塞
    g_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (-1 == g_sockfd)
    {
        printf("socket open err.");
        return ret;
    }
    setNonBlock(g_sockfd);

    if (g_run_mode == RUN_MODE_TEST)
    {
        // 2. 准备接收方的地址和端口
        struct sockaddr_in sock_addr = {0};
        sock_addr.sin_family = AF_INET;
        sock_addr.sin_port = htons(atoi(g_my_conf[CFG_DHCP_PORT].c_str()));
        sock_addr.sin_addr.s_addr = inet_addr(g_my_conf[CFG_DHCP_SERVER].c_str());
    
        // 3. 发送数据到指定的ip和端口
        char sendbuf[1024]={'0'};
        memset(sendbuf, '0', sizeof(1024));
        
        // 4. 解析消息类型流程：CFG_DHCP_MSG_TYPE可配置为 1;101;102;7 的组合
        int num1 = 0;
        char *pos1[MAX_POS_NUM];    
        char type_list[128] = {'\0'};
        strncpy(type_list, g_my_conf[CFG_DHCP_MSG_TYPE].c_str(), 128);
        split_formate_str(type_list, ";", pos1, num1);
    
        for (int t1 = 0; t1 < num1; t1++)
        {
            //再次切割，获取操作msg_type对应的延时操作时间
            int num2 = 0;
            char *pos2[MAX_POS_NUM];  
            uint16_t wait_times = 0;
            split_formate_str(pos1[t1], "-", pos2 ,num2);
            if (num2 > 2)
            {
                printf(">>>> !!!!  dhcp_client.conf dhcp_msg_type formate error....\n");
                close(g_sockfd);
                close(g_wait_sockfd);
                return -1;
            }
            uint8_t msg_type = atoi(pos2[0]);
            if (num2 > 1)
                wait_times = atoi(pos2[1]);
    
            //int cnt = get_dhcp_req_pkg_v4(sendbuf, g_transid, msg_type);
            int cnt = get_dhcp_req_pkg_v4(sendbuf, g_transid, msg_type, 
                                          g_my_conf[CFG_CLIENT_MAC].c_str(), 
                                          g_my_conf[CFG_CLIENT_IP].c_str(),
                                          g_my_conf[CFG_YOUR_IP].c_str(),
                                          g_my_conf[CFG_INTERFACE_IP].c_str());
            if (msg_type == DHCPDISCOVER) 
            {
                //discover阶段，client_addr会参与校验，必须是0.0.0.0
                if (strcmp(g_my_conf[CFG_CLIENT_IP].c_str(),"0.0.0.0") != 0)
                {
                    printf(">>>> !!!! discover req formate error, client addr %s must be 0.0.0.0 ....\n", g_my_conf[CFG_CLIENT_IP].c_str());
                    close(g_sockfd);
                    close(g_wait_sockfd);
                    return -1;
                }
            }else {
                //其他 request 阶段，client_addr不应该是0.0.0.0
                if (strcmp(g_my_conf[CFG_CLIENT_IP].c_str(),"0.0.0.0") == 0)
                {
                    printf(">>>> !!!! discover req formate error, client addr %s must not be 0.0.0.0 ....\n", g_my_conf[CFG_CLIENT_IP].c_str());
                    close(g_sockfd);
                    return -1;
                }
            }
            
            ret = sendto(g_sockfd, sendbuf, cnt, 0, (struct sockaddr*)&sock_addr, sizeof(sock_addr));
            if (ret > 0)
            {
                printf("send dhcp %s transid %d succssed , send length is %d ....\n", g_msg_type_detail[msg_type] ,g_transid, ret);
                recv_dhcp_rsp_v4(g_sockfd, g_transid, msg_type);
    
                //设置延时
                if (wait_times > 0)
                    time_to_wait(wait_times);
            }else{
                printf("send dhcp req failed , check dhcp server config ....\n");
            }
            
            g_transid ++;
        }
    }else if (g_run_mode == RUN_MODE_PERF){
        //初始化 dhcp 的 transid context 池 begin
        rand_transid_offset = g_transid - queue_size;
        g_transid_pool= new mutil_queue(queue_size, rand_transid_offset);
        g_transid_pool->init();
        uint8_t * transid_mem_pool = (uint8_t *)calloc(queue_size, sizeof(mutil_transid_context));
        if (transid_mem_pool == NULL)
        {
            printf("get transid mem pool failed......\n");
            return false;
        }
        for (int i = 0; i <queue_size; i++)
        {
            mutil_transid_context * transid_cxt = (mutil_transid_context *)(transid_mem_pool + sizeof(mutil_transid_context) * i);

            transid_cxt->base.id = i;
            transid_cxt->next_step = 0;
            transid_cxt->transid = rand_transid_offset + i;
            generate_random_macs(transid_cxt->mac_addr, 1);
            g_transid_pool->add_elem((uint8_t *)transid_cxt);
        }
        //初始化 dhcp 的 transid context 池 end

        //解析消息类型；在性能测试的时候，不可以进行延时操作
        int num1 = 0;
        char *pos1[MAX_POS_NUM];    
        char type_list[128] = {'\0'};
        memset(g_perf_types, 0, sizeof(g_perf_types));
        strncpy(type_list, g_my_conf[CFG_DHCP_MSG_TYPE].c_str(), 128);
        split_formate_str(type_list, ";", pos1, num1);
        for (int t1 = 0; t1 < num1; t1++)
        {
            //再次切割，在性能测试的时候，不可以进行延时操作
            int num2 = 0;
            char *pos2[MAX_POS_NUM];  
            uint16_t wait_times = 0;
            split_formate_str(pos1[t1], "-", pos2 ,num2);
            if (num2 > 2)
            {
                printf(">>>> !!!!  dhcp_client.conf dhcp_msg_type formate error....\n");
                close(g_sockfd);
                close(g_wait_sockfd);
                return -1;
            }
            uint8_t msg_type = atoi(pos2[0]);
            g_perf_types[t1] = msg_type;
        }

        mutil_transid_context *transid_ele = NULL;
        g_total_qps = atoi(g_my_conf[CFG_TOTAL_QPS].c_str());
        g_perf_thread_num = g_total_qps/MAX_SINGLE_THREAD_QPS + 1;
        if (g_total_qps%MAX_SINGLE_THREAD_QPS == 0)
            g_perf_thread_num -= 1;
        if (g_perf_thread_num > MAX_THREAD_NUM)
            g_perf_thread_num = MAX_THREAD_NUM;
        g_perf_thread_num = 1;
        for (int t1 = 0; t1 < g_perf_thread_num; t1++)
            g_req_queue[t1] = new simple_queue(g_total_qps);

        
        for (int t1 = 0; t1 < g_total_qps; t1 ++)
        {
            transid_ele = (mutil_transid_context *)(g_transid_pool->fitch_elem());
            if (transid_ele == NULL)
            {
                printf(">>>> !!!!  transid pool is null .....\n");
                usleep(USLEEP_GAP);
            }else{
                queue_elem_m * msg_elem = (queue_elem_m *)calloc(1, sizeof(queue_elem_m));
                (msg_elem->base).data_len = get_dhcp_req_pkg_v4(msg_elem->buff, 
                                    transid_ele->transid, 
                                    g_perf_types[0],
                                    transid_ele->mac_addr[0], 
                                    g_my_conf[CFG_CLIENT_IP].c_str(),
                                    g_my_conf[CFG_YOUR_IP].c_str(),
                                    g_my_conf[CFG_INTERFACE_IP].c_str());
                transid_ele->next_step ++;
                (msg_elem->base).step = g_perf_types[0];
                (msg_elem->base).transid = transid_ele->transid;

                g_req_queue[t1%g_perf_thread_num]->push_elem((uint8_t *)msg_elem);
            }
        }

        for (int i = 0; i < g_perf_thread_num; i++)
        {
            run_cxt[i] = (thread_cxt*)calloc(1, sizeof(thread_cxt));
            run_cxt[i]->proc_queue = g_req_queue[i];
            run_cxt[i]->proc_qps = MAX_SINGLE_THREAD_QPS;
            if (i == g_perf_thread_num -1)
            {
                run_cxt[i]->proc_qps = g_total_qps - MAX_SINGLE_THREAD_QPS*(g_perf_thread_num -1);
            }
            pthread_create(&thid_recv[i], NULL, recv_dhcp_rsp_process, (void *)run_cxt[i]); 
            pthread_create(&thid_req[i], NULL, run_dhcp_req_process, (void *)run_cxt[i]);
        }
        pthread_create(&thid_stat, NULL, dhcp_run_stats, NULL); 
        for (int i = 0; i < g_perf_thread_num; i++)
        {
            pthread_join(thid_req[i], NULL);
            pthread_join(thid_recv[i], NULL);
        }

        pthread_join(thid_stat, NULL);
    }


    // 4. 关闭套接字
    if (g_run_mode == RUN_MODE_TEST)
    {
        close(g_sockfd);
        close(g_wait_sockfd);
    }

    printf(">>>> !!!!  main thread existed .....\n");
}




