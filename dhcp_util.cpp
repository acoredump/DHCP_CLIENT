#include "dhcp_util.h"

mutil_queue::mutil_queue(int max_ele_num, uint32_t offset_id)
{
    pthread_spin_init(&_lock, 0);
    _p_header = NULL;
    _p_tailer = NULL;
    _max_ele_num = max_ele_num;
    _cur_ele_num = 0;
    _cur_ele_num_lru = 0;
    _offset_id = offset_id;
    _p_header_lru = NULL;
    _p_tailer_lru = NULL;
}

bool mutil_queue::init()
{
    _used_maps = (base_map *)malloc(_max_ele_num * sizeof(base_map));
    //所有队列maps对象处于初始化状态
    if (_used_maps == NULL)
        return false;
    for (int count = 0; count < _max_ele_num; count ++)
        _used_maps[count].tag = INIT_STATUS;
    return true;
}

bool mutil_queue::add_elem(uint8_t *elem)
{
    pthread_spin_lock(&_lock);
    do
    {
        if (_cur_ele_num > _max_ele_num)
            break;
        mutil_base *base = (mutil_base *)elem;
        base->p_lru_next = NULL;
        base->p_lru_prev = NULL;
        if(_used_maps[base->id].tag == INIT_STATUS)
        {
            if (_p_tailer == _p_header && _p_tailer == NULL)
            {
                _p_header = elem;
                _p_tailer = elem;
                base->p_next = NULL;
            }else
            {
                mutil_base *base = (mutil_base *)_p_tailer;
                base->p_next = elem;
                _p_tailer = elem;
                base = (mutil_base *)_p_tailer;
                base->p_next = NULL;
            }
            _used_maps[base->id ].tag = FREE_STATUS;
            _used_maps[base->id ]._p_elem = NULL;
        }
        _cur_ele_num ++;
    }while (0);

    pthread_spin_unlock(&_lock);
    return true;
}

uint8_t * mutil_queue::fitch_elem()
{
    uint8_t *rp_elem = NULL;
    pthread_spin_lock(&_lock);

    do
    {
        //取队列首元素
        if(_cur_ele_num > 0)
        {
            rp_elem = _p_header;
            mutil_base *base = (mutil_base *)_p_header;
            _used_maps[base->id ].tag = USED_STATUS;
            //记录取出的元素
            _used_maps[base->id ]._p_elem =_p_header;

            _p_header = base->p_next;

            base->p_next = NULL;
            _cur_ele_num--;
            if (_cur_ele_num == 0)
            {
                _p_header = NULL;
                _p_tailer = NULL;
            }

            //放入lru队列（从队列尾部加入，队首开始的元素是存在时间最长的）
            if (_p_header_lru == _p_tailer_lru && _p_header_lru == NULL)
            {
                //空队列
                _p_header_lru = rp_elem;
                _p_tailer_lru = _p_header_lru;
                ((mutil_base *)_p_tailer_lru)->p_lru_next = NULL;
                ((mutil_base *)_p_tailer_lru)->p_lru_prev = NULL;
            }else
            {
                //队尾入
                ((mutil_base *)_p_tailer_lru)->p_lru_next = rp_elem;
                ((mutil_base *)rp_elem)->p_lru_prev = _p_tailer_lru;
                ((mutil_base *)rp_elem)->p_lru_next = NULL;
                _p_tailer_lru = rp_elem;

            }
            _cur_ele_num_lru ++;
        }else
        {
            //从lru队列头部开始回收 lru队列的ele，回收至少3分之1的队列元素
            printf("current free elem num is %d, total elem num is %d, total lru elem num is %d\n", _cur_ele_num,
                   _max_ele_num,
                   _cur_ele_num_lru);
            while(_cur_ele_num < (_max_ele_num/3))
            {
                //从lru以及_used_maps中摘除
                mutil_base *p_recycle = (mutil_base *)_p_header_lru;
                if (p_recycle == NULL)
                    break;
                //需要对回收的队列元素做的操作 to do 

                _p_header_lru = p_recycle->p_lru_next;
                if (_p_header_lru != NULL)
                {
                    ((mutil_base *)_p_header_lru)->p_lru_prev = NULL;
                }else
                {
                    //最后一个可回收的ele
                    printf("current free elem num is %d, total elem num is %d\n", _cur_ele_num, _max_ele_num);
                    _p_header_lru = NULL;
                    _p_tailer_lru = NULL;

                }
                _used_maps[p_recycle->id ].tag = FREE_STATUS;
                _used_maps[p_recycle->id ]._p_elem = NULL;
                p_recycle->p_lru_next = NULL;
                p_recycle->p_lru_prev = NULL;

                //放入到空闲队列
                if (_cur_ele_num > 0)
                {
                    ((mutil_base *)_p_tailer)->p_next = (uint8_t *)p_recycle;
                    _p_tailer = (uint8_t *)p_recycle;
                }else if(_cur_ele_num == 0)
                {
                    //当前空闲队列为空
                    _p_header = (uint8_t *)p_recycle;
                    _p_tailer = _p_header;
                }
                ((mutil_base *)_p_tailer)->p_next = NULL;
                _cur_ele_num++;
                _cur_ele_num_lru--;
            }
            break;
        }
    }while(0);

    pthread_spin_unlock(&_lock);
    return rp_elem;
}

//使用id进行归还，后续也可以按照elem元素进行归还
uint8_t * mutil_queue::search_elem(uint32_t tid)
{
    uint8_t *res = NULL;
    uint8_t *lru_pos = NULL;
    uint32_t id = tid - _offset_id;
    if (id > _max_ele_num)
        return res;
    pthread_spin_lock(&_lock);
    do
    {
        if(_used_maps[id].tag == USED_STATUS)
        {
            res = _used_maps[id]._p_elem;
            //首先从lru中摘除，然后放入到空闲队列(大概率是lru队列队中摘除)
            lru_pos = res;
            if (_cur_ele_num_lru == 1)
            {
                //当前lru中只有一个元素
                ((mutil_base *)lru_pos)->p_lru_next = NULL;
                ((mutil_base *)lru_pos)->p_lru_prev = NULL;
                _p_tailer_lru = NULL;
                _p_header_lru = _p_tailer_lru;
            }else
            {
                if (((mutil_base *)lru_pos)->p_lru_prev != NULL)
                {
                    uint8_t * prev = ((mutil_base *)lru_pos)->p_lru_prev;
                    uint8_t * pnext = ((mutil_base *)lru_pos)->p_lru_next;
                    if (pnext != NULL)
                    {
                        //说明是队列中的元素
                        ((mutil_base *)prev)->p_lru_next = pnext;
                        ((mutil_base *)pnext)->p_lru_prev = prev;

                    }else
                    {
                        //说明是lru队列尾部，所以pnext == NULL
                        _p_tailer_lru = prev;
                        ((mutil_base *)_p_tailer_lru)->p_lru_next = NULL;
                    }
                }else
                {
                    //说明刚好是LRU队列头部
                    _p_header_lru = ((mutil_base *)lru_pos)->p_lru_next;
                    ((mutil_base *)_p_header_lru)->p_lru_prev = NULL;
                }
                ((mutil_base *)lru_pos)->p_lru_prev = NULL;
                ((mutil_base *)lru_pos)->p_lru_next = NULL;
            }
            _cur_ele_num_lru --;
        }else
        {
            break;
        }
    }while(0);

    #if 0
    assert(_p_header != NULL);
    if (((mutil_base *)_p_header)->p_next  == NULL)
    {
        printf("_pheader's next is null in search_release_elem and _cur_ele_num is %d..........\n", _cur_ele_num);
    }
    #endif

    pthread_spin_unlock(&_lock);
    return res;
}

void mutil_queue::release_elem(uint8_t *elem)
{
    pthread_spin_lock(&_lock);
    //加入到空闲队列
    mutil_base *base = (mutil_base *)elem;
    int id = base->id;

    if (_cur_ele_num > 0)
    {
        ((mutil_base *)_p_tailer)->p_next = _used_maps[id]._p_elem;
        _p_tailer = _used_maps[id]._p_elem;
    }else if(_cur_ele_num == 0)
    {
        //当前队列为空
        _p_header = _used_maps[id]._p_elem;
        _p_tailer = _p_header;
    }
    ((mutil_base *)_p_tailer)->p_next = NULL;
    _used_maps[id].tag = FREE_STATUS;
    _used_maps[id]._p_elem = NULL;
    _cur_ele_num++;
    pthread_spin_unlock(&_lock);
}


simple_queue::simple_queue(int max_ele_num) {
    pthread_spin_init(&lock, 0);
    _max_ele_num = max_ele_num;
    _cur_ele_num = 0;
    _p_header = NULL;
    _p_tailer = NULL;
}

bool simple_queue::push_elem(uint8_t *elem)
{
    bool rc = true;
    pthread_spin_lock(&lock);
    do{
        if (_cur_ele_num >= _max_ele_num)
        {
            rc = false;
            break;
        }
        if (_p_header == _p_tailer && _p_header == NULL)
        {
            //空队列
            _p_header = elem;
            _p_tailer = elem;
            elem_base *base = (elem_base *)elem;
            base->p_next = NULL;
        }else
        {
            //非空队列，尾部插入
            elem_base *base = (elem_base *)_p_tailer;
            base->p_next = elem;
            _p_tailer = elem;
            base = (elem_base *)_p_tailer;
            base->p_next = NULL;
        }
        _cur_ele_num ++;
    }while(0);
    pthread_spin_unlock(&lock);
    return rc;
}

uint8_t * simple_queue::fitch_elem()
{
    uint8_t *p_elem = NULL;
    pthread_spin_lock(&lock);
    do{
        if (_cur_ele_num == 0)
            break;
        p_elem = _p_header;
        elem_base *base = (elem_base *)p_elem;
        if (_cur_ele_num == 1)
        {
            //取完数据即为空队列
            _p_header = NULL;
            _p_tailer = NULL;
        }else
        {
            _p_header = base->p_next;
        }
        base->p_next = NULL;
        _cur_ele_num --;
    }while(0);

    pthread_spin_unlock(&lock);
    return p_elem;
}

mini_qps_clock::mini_qps_clock(uint32_t r_qps)
{
    _clock_qps = r_qps;
}


void mini_qps_clock::init()
{
    _clock_gap = _clock_qps > 0 ? 1000000000L / _clock_qps : 0;
    clock_gettime(CLOCK_MONOTONIC, &_next_mo);
    return ;
}

void mini_qps_clock::clock()
{
    if(_clock_gap)
	{
		struct timespec now_mo;
		clock_gettime(CLOCK_MONOTONIC, &now_mo);
		if(now_mo.tv_sec < _next_mo.tv_sec ||
			(now_mo.tv_sec == _next_mo.tv_sec && now_mo.tv_nsec < _next_mo.tv_nsec))
		{
			clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &_next_mo, NULL);
		}
		_next_mo.tv_nsec += _clock_gap;
		if(_next_mo.tv_nsec >= 1000000000)
		{
			_next_mo.tv_nsec -= 1000000000;
			_next_mo.tv_sec++;
		}
	}
    return ;
}


