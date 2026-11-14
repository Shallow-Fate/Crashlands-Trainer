#include "runtime.h"
#include "offsets.h"
#include "../../vendor/minhook/include/MinHook.h"
#include <intrin.h>
#include <cstdio>
#include <cstring>
#ifdef TRAINER_TEST
static uintptr_t testCaller=0;
#define CALLER_RVA testCaller
#else
#define CALLER_RVA (reinterpret_cast<uintptr_t>(_ReturnAddress())-base)
#endif

static uintptr_t base=0;
static Shared* shared=nullptr;
static HANDLE mapping=nullptr;
static Script originalGain,originalCraft,originalGenerate,originalResource,originalDrop;
static Script originalRecipe,originalContains,originalRandomList,originalWeighted;
static Script originalTake,originalRecipeTime,originalQueue,originalEquipStat,originalSwing;
static Script originalLegendary;
static Builtin originalBuiltin;
using GetVar=RV* (*)(void*,int);
using SetVar=RV* (*)(void*,int);
using RandomReal=double (*)(double);
static GetVar originalGet;
static RandomReal originalRandom;
static thread_local int craftDepth=0,takeDepth=0,resourceDepth=0,recipeDepth=0,legendaryDepth=0;
struct EquipmentContext { std::vector<int> pool; EquipmentContext* previous; };
struct DropContext { std::vector<int> indices; size_t cursor=0; DropContext* previous; };
struct RecipeContext { std::vector<int> picked; int chosen=-1; RecipeContext* previous; };
static thread_local EquipmentContext* equipmentContext=nullptr;
static thread_local DropContext* dropContext=nullptr;
static thread_local RecipeContext* recipeContext=nullptr;

static bool enabled(int i) { return shared && (shared->flags&(1<<i)) && DWORD(GetTickCount()-shared->clientTick)<2500; }
static void hit(int i) { if(shared) InterlockedIncrement64(&shared->hits[i]); }
static int id(uintptr_t rva) { return *reinterpret_cast<int*>(base+rva); }
static RV* global(uintptr_t rva) {
    auto object=*reinterpret_cast<void**>(base+offsets::globals);
    if(!object || id(rva)<0) return nullptr;
    auto vt=*reinterpret_cast<uintptr_t**>(object);
    return reinterpret_cast<GetVar>(vt[1])(object,id(rva));
}
static RV* instanceVar(void* object,uintptr_t rva) {
    if(!object || id(rva)<0) return nullptr;
    auto vt=*reinterpret_cast<uintptr_t**>(object);
    return reinterpret_cast<GetVar>(vt[1])(object,id(rva));
}
static RV* instanceVarSet(void* object,uintptr_t rva) {
    if(!object || id(rva)<0) return nullptr;
    auto vt=*reinterpret_cast<uintptr_t**>(object);
    return reinterpret_cast<SetVar>(vt[2])(object,id(rva));
}
static void setNumber(RV* r,double n) {
    if(!r) return;
    if((1u<<(r->kind&31))&0x46) reinterpret_cast<void(*)(RV*)>(base+offsets::rv_free)(r);
    *r=RV::number(n);
}
static RV call(void* self,void* other,uintptr_t variable,std::initializer_list<RV*> args) {
    RV result;
    originalBuiltin(self,other,&result,static_cast<int>(args.size()),id(variable),const_cast<RV**>(args.begin()));
    return result;
}
static int listSize(void* s,void* o,RV* list) {
    RV r=call(s,o,offsets::var_ds_list_size,{list});
    return r.numeric()?std::clamp(static_cast<int>(r.number()),0,4096):0;
}
static RV listValue(void* s,void* o,RV* list,int index) {
    RV n=RV::number(index);
    return call(s,o,offsets::var_ds_list_find_value,{list,&n});
}
static bool materialType(int type) {
    switch(type) {
    case 10: case 11: case 12: case 13: case 14: case 16: case 17: case 24: case 26: case 35: case 36: return true;
    default:return false;
    }
}
static int itemType(int item) {
    RV* r=array_at(array_at(global(offsets::var_ITEMINFO),item),3);
    return r&&r->numeric()?static_cast<int>(r->number()):-1;
}
static bool equipmentType(int type) { return (type>=0 && type<=5)||type==7; }
static bool stackableItem(int item) {
    // gain_item merges database stacks only when ITEMINFO[item][4] < 0 or > 1.
    // Values 0 and 1 require a separate inventory record for every acquired item.
    RV* limit=array_at(array_at(global(offsets::var_ITEMINFO),item),4);
    return limit && limit->numeric() && std::isfinite(limit->number()) &&
           (limit->number()<0 || limit->number()>1);
}
static bool quantityEligible(int item) {
    if(!stackableItem(item)) return false;
    int type=itemType(item);
    return materialType(type) || ((craftDepth || takeDepth) && type>=0 &&
           !equipmentType(type) && type!=27 && type!=38);
}
// infer_stat_amt supports 28 IDs. Three are weapon base stats, not universal affixes.
static bool equipmentAffix(int stat) {return stat>=0 && stat<29 && stat!=3 && stat!=4 && stat!=6 && stat!=24;}

static RV* hookGain(void* s,void* o,RV* r,int n,RV** a) {
    double quantity;
    if(enabled(0) && n>=2 && n<=16 && a[0] && a[0]->numeric() &&
       std::isfinite(a[0]->number()) && a[0]->number()>=0 && a[0]->number()<1000000 &&
       quantityEligible(static_cast<int>(a[0]->number())) && multiplied_quantity(a[1],quantity)) {
        RV amount=RV::number(quantity);
        std::array<RV*,16> copy{};std::copy(a,a+n,copy.begin());copy[1]=&amount;
        hit(0);shared->lastItem=static_cast<LONG64>(a[0]->number());shared->lastAmount=quantity;
        return originalGain(s,o,r,n,copy.data());
    }
    return originalGain(s,o,r,n,a);
}
static RV* hookTake(void* s,void* o,RV* r,int n,RV** a) {
    ++takeDepth;RV* result=originalTake(s,o,r,n,a);--takeDepth;return result;
}
static RV* hookSwing(void* s,void* o,RV* r,int n,RV** a) {
    if(!enabled(6) || !s) return originalSwing(s,o,r,n,a);
    RV* duration=instanceVar(s,offsets::var_gatherswing_duration);
    RV* maxcool=instanceVar(s,offsets::var_gatherswing_maxcool);
    if(!duration || !maxcool || !duration->numeric() || !maxcool->numeric()) return originalSwing(s,o,r,n,a);
    RV savedDuration=*duration,savedMaxcool=*maxcool;
    RV* durationSet=instanceVarSet(s,offsets::var_gatherswing_duration);
    RV* maxcoolSet=instanceVarSet(s,offsets::var_gatherswing_maxcool);
    if(!durationSet || !maxcoolSet) return originalSwing(s,o,r,n,a);
    // Keep the native swing, damage and drop path; only collapse the gather swing timing.
    *durationSet=RV::number(0.000001);*maxcoolSet=RV::number(0.000001);
    RV* result=originalSwing(s,o,r,n,a);
    *durationSet=savedDuration;*maxcoolSet=savedMaxcool;
    hit(6);
    return result;
}
static RV* hookRecipeTime(void* s,void* o,RV* r,int n,RV** a) {
    if(enabled(5)) { setNumber(r,0.000001);hit(5);return r; }
    return originalRecipeTime(s,o,r,n,a);
}
static RV* hookQueue(void* s,void* o,RV* r,int n,RV** a) {
    RV* result=originalQueue(s,o,r,n,a);
    if(enabled(5)) {
        // Completed queues are advanced by the normal station update on the next frame.
        RV* stations=global(offsets::var_STATION_DATA);
        for(int i=0;i<array_size(stations);i++) {
            RV* remaining=array_at(array_at(stations,i),1);
            if(remaining && remaining->numeric() && remaining->number()>0) setNumber(remaining,0.000001);
        }
    }
    return result;
}
static RV* hookCraft(void* s,void* o,RV* r,int n,RV** a) {
    ++craftDepth;
    RV* result=originalCraft(s,o,r,n,a);
    --craftDepth;
    return result;
}
static RV* hookGenerate(void* s,void* o,RV* r,int n,RV** a) {
    uintptr_t caller=CALLER_RVA;
    // Station collection and Juiceforge rerolls both use this native generator.
    // Match only their verified call sites so eligibility, juice costs, the existing
    // equipment index and the native refresh after a reroll remain intact.
    bool equipmentSource=craftDepth || takeDepth || caller==offsets::station_generate_return || caller==offsets::reforge_generate_return;
    if(!enabled(1) || !equipmentSource || n<2 || n>16) return originalGenerate(s,o,r,n,a);
    RV quality=RV::number(4);
    if(a[1]->numeric()) quality.value.real=std::max(4.0,a[1]->number());
    if(a[0]->numeric()) {
        RV* min=array_at(array_at(global(offsets::var_ITEMINFO),static_cast<int>(a[0]->number())),9);
        if(min&&min->numeric()) quality.value.real=std::max(quality.number(),min->number());
    }
    std::array<RV*,16> copy{};std::copy(a,a+n,copy.begin());copy[1]=&quality;
    EquipmentContext context{{},equipmentContext};equipmentContext=&context;
    RV* result=originalGenerate(s,o,r,n,copy.data());
    equipmentContext=context.previous;
    if(result && result->numeric()) {
        RV* equip=array_at(global(offsets::var_EQUIPMENT),static_cast<int>(result->number()));
        RV* stats=array_at(equip,3);
        if(stats && stats->handle()) {
            bool weaponBase[7]{};
            for(int i=0,size=listSize(s,o,stats);i<size;i++){RV value=listValue(s,o,stats,i);if(value.numeric()){int st=static_cast<int>(value.number());if(st==3||st==4||st==6)weaponBase[st]=true;}}
            call(s,o,offsets::var_ds_list_clear,{stats});
            for(int stat=0;stat<29;stat++) if(equipmentAffix(stat) || (stat<7&&weaponBase[stat])) { RV value=RV::number(stat);call(s,o,offsets::var_ds_list_add,{stats,&value}); }
            shared->lastAffixes=listSize(s,o,stats);
        }
        shared->lastQuality=static_cast<LONG>(quality.number());
        RV dummy;RV* args[]={result};
        reinterpret_cast<Script>(base+offsets::set_equipment_name)(s,o,&dummy,1,args);
        hit(1);
    }
    return result;
}
static bool extremeEquipment(void* s,void* o,int equipment) {
    RV* stats=array_at(array_at(global(offsets::var_EQUIPMENT),equipment),3);
    if(!stats || !stats->handle()) return false;
    int size=listSize(s,o,stats);if(size<25 || size>28)return false;
    int expected=0,previous=-1;
    for(int i=0;i<size;i++) {RV stat=listValue(s,o,stats,i);if(!stat.numeric())return false;int st=static_cast<int>(stat.number());if(st<=previous||st<0||st>=29||st==24)return false;previous=st;if(equipmentAffix(st))++expected;}
    return expected==25;
}
// STATINFO entry index 4 is the game's own hard cap for a stat. Zero means the stat has no
// finite native cap; those values must keep the original infer/quality/level formula.
static double nativeStatCap(int stat) {
    RV* cap=array_at(array_at(global(offsets::var_STATINFO),stat),4);
    if(!cap || !cap->numeric()) return 0;
    double value=cap->number();
    return std::isfinite(value)&&value>0?value:0;
}
static RV* hookEquipStat(void* s,void* o,RV* r,int n,RV** a) {
    if(enabled(1) && n==2 && a[0]->numeric() && a[1]->numeric()) {
        int stat=static_cast<int>(a[1]->number());
        int equipment=static_cast<int>(a[0]->number());
        if(equipmentAffix(stat) && extremeEquipment(s,o,equipment)) {
            double cap=nativeStatCap(stat);
            if(cap>0) {setNumber(r,cap);return r;}
            // Uncapped affix: fall through so the original mechanism supplies the value.
        }
    }
    return originalEquipStat(s,o,r,n,a);
}
static RV* hookBuiltin(void* s,void* o,RV* r,int n,int function,RV** a) {
    uintptr_t caller=CALLER_RVA;
    // This is the native generator's applicable-stat pool, immediately before its destruction.
    if(equipmentContext && caller==0x4930fa && n==1 && function==id(offsets::var_ds_list_destroy)) {
        int size=listSize(s,o,a[0]);
        for(int i=0;i<size;i++) {
            RV stat=listValue(s,o,a[0],i);
            if(stat.numeric() && stat.number()>=0 && stat.number()<128) {
                int value=static_cast<int>(stat.number());
                auto& pool=equipmentContext->pool;
                if(std::find(pool.begin(),pool.end(),value)==pool.end()) pool.push_back(value);
            }
        }
    }
    return originalBuiltin(s,o,r,n,function,a);
}
static RV* hookResource(void* s,void* o,RV* r,int n,RV** a) {
    ++resourceDepth;
    RV* result=originalResource(s,o,r,n,a);
    --resourceDepth;
    return result;
}
static RV* hookLegendary(void* s,void* o,RV* r,int n,RV** a) {
    if(!enabled(4) || n!=1 || !a || !a[0] || !a[0]->numeric() ||
       !std::isfinite(a[0]->number()) || a[0]->number()<0 ||
       a[0]->number()!=std::floor(a[0]->number())) return originalLegendary(s,o,r,n,a);
    RV* tally=global(offsets::var_LEGENDARY_TALLY);
    RV* noDrop=global(offsets::var_LEGENDARY_NODROP);
    if(!tally || !tally->handle() || !noDrop || !noDrop->handle()) return originalLegendary(s,o,r,n,a);
    // Keep story/ownership exclusions and the native -1 (already dropped) marker.
    RV blocked;RV* containsArgs[]={noDrop,a[0]};
    originalContains(s,o,&blocked,2,containsArgs);
    if(!blocked.numeric() || blocked.number()!=0) return originalLegendary(s,o,r,n,a);
    RV exists=call(s,o,offsets::var_ds_map_exists,{tally,a[0]});
    if(!exists.numeric()) return originalLegendary(s,o,r,n,a);
    double count=0;
    if(exists.number()!=0) {
        RV value=call(s,o,offsets::var_ds_map_find_value,{tally,a[0]});
        if(!value.numeric() || !std::isfinite(value.number()) || value.number()<0) return originalLegendary(s,o,r,n,a);
        count=value.number();
    }
    // Native first encounter only initializes the tally, and its chance is
    // (tally - 20) * 0.0003 * (1 + PLAYERSTATS[33]). Seed the warmup floor so
    // this very encounter enters the normal increment/roll/drop/-1 path.
    if(exists.number()==0 || count<20) {
        RV warmup=RV::number(20);
        call(s,o,offsets::var_ds_map_set,{tally,a[0],&warmup});
    }
    // Resource, creature and fishing events call this outside drop_itemdrops.
    ++legendaryDepth;
    RV* result=originalLegendary(s,o,r,n,a);
    --legendaryDepth;hit(4);
    return result;
}
static RV* hookDrop(void* s,void* o,RV* r,int n,RV** a) {
    if(!enabled(4) || !resourceDepth || n!=3 || !a[0]->numeric()) return originalDrop(s,o,r,n,a);
    DropContext context{{},0,dropContext};
    int size=std::min(listSize(s,o,a[1]),listSize(s,o,a[2]));
    for(int i=0;i<size;i++) {
        RV weight=listValue(s,o,a[2],i);
        RV item=listValue(s,o,a[1],i);
        if(weight.numeric() && weight.number()>0 && item.numeric() && item.number()>=0) context.indices.push_back(i);
    }
    if(context.indices.empty()) return originalDrop(s,o,r,n,a);
    RV amount=RV::number(std::max(a[0]->number(),static_cast<double>(context.indices.size())));
    RV* args[]={&amount,a[1],a[2]};
    dropContext=&context;
    RV* result=originalDrop(s,o,r,3,args);
    dropContext=context.previous;hit(4);
    return result;
}
static RV* hookWeighted(void* s,void* o,RV* r,int n,RV** a) {
    uintptr_t caller=CALLER_RVA;
    if(dropContext && caller>=offsets::drop_items && caller<offsets::drop_items+0x3000 && dropContext->cursor<dropContext->indices.size()) {
        setNumber(r,dropContext->indices[dropContext->cursor++]);return r;
    }
    return originalWeighted(s,o,r,n,a);
}
static RV* hookContains(void* s,void* o,RV* r,int n,RV** a) {
    if(recipeContext && n==2 && a[0]->handle() && a[1]->numeric()) {
        RV* noDrop=global(offsets::var_RECIPE_NODROP);
        int value=static_cast<int>(a[1]->number());
        if(noDrop && a[0]->sameHandle(*noDrop)) {
            if(std::find(recipeContext->picked.begin(),recipeContext->picked.end(),value)!=recipeContext->picked.end()) {setNumber(r,1);return r;}
            // Sawgrass Fingers is reserved by story loot, although it is also in Sawgrass's native drop pool.
            if(value==8) {setNumber(r,0);return r;}
        }
    }
    return originalContains(s,o,r,n,a);
}
static RV* hookRandomList(void* s,void* o,RV* r,int n,RV** a) {
    uintptr_t caller=CALLER_RVA;
    RV* result=originalRandomList(s,o,r,n,a);
    if(recipeContext && caller==0x2b0af1 && result && result->numeric()) recipeContext->chosen=static_cast<int>(result->number());
    return result;
}
static RV* hookRecipe(void* s,void* o,RV* r,int n,RV** a) {
    if(!enabled(2) && !(enabled(4)&&resourceDepth)) return originalRecipe(s,o,r,n,a);
    RV* cooldown=global(offsets::var_RECIPEDROP_COOLDOWN);
    RV saved;bool restore=cooldown && cooldown->numeric();if(restore) saved=*cooldown;
    RecipeContext context{{},-1,recipeContext};recipeContext=&context;
    ++recipeDepth;
    RV* result=r;
    int limit=256;
    for(int i=0;i<limit;i++) {
        if(restore) *cooldown=RV::number(0);
        context.chosen=-1;
        result=originalRecipe(s,o,r,n,a);
        if(context.chosen<0 || std::find(context.picked.begin(),context.picked.end(),context.chosen)!=context.picked.end()) break;
        context.picked.push_back(context.chosen);hit(2);
    }
    if(restore) *cooldown=saved;
    --recipeDepth;recipeContext=context.previous;
    return result;
}
static double hookRandom(double limit) {
    uintptr_t caller=CALLER_RVA;
    if(enabled(4) && legendaryDepth && caller==offsets::legendary_chance_return) return 0.0;
    if(recipeDepth && caller==0x2aec57) return 0.0;
    // The second roll REMOVES recipes below the roll threshold. A high roll preserves rare recipes.
    if(recipeDepth && caller==0x2b0913) return limit;
    // Harvest juice is a separate chance roll, outside the weighted resource table.
    if(enabled(4) && resourceDepth && (caller==0x2adbf8 || caller==0x3f655a)) return 0.0;
    return originalRandom(limit);
}
#include "diagnostics.inc"
static RV* hookGet(void* self,int variable) {
    uintptr_t caller=CALLER_RVA;
    RV* result=originalGet(self,variable);
    if(caller==0xebdab2 && enabled(5)) {
        RV* stations=global(offsets::var_STATION_DATA);
        for(int i=0;i<array_size(stations);i++) {RV* remaining=array_at(array_at(stations,i),1);if(remaining&&remaining->numeric()&&remaining->number()>0.000001)setNumber(remaining,0.000001);}
    }
    if(caller==0xebdab2 && shared && InterlockedExchange(&shared->testCommand,0)==1) diagnostics(self);
    if(caller==0xebdab2 && enabled(3) && result && result->numeric()) {
        static thread_local RV speed;
        speed=RV::number(result->number()*10.0);shared->lastSpeed=speed.number();hit(3);return &speed;
    }
    return result;
}

static bool install(uintptr_t address,void* detour,void** original) {
    if(MH_CreateHook(reinterpret_cast<void*>(base+address),detour,original)!=MH_OK) return false;
    InterlockedIncrement(&shared->hookCount);return true;
}
static DWORD WINAPI initialize(void*) {
    base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->FileHeader.TimeDateStamp!=0x68f7b82a || nt->OptionalHeader.SizeOfImage!=0x28a7000) return 1;
    wchar_t name[96];swprintf_s(name,L"Local\\CrashlandsTrainer_v2_%lu",GetCurrentProcessId());
    mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name);
    if(!mapping) return 2;
    shared=static_cast<Shared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,4096));
    if(!shared || shared->magic!=0x43544e52 || shared->version!=3) return 3;
    shared->status=0;
    bool ok=MH_Initialize()==MH_OK;
#define HOOK(name,fn,original) ok=ok&&install(offsets::name,reinterpret_cast<void*>(fn),reinterpret_cast<void**>(&original))
    HOOK(gain_item,hookGain,originalGain);HOOK(craft_recipe,hookCraft,originalCraft);
    HOOK(generate_randomized_equipment,hookGenerate,originalGenerate);HOOK(call_builtin,hookBuiltin,originalBuiltin);
    HOOK(drop_itemdrops,hookResource,originalResource);HOOK(drop_items,hookDrop,originalDrop);
    HOOK(get_list_index_by_weight,hookWeighted,originalWeighted);HOOK(check_for_recipe_drop,hookRecipe,originalRecipe);
    HOOK(ds_list_contains,hookContains,originalContains);HOOK(ds_list_get_random,hookRandomList,originalRandomList);
    HOOK(random_real,hookRandom,originalRandom);HOOK(get_instance_var,hookGet,originalGet);
    HOOK(take_item_from_station,hookTake,originalTake);HOOK(get_recipe_time,hookRecipeTime,originalRecipeTime);
    HOOK(queue_recipe,hookQueue,originalQueue);HOOK(get_eq_stat_amt,hookEquipStat,originalEquipStat);
    HOOK(swing_item,hookSwing,originalSwing);
    HOOK(check_legendary_component_drop,hookLegendary,originalLegendary);
#undef HOOK
    if(!ok || MH_EnableHook(MH_ALL_HOOKS)!=MH_OK) {
        MH_DisableHook(MH_ALL_HOOKS);shared->flags=0;shared->status=-1;
        strcpy_s(shared->detail,"Hook initialization failed; no features are active.");return 4;
    }
    shared->status=1;strcpy_s(shared->detail,"Trainer 1.3.0; Steam build 20483884: 18 native hooks active.");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        HANDLE t=CreateThread(nullptr,0,initialize,nullptr,0,nullptr);if(t) CloseHandle(t);
    }
    return TRUE;
}
