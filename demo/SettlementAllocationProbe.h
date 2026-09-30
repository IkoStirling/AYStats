#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
namespace metrics {
std::atomic_size_t calls=0,live=0,peak=0;
struct alignas(std::max_align_t) Header {void* raw;std::size_t bytes;};
void* allocate(std::size_t bytes,std::size_t alignment) {
    alignment=std::max(alignment,alignof(Header));
    if(bytes>SIZE_MAX-alignment-sizeof(Header))throw std::bad_alloc();
    auto* raw=std::malloc(bytes+alignment+sizeof(Header));if(!raw)throw std::bad_alloc();
    const auto address=(reinterpret_cast<std::uintptr_t>(raw)+sizeof(Header)+alignment-1)&~(alignment-1);
    auto* header=reinterpret_cast<Header*>(address)-1;header->raw=raw;header->bytes=bytes;
    calls.fetch_add(1);const auto total=live.fetch_add(bytes)+bytes;auto observed=peak.load();
    while(observed<total && !peak.compare_exchange_weak(observed,total)){}
    return reinterpret_cast<void*>(address);
}
void release(void* ptr) noexcept {if(!ptr)return;auto* header=static_cast<Header*>(ptr)-1;live.fetch_sub(header->bytes);std::free(header->raw);}
std::size_t reset(){calls=0;const auto initial=live.load();peak=initial;return initial;}
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
}
void* operator new(std::size_t n){return metrics::allocate(n,alignof(std::max_align_t));}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {metrics::release(p);}
void operator delete[](void* p) noexcept {metrics::release(p);}
void operator delete(void* p,std::size_t) noexcept {metrics::release(p);}
void operator delete[](void* p,std::size_t) noexcept {metrics::release(p);}
void* operator new(std::size_t n,std::align_val_t a){return metrics::allocate(n,std::size_t(a));}
void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void* p,std::align_val_t) noexcept {metrics::release(p);}
void operator delete[](void* p,std::align_val_t) noexcept {metrics::release(p);}
void operator delete(void* p,std::size_t,std::align_val_t) noexcept {metrics::release(p);}
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept {metrics::release(p);}
