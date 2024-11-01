#include "heapTimer.h"

// 构造函数 1，初始化一个大小为 cap 的空堆
time_heap::time_heap(int cap):capacity(cap),cur_size(0)
{
    array = new heap_timer *[capacity];
    assert(array);

    //std::fill(std::begin(array), std::end(array), 0);
    for (int i = 0; i < capacity; ++i)
    {
        array[i] = nullptr;
    }
}

// 构造函数2,用已有的数组来初始化堆
time_heap::time_heap(heap_timer** init_array,int size,int capacity):cur_size(size),capacity(capacity)
{
    assert(capacity >= size);

    array = new heap_timer *[capacity];
    assert(array);
    for (int i = 0; i < capacity;++i)
    {
        array[i] = nullptr;
    }
    if(size)
    {
        // 用已有数组初始化堆数据
        for (int i = 0; i < size;++i)
        {
            array[i] = init_array[i];
        }

        //对数组中的第(cur_size-1)/2 ~ 0个元素执行下滤操作
        for (int i=(cur_size-1)/2 ;i>=0; --i)
        {
            percolate_down(i);
        }
    }
}

// 销毁时间堆
time_heap::~time_heap()
{
    for (int i = 0; i < cur_size;i++)
    {
        delete array[i];
    }
    delete[] array;
}

// 添加目标定时器timer
void time_heap::add_timer(heap_timer*timer)
{
    if(!timer) {
       return; 
    }
    // 如果当前堆数组容量不够,则将其扩大1倍
    if(cur_size>=capacity){
        resize();
    }
    // 新插入一个元素,当前堆大小+1,hole是新建空穴位置
    int hole = cur_size++;
    int parent = 0;

    // 对从空穴到根节点的路径上的所有结点执行上滤操作
    while (hole>0)
    {
        parent = (hole - 1) / 2; // 数组下标从0开始,所以要-1
        if (array[parent]->expire<=timer->expire){   
            break;
        }
        // parent放到下面,hole上移
        array[hole] = array[parent];
        hole = parent;
    }
    array[hole] = timer;
}

// 删除目标定时器tiemr
void time_heap::del_timer(heap_timer*timer)
{
    if (!timer){
        return;
    }
    /* 
        延迟销毁，仅仅将目标定时器的回调函数设置为空。
        这将节省真正删除该定时器造成的开销，但使得堆数组容易膨胀
    */
    timer->call_back = nullptr;
}

// 获得堆顶部的定时器
heap_timer* time_heap::top() const
{
    if ( empty() )
    {
        return NULL;
    }
    return array[0];
}

// 删除堆顶部的定时器
void time_heap::pop_timer()
{
    if( empty() )
    {
        return;
    }

    if( array[0] )
    {
        delete array[0];

        // 将原来的堆顶元素替换为堆数组中的最后一个元素，再进行下滤操作
        array[0] = array[--cur_size];
        percolate_down( 0 );
    }
}

// 调整目标定时器位置
void time_heap::adj_timer( heap_timer* timer )
{
    if( !timer )
    {
        return;
    }
    // 找到目标定时器的位置
    int hole = 0;
    for(; hole < cur_size; hole++)
    {
        if( array[hole] == timer)
        {
            break;
        }
    }

    // 进行下滤，因为其到期时间延长了
    percolate_down(hole);
}

// 心搏函数
void time_heap::tick()
{
    // tmp 暂存堆顶元素
    heap_timer* tmp = array[0];
    
    // 当前时间
    time_t cur = time( NULL );
    
    // 循环处理堆中到期的定时器
    while( !empty() )
    {
        if( !tmp )
        {
            break;
        }

        // 如果堆顶定时器没到期，则退出循环。因为是最小堆，所有只需判断堆顶。
        if( tmp->expire > cur )
        {
            break;
        }

        /* 
            判断当前堆顶定时器是否被删除了
            因为在 del_timer 中采用了延迟删除的方法，只是将其回调函数置 NULL，其还在堆中
        */
        if( array[0]->call_back )
        {
            array[0]->call_back( array[0]->user_data );
        }

        // 删除当前堆顶，同时生成新的堆顶定时器
        pop_timer();
        tmp = array[0];
    }
}

// 下滤操作，保证数组中的每个结点都满足最小堆性质
void time_heap::percolate_down( int hole )
{
    heap_timer* temp = array[hole];
    int child = 0;
    for ( ; ((hole*2+1) <= (cur_size-1)); hole=child )
    {
        child = hole*2+1;
        if ( (child < (cur_size-1)) && (array[child+1]->expire < array[child]->expire ) )
        {
            ++child;
        }
        if ( array[child]->expire < temp->expire )
        {
            array[hole] = array[child];
        }
        else
        {
            break;
        }
    }
    array[hole] = temp;
}

// 将堆数组扩大一倍
void time_heap::resize()
{
    heap_timer** temp = new heap_timer* [2*capacity];
    for( int i = 0; i < 2*capacity; ++i )
    {
        temp[i] = NULL;
    }
    assert(temp);
    capacity = 2 * capacity;
    for ( int i = 0; i < cur_size; ++i )
    {
        temp[i] = array[i];
    }
    delete [] array;
    array = temp;
}
