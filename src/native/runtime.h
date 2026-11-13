#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <vector>
#include <array>

struct RV {
    union { double real; int64_t integer; void* ptr; } value{};
    uint32_t flags=0;
    uint32_t kind=5;
    static RV number(double n) { RV r; r.value.real=n; r.kind=0; return r; }
    bool numeric() const { auto k=kind&0xffffff;return k==0 || k==7 || k==10 || k==13; }
    bool handle() const { return numeric() || (kind&0xffffff)==15; }
    double number() const { auto k=kind&0xffffff;return k==10 ? static_cast<double>(value.integer) : k==7 ? static_cast<double>(static_cast<int32_t>(value.integer)) : value.real; }
    bool sameHandle(const RV& other) const { return numeric() && other.numeric() ? number()==other.number() : (kind&0xffffff)==15 && (other.kind&0xffffff)==15 && value.integer==other.value.integer; }
};
static_assert(sizeof(RV)==16);
struct Shared {
    uint32_t magic,version;
    volatile LONG flags,status;
    volatile DWORD clientTick;
    volatile LONG hookCount;
    volatile LONG64 hits[7];
    volatile LONG64 lastItem;
    volatile double lastAmount;
    volatile LONG lastQuality,lastAffixes;
    volatile double lastSpeed;
    volatile LONG testCommand,testStatus;
    char detail[512];
};
static_assert(offsetof(Shared,hits)==24 && offsetof(Shared,lastItem)==80 && offsetof(Shared,lastSpeed)==104 && offsetof(Shared,detail)==120);
using Script=RV* (*)(void*,void*,RV*,int,RV**);
using Event=void (*)(void*,void*);
using Builtin=RV* (*)(void*,void*,RV*,int,int,RV**);

inline RV* array_at(RV* r,int n) {
    if(!r || (r->kind&0xffffff)!=2 || !r->value.ptr || n<0) return nullptr;
    auto p=static_cast<uint8_t*>(r->value.ptr);
    int length=*reinterpret_cast<int*>(p+0x24);
    if(n>=length || length>1000000) return nullptr;
    return *reinterpret_cast<RV**>(p+8)+n;
}
inline int array_size(RV* r) {
    if(!r || (r->kind&0xffffff)!=2 || !r->value.ptr) return 0;
    return *reinterpret_cast<int*>(static_cast<uint8_t*>(r->value.ptr)+0x24);
}

inline bool multiplied_quantity(const RV* r,double& out) {
    if(!r || !r->numeric()) return false;
    double n=r->number();
    if(!std::isfinite(n) || n<=0 || n>214748.3647) return false;
    out=n*10000.0;
    return true;
}
