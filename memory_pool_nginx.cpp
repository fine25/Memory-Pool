#include "ngx_mem_pool.h"

#include <iostream>
#include <stdlib.h>
#include <string.h>
using namespace std;


typedef struct Data stData;
struct Data
{
    char* ptr;
    FILE* pfile;
};

void func1(void* temp)
{
    char* p = (char*)temp;
    cout<<"free ptr mem!"<<endl;
    free(p);
}
void func2(void* pf1)
{
    FILE* pf = (FILE*)pf1;
    cout<<"close file!"<<endl;
    fclose(pf);
}
int main()
{
    ngx_mem_pool mempool;
    // 512 - sizeof(ngx_pool_t) - 4095   =>   max
   
    if (mempool.ngx_create_pool(512) == nullptr)
    {
        cout<<"ngx_create_pool fail..."<<endl;
        return -1;
    }

    void* p1 = mempool.ngx_palloc(128); // 从小块内存池分配的
    if (p1 == nullptr)
    {
        cout << "ngx_palloc 128 bytes fail..." << endl;
        return -1;
    }

    stData* p2 = (stData*)mempool.ngx_palloc(512); // 从大块内存池分配的
    if (p2 == nullptr)
    {
        cout<<"ngx_palloc 512 bytes fail..."<<endl;
        return -1;
    }
    p2->ptr = (char*)malloc(12);
    strcpy(p2->ptr, "hello world");
    p2->pfile = fopen("data.txt", "w");

    ngx_pool_cleanup_s* c1 = mempool.ngx_pool_cleanup_add( sizeof(char*));
    c1->handler = func1;
    c1->data = p2->ptr;

    ngx_pool_cleanup_s* c2 = mempool.ngx_pool_cleanup_add( sizeof(FILE*));
    c2->handler = func2;
    c2->data = p2->pfile;

    mempool.ngx_destory_pool(); // 1.调用所有的预置的清理函数 2.释放大块内存 3.释放小块内存池所有内存

    return 0;
}
