#define TRAINER_TEST
#include "../src/native/trainer.cpp"
#include <map>
#include <memory>
#include <iostream>
#include <limits>

static int assertions=0;
#define CHECK(x) do { ++assertions; if(!(x)){std::cerr<<"FAIL line "<<__LINE__<<": "<<#x<<"\n";return 1;} }while(0)
struct ArrayStorage { uint64_t a=0;RV* elements=nullptr;void* owner=nullptr;int references=1;int b=0;int c=0;int length=0; };
static_assert(offsetof(ArrayStorage,length)==0x24);
struct FixtureArray { std::vector<RV> values;ArrayStorage storage;RV value;FixtureArray(int n):values(n) {storage.elements=values.data();storage.length=n;value.kind=2;value.value.ptr=&storage;} };
static std::vector<std::unique_ptr<FixtureArray>> arrays;
static RV makeArray(int size) { auto a=std::make_unique<FixtureArray>(size);RV value=a->value;arrays.push_back(std::move(a));return value; }
static std::map<int,RV> globals;
static std::map<int,std::vector<RV>> lists;
static std::map<int,std::map<int,RV>> maps;
static RV makeHandle(int index) {RV r;r.kind=15;r.value.integer=0x0100000000000000LL|index;return r;}
static int handleIndex(const RV& r) {return (r.kind&0xffffff)==15?static_cast<int>(r.value.integer):static_cast<int>(r.number());}
struct Object {void** vt;};
static RV* mockGlobal(void*,int i) {return &globals[i];}
static RV* mockBuiltin(void*,void*,RV* result,int n,int fn,RV** args) {
    if(fn==id(offsets::var_ds_map_exists) || fn==id(offsets::var_ds_map_find_value) || fn==id(offsets::var_ds_map_set)) {
        auto& map=maps[handleIndex(*args[0])];int key=static_cast<int>(args[1]->number());
        auto found=map.find(key);
        if(fn==id(offsets::var_ds_map_exists)) *result=RV::number(found!=map.end()?1:0);
        else if(fn==id(offsets::var_ds_map_find_value)) *result=found==map.end()?RV():found->second;
        else {map[key]=*args[2];*result=RV::number(0);}
        return result;
    }
    auto& list=lists[args&&n?handleIndex(*args[0]):0];
    if(fn==id(offsets::var_ds_list_size)) *result=RV::number(static_cast<double>(list.size()));
    else if(fn==id(offsets::var_ds_list_find_value)) {int i=static_cast<int>(args[1]->number());*result=i>=0&&i<static_cast<int>(list.size())?list[i]:RV();}
    else if(fn==id(offsets::var_ds_list_find_index)) {int i=0;while(i<static_cast<int>(list.size())&&list[i].number()!=args[1]->number())++i;*result=RV::number(i==list.size()?-1:i);}
    else if(fn==id(offsets::var_ds_list_add)) {for(int i=1;i<n;i++)list.push_back(*args[i]);*result=RV::number(0);}
    else if(fn==id(offsets::var_ds_list_clear)) {list.clear();*result=RV::number(0);}
    else if(fn==id(offsets::var_ds_list_destroy)) {list.clear();*result=RV::number(0);}
    return result;
}
static double receivedAmount,receivedQuality;static int gainCalls,extraArgument,generated,renamed;
static RV* mockGain(void*,void*,RV* r,int n,RV** a) {gainCalls++;receivedAmount=a[1]->number();extraArgument=n==4?static_cast<int>(a[3]->number()):-1;*r=RV::number(1);return r;}
static RV* mockRename(void*,void*,RV* r,int,RV**) {renamed++;*r=RV::number(0);return r;}
static RV* mockGenerate(void* s,void* o,RV* r,int,RV** a) {
    generated++;receivedQuality=a[1]->number();RV pool=RV::number(200),dummy;RV* args[]={&pool};
    uintptr_t previous=testCaller;testCaller=0x4930fa;
    hookBuiltin(s,o,&dummy,1,id(offsets::var_ds_list_destroy),args);testCaller=previous;
    *r=RV::number(0);return r;
}
static RV* mockCraft(void* s,void* o,RV* r,int n,RV** a) {return hookGenerate(s,o,r,n,a);}
static RV* mockTakeGain(void* s,void* o,RV* r,int n,RV** a) {return hookGain(s,o,r,n,a);}
static RV* mockRecipeTime(void*,void*,RV* r,int,RV**) {*r=RV::number(30);return r;}
static RV* mockQueue(void*,void*,RV* r,int,RV**) {*r=RV::number(1);return r;}
static RV* mockEquipStat(void*,void*,RV* r,int,RV**) {*r=RV::number(0.2);return r;}
static std::vector<int> draws;
static RV* mockWeighted(void*,void*,RV* r,int,RV**) {*r=RV::number(0);return r;}
static RV* mockDrop(void* s,void* o,RV* r,int,RV** a) {
    uintptr_t previous=testCaller;testCaller=offsets::drop_items+100;
    for(int i=0;i<static_cast<int>(a[0]->number());i++){RV pick;RV* args[]={a[2]};hookWeighted(s,o,&pick,1,args);draws.push_back(static_cast<int>(pick.number()));}
    testCaller=previous;*r=RV::number(0);return r;
}
static RV* mockContains(void*,void*,RV* r,int,RV** a) {bool found=false;for(auto v:lists[handleIndex(*a[0])])if(v.number()==a[1]->number())found=true;*r=RV::number(found?1:0);return r;}
static int recipeCalls,chosenRecipe;static std::vector<int> recipes;
static RV* mockRandomList(void*,void*,RV* r,int,RV**) {*r=chosenRecipe>=0?RV::number(chosenRecipe):RV();return r;}
static RV* mockRecipe(void* s,void* o,RV* r,int,RV**) {
    recipeCalls++;chosenRecipe=-1;
    if(global(offsets::var_RECIPEDROP_COOLDOWN)->number()>0) return r;
    for(int i:{3,4,5}) {RV value=RV::number(i),contains;RV* a[]={global(offsets::var_RECIPE_NODROP),&value};hookContains(s,o,&contains,2,a);if(contains.number()==0){chosenRecipe=i;break;}}
    if(chosenRecipe>=0) {
        uintptr_t previous=testCaller;testCaller=0x2b0af1;
        hookRandomList(s,o,r,0,nullptr);testCaller=previous;recipes.push_back(chosenRecipe);
        *global(offsets::var_RECIPEDROP_COOLDOWN)=RV::number(120);
    }
    return r;
}
static RV nativeSpeed=RV::number(150);
static RV* mockGet(void*,int) {return &nativeSpeed;}
static double mockRandom(double limit){return limit*0.5;}
static std::vector<int> legendaryDrops;
static int legendaryCalls;
static double legendaryRollCount;
// Mirrors check_legendary_component_drop: exclusion -> initialize-only on a
// missing key -> -1 guard -> increment -> warmup/chance -> mark -1 and drop.
static RV* mockLegendary(void* s,void* o,RV* r,int n,RV** a) {
    ++legendaryCalls;*r=RV::number(0);
    if(n!=1 || !a || !a[0] || !a[0]->numeric() || !std::isfinite(a[0]->number())) return r;
    RV* tally=global(offsets::var_LEGENDARY_TALLY);
    RV* noDrop=global(offsets::var_LEGENDARY_NODROP);
    if(!tally->handle() || !noDrop->handle()) return r;
    RV blocked;RV* check[]={noDrop,a[0]};hookContains(s,o,&blocked,2,check);
    if(blocked.number()!=0) return r;
    RV exists=call(s,o,offsets::var_ds_map_exists,{tally,a[0]});
    if(exists.number()==0) {RV one=RV::number(1);call(s,o,offsets::var_ds_map_set,{tally,a[0],&one});return r;}
    RV count=call(s,o,offsets::var_ds_map_find_value,{tally,a[0]});
    if(!count.numeric() || count.number()==-1) return r;
    count=RV::number(count.number()+1);call(s,o,offsets::var_ds_map_set,{tally,a[0],&count});
    legendaryRollCount=count.number();
    double chance=(count.number()-20)*0.0003;
    uintptr_t previous=testCaller;testCaller=offsets::legendary_chance_return;
    double roll=hookRandom(1);testCaller=previous;
    if(roll-chance<=0.00001) {
        RV done=RV::number(-1);call(s,o,offsets::var_ds_map_set,{tally,a[0],&done});
        legendaryDrops.push_back(static_cast<int>(a[0]->number()));
    }
    return r;
}
static std::map<int,RV> instanceValues;
static double swingDurationSeen,swingMaxcoolSeen;
static RV* mockInstanceGet(void*,int i) {return &instanceValues[i];}
static RV* mockInstanceSet(void*,int i) {return &instanceValues[i];}
static RV* mockSwing(void*,void*,RV* r,int,RV**) {
    swingDurationSeen=instanceValues[id(offsets::var_gatherswing_duration)].number();
    swingMaxcoolSeen=instanceValues[id(offsets::var_gatherswing_maxcool)].number();
    *r=RV::number(1);return r;
}
struct InstanceObject {void** vt;};
// Mirrors the shipped game's STATINFO index 4; 0 means the stat has no native hard cap.
static double nativeCapForTest(int stat) {
    switch(stat) {
    case 1:case 20:case 21:case 22:case 23:return 0.9;
    case 5:case 8:case 9:case 10:case 12:case 14:case 15:case 16:case 17:case 18:case 19:
    case 30:case 31:case 32:return 1.0;
    default:return 0;
    }
}
static void reset(Shared& state,int flags) {state={};state.flags=flags;state.clientTick=GetTickCount();}
int main() {
    base=reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr,0x28a7000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    CHECK(base!=0);Shared state{};shared=&state;
    void* vtable[]={nullptr,reinterpret_cast<void*>(mockGlobal)};Object object{vtable};*reinterpret_cast<void**>(base+offsets::globals)=&object;
    uintptr_t variables[]={offsets::var_ITEMINFO,offsets::var_EQUIPMENT,offsets::var_RECIPEDROP_COOLDOWN,offsets::var_RECIPE_NODROP,offsets::var_ds_list_size,offsets::var_ds_list_find_value,offsets::var_ds_list_find_index,offsets::var_ds_list_add,offsets::var_ds_list_destroy,offsets::var_ds_list_clear,offsets::var_STATION_DATA,offsets::var_STATINFO,offsets::var_gatherswing_duration,offsets::var_gatherswing_maxcool,offsets::var_ds_map_exists,offsets::var_ds_map_find_value,offsets::var_ds_map_set,offsets::var_LEGENDARY_TALLY,offsets::var_LEGENDARY_NODROP};
    int index=1;for(auto address:variables)*reinterpret_cast<int*>(base+address)=index++;
    originalBuiltin=mockBuiltin;originalGain=mockGain;originalCraft=mockCraft;originalGenerate=mockGenerate;
    originalDrop=mockDrop;originalWeighted=mockWeighted;originalContains=mockContains;originalRandomList=mockRandomList;originalRecipe=mockRecipe;originalGet=mockGet;originalRandom=mockRandom;
    originalTake=mockTakeGain;originalRecipeTime=mockRecipeTime;originalQueue=mockQueue;originalEquipStat=mockEquipStat;originalSwing=mockSwing;
    originalLegendary=mockLegendary;
    uint8_t jump[]={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};void* target=reinterpret_cast<void*>(mockRename);memcpy(jump+2,&target,8);memcpy(reinterpret_cast<void*>(base+offsets::set_equipment_name),jump,sizeof(jump));
    globals[id(offsets::var_ITEMINFO)]=makeArray(50);
    for(int item=0;item<50;item++){*array_at(global(offsets::var_ITEMINFO),item)=makeArray(10);*array_at(array_at(global(offsets::var_ITEMINFO),item),3)=RV::number(item);*array_at(array_at(global(offsets::var_ITEMINFO),item),4)=RV::number(-1);}
    globals[id(offsets::var_STATINFO)]=makeArray(36);
    for(int stat=0;stat<36;stat++){*array_at(global(offsets::var_STATINFO),stat)=makeArray(5);*array_at(array_at(global(offsets::var_STATINFO),stat),4)=RV::number(nativeCapForTest(stat));}
    RV result,item=RV::number(26),amount=RV::number(3),extra=RV::number(77);RV* gain[]={&item,&amount,&extra,&extra};
    reset(state,1);hookGain(nullptr,nullptr,&result,4,gain);
    CHECK(receivedAmount==30000);CHECK(amount.number()==3);CHECK(extraArgument==77);CHECK(state.hits[0]==1);CHECK(gainCalls==1);
    for(int type:{10,11,12,13,14,16,17,24,26,35,36}){item=RV::number(type);hookGain(nullptr,nullptr,&result,2,gain);CHECK(receivedAmount==30000);}
    for(int type:{0,1,2,3,4,5,6,7,15,20,21,27,31,38}){item=RV::number(type);hookGain(nullptr,nullptr,&result,2,gain);CHECK(receivedAmount==3);}
    item=RV::number(26);for(double value:{0.0,-2.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {amount=RV::number(value);LONG64 before=state.hits[0];hookGain(nullptr,nullptr,&result,2,gain);CHECK(state.hits[0]==before);}
    amount=RV::number(3);state.flags=0;hookGain(nullptr,nullptr,&result,2,gain);CHECK(receivedAmount==3);
    reset(state,1);state.clientTick=GetTickCount()-3000;hookGain(nullptr,nullptr,&result,2,gain);CHECK(receivedAmount==3);
    *array_at(array_at(global(offsets::var_ITEMINFO),10),3)=RV::number(21);
    item=RV::number(10);reset(state,1);hookTake(nullptr,nullptr,&result,2,gain);CHECK(receivedAmount==30000);CHECK(takeDepth==0);CHECK(amount.number()==3);
    hookGain(nullptr,nullptr,&result,2,gain);CHECK(receivedAmount==3);item=RV::number(12);hookTake(nullptr,nullptr,&result,2,gain);CHECK(receivedAmount==30000);
    // Native stack limits apply to both material pickups and both crafting paths.
    // A category alone cannot establish that a given item can be stacked.
    reset(state,1);
    for(int type:{0,1,2,3,4,5,6,7,10,11,12,13,14,15,16,17,19,20,21,24,26,27,31,33,34,35,36,37,38}) {
        item=RV::number(type);RV* limit=array_at(array_at(global(offsets::var_ITEMINFO),type),4);
        for(double value:{0.0,1.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
            *limit=RV::number(value);LONG64 before=state.hits[0];
            hookGain(nullptr,nullptr,&result,4,gain);CHECK(receivedAmount==3);CHECK(extraArgument==77);
            hookTake(nullptr,nullptr,&result,4,gain);CHECK(receivedAmount==3);CHECK(takeDepth==0);
            craftDepth=1;hookGain(nullptr,nullptr,&result,4,gain);craftDepth=0;CHECK(receivedAmount==3);
            CHECK(state.hits[0]==before);CHECK(amount.number()==3);
        }
        *limit=RV();hookTake(nullptr,nullptr,&result,2,gain);CHECK(receivedAmount==3);
        *limit=RV::number(-1);
    }
    for(int type:{6,15,19,20,21,31,33,34,37}) {
        item=RV::number(type);RV* limit=array_at(array_at(global(offsets::var_ITEMINFO),type),4);
        for(double value:{-1.0,2.0,99.0}) {
            *limit=RV::number(value);hookTake(nullptr,nullptr,&result,2,gain);CHECK(receivedAmount==30000);
            craftDepth=1;hookGain(nullptr,nullptr,&result,2,gain);craftDepth=0;CHECK(receivedAmount==30000);
        }
        *limit=RV::number(-1);
    }
    item=RV::number(50);hookTake(nullptr,nullptr,&result,2,gain);CHECK(receivedAmount==3);
    item=RV::number(-1);hookTake(nullptr,nullptr,&result,2,gain);CHECK(receivedAmount==3);
    item=RV::number(std::numeric_limits<double>::infinity());hookTake(nullptr,nullptr,&result,2,gain);CHECK(receivedAmount==3);
    item=RV::number(12);
    globals[id(offsets::var_EQUIPMENT)]=makeArray(1);*array_at(global(offsets::var_EQUIPMENT),0)=makeArray(4);
    *array_at(array_at(global(offsets::var_EQUIPMENT),0),3)=makeHandle(100);lists[100]={RV::number(1),RV::number(1)};
    lists[200]={RV::number(1),RV::number(2),RV::number(2),RV::number(3)};
    RV quality=RV::number(-1);RV* equipArgs[]={&item,&quality};reset(state,2);hookCraft(nullptr,nullptr,&result,2,equipArgs);
    CHECK(receivedQuality==4);CHECK(quality.number()==-1);CHECK(lists[100].size()==25);int next=0;for(int i=0;i<29;i++)if(equipmentAffix(i))CHECK(lists[100][next++].number()==i);CHECK(renamed==1);CHECK(state.lastAffixes==25);CHECK(craftDepth==0);CHECK(equipmentContext==nullptr);
    quality=RV::number(5);hookCraft(nullptr,nullptr,&result,2,equipArgs);CHECK(receivedQuality==5);
    quality=RV::number(-1);hookGenerate(nullptr,nullptr,&result,2,equipArgs);CHECK(receivedQuality==-1); // Non-crafting generation passes through.
    lists[200]={RV::number(2),RV::number(3)};testCaller=0xba43e0;hookGenerate(nullptr,nullptr,&result,2,equipArgs);CHECK(receivedQuality==4);CHECK(state.hits[1]==3);testCaller=0;
    // Collecting a completed station craft takes the takeDepth path and must receive the same gear.
    lists[100]={RV::number(1)};quality=RV::number(-1);takeDepth=1;hookGenerate(nullptr,nullptr,&result,2,equipArgs);takeDepth=0;
    CHECK(receivedQuality==4);CHECK(lists[100].size()==25);CHECK(craftDepth==0);
    RV equipId=RV::number(0),stat=RV::number(0);RV* statArgs[]={&equipId,&stat};
    for(int i=0;i<36;i++){
        stat=RV::number(i);hookEquipStat(nullptr,nullptr,&result,2,statArgs);
        double cap=nativeCapForTest(i),expected=equipmentAffix(i)&&cap>0?cap:0.2;
        CHECK(nativeStatCap(i)==cap);CHECK(std::isfinite(result.number())&&result.number()>0);CHECK(result.number()==expected);
    }
    // Capped affixes use the native STATINFO cap; uncapped affixes keep the original formula.
    for(int i:{1,5,8,9,10,12,14,15,16,17,18,19,20,21,22,23}){stat=RV::number(i);hookEquipStat(nullptr,nullptr,&result,2,statArgs);CHECK(result.number()==nativeCapForTest(i));}
    for(int i:{0,2,7,11,13,25,26,27,28}){stat=RV::number(i);hookEquipStat(nullptr,nullptr,&result,2,statArgs);CHECK(result.number()==0.2);}
    stat=RV::number(31);hookEquipStat(nullptr,nullptr,&result,2,statArgs);CHECK(result.number()==0.2);
    *array_at(array_at(global(offsets::var_STATINFO),5),4)=RV::number(0.77);stat=RV::number(5);hookEquipStat(nullptr,nullptr,&result,2,statArgs);CHECK(result.number()==0.77);
    *array_at(array_at(global(offsets::var_STATINFO),5),4)=RV::number(0);hookEquipStat(nullptr,nullptr,&result,2,statArgs);CHECK(result.number()==0.2);
    *array_at(array_at(global(offsets::var_STATINFO),5),4)=RV::number(1);
    stat=RV::number(5);state.flags=0;hookEquipStat(nullptr,nullptr,&result,2,statArgs);CHECK(result.number()==0.2);
    state.flags=2;hookEquipStat(nullptr,nullptr,&result,2,statArgs);CHECK(result.number()==1);
    lists[100].pop_back();hookEquipStat(nullptr,nullptr,&result,2,statArgs);CHECK(result.number()==0.2);
    lists[100]={RV::number(3),RV::number(4),RV::number(6)};craftDepth=1;hookGenerate(nullptr,nullptr,&result,2,equipArgs);craftDepth=0;
    CHECK(lists[100].size()==28);CHECK(extremeEquipment(nullptr,nullptr,0));
    // Weapon base stats have no native cap, so they also keep their original values.
    for(int i:{3,4,6}){stat=RV::number(i);hookEquipStat(nullptr,nullptr,&result,2,statArgs);CHECK(result.number()==0.2);}
    // Instant gathering temporarily collapses the native gather swing timing, then restores it.
    void* instanceVtable[]={nullptr,reinterpret_cast<void*>(mockInstanceGet),reinterpret_cast<void*>(mockInstanceSet)};
    InstanceObject instance{instanceVtable};
    instanceValues[id(offsets::var_gatherswing_duration)]=RV::number(0.15);
    instanceValues[id(offsets::var_gatherswing_maxcool)]=RV::number(0.4);
    reset(state,0);swingDurationSeen=swingMaxcoolSeen=0;hookSwing(&instance,nullptr,&result,0,nullptr);
    CHECK(swingDurationSeen==0.15);CHECK(swingMaxcoolSeen==0.4);CHECK(state.hits[6]==0);
    reset(state,64);swingDurationSeen=swingMaxcoolSeen=0;hookSwing(&instance,nullptr,&result,0,nullptr);
    CHECK(swingDurationSeen==0.000001);CHECK(swingMaxcoolSeen==0.000001);
    CHECK(instanceValues[id(offsets::var_gatherswing_duration)].number()==0.15);
    CHECK(instanceValues[id(offsets::var_gatherswing_maxcool)].number()==0.4);CHECK(state.hits[6]==1);
    reset(state,0);swingDurationSeen=swingMaxcoolSeen=0;hookSwing(&instance,nullptr,&result,0,nullptr);
    CHECK(swingDurationSeen==0.15);CHECK(swingMaxcoolSeen==0.4);CHECK(state.hits[6]==0);
    globals[id(offsets::var_STATION_DATA)]=makeArray(1);*array_at(global(offsets::var_STATION_DATA),0)=makeArray(2);RV* remaining=array_at(array_at(global(offsets::var_STATION_DATA),0),1);*remaining=RV::number(40);
    reset(state,32);hookRecipeTime(nullptr,nullptr,&result,0,nullptr);CHECK(result.number()>0&&result.number()<0.00001);hookQueue(nullptr,nullptr,&result,0,nullptr);CHECK(remaining->number()<0.00001);
    reset(state,0);hookRecipeTime(nullptr,nullptr,&result,0,nullptr);CHECK(result.number()==30);
    lists[300]={RV::number(10),RV::number(11),RV::number(12),RV::number(13)};
    lists[301]={RV::number(1),RV::number(0),RV::number(0.1),RV::number(2)};
    RV count=RV::number(1),items=RV::number(300),weights=RV::number(301);RV* dropArgs[]={&count,&items,&weights};
    reset(state,16);resourceDepth=1;draws.clear();hookDrop(nullptr,nullptr,&result,3,dropArgs);
    CHECK(draws==std::vector<int>({0,2,3}));CHECK(count.number()==1);CHECK(dropContext==nullptr);CHECK(state.hits[4]==1);
    count=RV::number(5);draws.clear();hookDrop(nullptr,nullptr,&result,3,dropArgs);CHECK(draws==std::vector<int>({0,2,3,0,0}));
    resourceDepth=0;count=RV::number(1);draws.clear();hookDrop(nullptr,nullptr,&result,3,dropArgs);CHECK(draws==std::vector<int>({0}));
    globals[id(offsets::var_RECIPEDROP_COOLDOWN)]=RV::number(90);globals[id(offsets::var_RECIPE_NODROP)]=makeHandle(400);lists[400]={RV::number(4)};
    recipeCalls=0;recipes.clear();reset(state,4);hookRecipe(nullptr,nullptr,&result,0,nullptr);CHECK(recipes==std::vector<int>({3,5}));CHECK(global(offsets::var_RECIPEDROP_COOLDOWN)->number()==90);CHECK(state.hits[2]==2);
    recipeCalls=0;recipes.clear();reset(state,16);resourceDepth=1;hookRecipe(nullptr,nullptr,&result,0,nullptr);resourceDepth=0;
    CHECK(recipes==std::vector<int>({3,5}));CHECK(recipeCalls==3);CHECK(lists[400].size()==1);CHECK(global(offsets::var_RECIPEDROP_COOLDOWN)->number()==90);CHECK(recipeContext==nullptr);CHECK(recipeDepth==0);
    RecipeContext recipeFixture{{},-1,nullptr};recipeContext=&recipeFixture;RV reserved=RV::number(8);RV* reservedArgs[]={global(offsets::var_RECIPE_NODROP),&reserved};lists[400].push_back(reserved);hookContains(nullptr,nullptr,&result,2,reservedArgs);CHECK(result.number()==0);recipeFixture.picked.push_back(8);hookContains(nullptr,nullptr,&result,2,reservedArgs);CHECK(result.number()==1);recipeContext=nullptr;
    reset(state,8);testCaller=0xebdab2;for(int i=0;i<10;i++)CHECK(hookGet(nullptr,0)->number()==1500);
    CHECK(nativeSpeed.number()==150);testCaller=0xebdab3;CHECK(hookGet(nullptr,0)->number()==150);testCaller=0xebdab2;state.flags=0;CHECK(hookGet(nullptr,0)->number()==150);
    reset(state,4);recipeDepth=1;testCaller=0x2aec57;CHECK(hookRandom(1)==0);testCaller=0x2b0913;CHECK(hookRandom(1)==1);CHECK(!(hookRandom(1)-0.999 < -0.00001));testCaller=0x2b0914;CHECK(hookRandom(1)==0.5);recipeDepth=0;
    reset(state,16);resourceDepth=1;testCaller=0x2adbf8;CHECK(hookRandom(1)==0);testCaller=0x3f655a;CHECK(hookRandom(1)==0);resourceDepth=0;CHECK(hookRandom(1)==0.5);
    globals[id(offsets::var_LEGENDARY_TALLY)]=makeHandle(500);
    globals[id(offsets::var_LEGENDARY_NODROP)]=makeHandle(501);
    RV hidden=RV::number(109);RV* hiddenArgs[]={&hidden};
    // Without the feature, the first encounter only records a tally, and the
    // next encounter still cannot pass the native twenty-encounter warmup.
    reset(state,0);maps[500].clear();legendaryDrops.clear();legendaryCalls=0;
    hookLegendary(nullptr,nullptr,&result,1,hiddenArgs);CHECK(maps[500][109].number()==1);CHECK(legendaryDrops.empty());
    hookLegendary(nullptr,nullptr,&result,1,hiddenArgs);CHECK(maps[500][109].number()==2);CHECK(legendaryDrops.empty());CHECK(legendaryCalls==2);
    // All nine unique IDs from the EXE's eleven legendary call sites. They
    // execute with resourceDepth == 0 (including creature and fishing events).
    for(int hiddenId:{109,830,831,832,833,836,838,840,843}) {
        for(int prior:{-2,0,1,19,20,21,100}) {
            maps[500].clear();if(prior!=-2)maps[500][hiddenId]=RV::number(prior);
            hidden=RV::number(hiddenId);legendaryDrops.clear();legendaryCalls=0;legendaryRollCount=-1;
            reset(state,16);CHECK(resourceDepth==0);
            hookLegendary(nullptr,nullptr,&result,1,hiddenArgs);
            CHECK(legendaryDrops==std::vector<int>({hiddenId}));CHECK(legendaryCalls==1);
            CHECK(maps[500][hiddenId].number()==-1);CHECK(legendaryRollCount>=21);CHECK(legendaryDepth==0);CHECK(state.hits[4]==1);
            CHECK(hidden.number()==hiddenId);CHECK(lists[501].empty());
            // Native completion prevents duplicates on the very next source.
            hookLegendary(nullptr,nullptr,&result,1,hiddenArgs);
            CHECK(legendaryDrops.size()==1);CHECK(maps[500][hiddenId].number()==-1);CHECK(legendaryCalls==2);CHECK(state.hits[4]==1);
        }
    }
    hidden=RV::number(109);maps[500].clear();legendaryDrops.clear();lists[501]={hidden};reset(state,16);
    hookLegendary(nullptr,nullptr,&result,1,hiddenArgs);CHECK(maps[500].empty());CHECK(legendaryDrops.empty());CHECK(state.hits[4]==0);
    maps[500][109]=RV::number(5);hookLegendary(nullptr,nullptr,&result,1,hiddenArgs);CHECK(maps[500][109].number()==5);CHECK(legendaryDrops.empty());lists[501].clear();
    // Other feature flags and an expired heartbeat do not force hidden drops.
    for(int flags:{0,1,2,4,8,32,64,111}) {
        reset(state,flags);maps[500].clear();legendaryDrops.clear();hookLegendary(nullptr,nullptr,&result,1,hiddenArgs);
        CHECK(maps[500][109].number()==1);CHECK(legendaryDrops.empty());CHECK(state.hits[4]==0);
    }
    reset(state,16);state.clientTick=GetTickCount()-3000;maps[500].clear();hookLegendary(nullptr,nullptr,&result,1,hiddenArgs);
    CHECK(maps[500][109].number()==1);CHECK(legendaryDrops.empty());CHECK(legendaryDepth==0);
    reset(state,16);maps[500].clear();hookLegendary(nullptr,nullptr,&result,0,nullptr);CHECK(maps[500].empty());CHECK(legendaryDepth==0);
    // Only the verified legendary roll is forced; unrelated RNG stays native.
    legendaryDepth=1;testCaller=offsets::legendary_chance_return;CHECK(hookRandom(1)==0);
    testCaller=offsets::legendary_chance_return+1;CHECK(hookRandom(1)==0.5);
    testCaller=offsets::legendary_chance_return;legendaryDepth=0;CHECK(hookRandom(1)==0.5);
    legendaryDepth=1;state.flags=0;CHECK(hookRandom(1)==0.5);legendaryDepth=0;testCaller=0;
    std::cout<<"PASS: "<<assertions<<" native integration assertions\n";
    return 0;
}
