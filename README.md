基于⾕歌开源项目tcmalloc的简化版⾼并发内存池，采用 Thread Cache、Central Cache、Page Cache 三层内存管理架构，支持多线程环境下的低延迟内存分配。可以替换系统默认的 malloc/free，实现更⾼效的动态内存分配机制。
