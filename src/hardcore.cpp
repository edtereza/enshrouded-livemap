// Guardian hardcore's native backend. Engine contracts derived from EMF's MIT
// research; no EMF runtime/client dependency. See dependencies/emf/LICENSE.txt.
#include "hardcore.h"
#include "hardcore_native.h"
#include "log.h"
#include <windows.h>
#include <safetyhook.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
namespace livemap::hardcore {
namespace {
using namespace native;
template<class T>T rd(const void* p,size_t off=0){T v;std::memcpy(&v,static_cast<const uint8_t*>(p)+off,sizeof v);return v;}
template<class T>void wr(void* p,size_t off,T v){std::memcpy(static_cast<uint8_t*>(p)+off,&v,sizeof v);}
using GetComponent=void(*)(uint64_t*,uint8_t*,uint32_t,uint32_t);
using ReadOther=void*(*)(void**,void*,uint64_t,uint32_t);
using Register=uint64_t(*)(void*,const Program*,char);
using Begin=void(*)(void*,void*,uint32_t);
using Queue=uint8_t*(*)(void*,void*,uint8_t,uint32_t,uint32_t);
using FindAttr=void*(*)(void*,uint32_t,const void*);
using AddAttr=void(*)(void*,const void*,int32_t,uint32_t);
using Required=uint32_t(*)(uint32_t,const void*);
using Destroy=uint64_t(*)(void*,uint32_t,char,uint64_t);
struct Functions {GetComponent get{};ReadOther read{};Register reg{};Begin begin{};Queue queue{};FindAttr find{};AddAttr add{};Required required{};Destroy destroy{};
    void*(*ctor)(uint8_t*){};void(*init)(uint8_t*,const uint8_t*){};
    void*(*take)(void*,uint8_t*,uint32_t,uint32_t,uint64_t,uint32_t){};
    void(*commit)(uint8_t*){};void(*rollback)(uint8_t*){};void(*free)(uint8_t*){};void(*wait)(uint8_t*){};
} f;
struct Guardian {bool(*enabled)(){};void(*backend)(uint32_t){};uint64_t(*death)(uint32_t,uint64_t){};uint64_t(*poll)(uint32_t,uint64_t){};bool(*complete)(uint32_t,uint64_t,uint64_t){};} api;
std::mutex api_mutex;
Guardian guardian(){std::lock_guard lock(api_mutex);if(!api.enabled){auto mod=GetModuleHandleW(L"guardian.dll");if(!mod)return {};Guardian next;
#define LOAD(field,name) next.field=reinterpret_cast<decltype(next.field)>(GetProcAddress(mod,name))
    LOAD(enabled,"GuardianHardcoreEnabled");LOAD(backend,"GuardianHardcoreBackend");LOAD(death,"GuardianHardcoreDeath");LOAD(poll,"GuardianHardcorePoll");LOAD(complete,"GuardianHardcoreComplete");
#undef LOAD
    if(next.enabled&&next.backend&&next.death&&next.poll&&next.complete)api=next;
}return api;}
SafetyHookInline sim_hook,register_hook;
std::atomic<uintptr_t> server_ptr{};
std::atomic<bool> faulted{},registered{};
struct Task {uint32_t handle{},entity{},start{};uint64_t account{},token{},began{};uint8_t before_count{};int phase{};Transform target{};bool wait_dead{};};
std::map<uint32_t,Task> tasks;
std::mutex tasks_mutex;
Program descriptor{};
constexpr Name sreads[]={{"BalancingRegistry",17},{"g38_items::ItemRegistry",23},{"impact::AttributeRegistry",25}};
constexpr Name swrites[]={{"KnowledgeTriggerQueue",21}};
constexpr Name oreads[]={{"Actor",5},{"CurrentTransform",16},{"Health",6},{"LastTransform",13},{"PlayerSpawnPoint",16},{"Teleport",8},{"TeleportCount",13}};
constexpr Name owrites[]={{"Fuel",4},{"Inventory",9},{"InventoryEmptyState",19},{"InventorySetup",14},{"ItemState",9},{"OwnerRelationship",17},{"SlotSelection",13}};
constexpr Name addtypes[]={{"Teleport",8}};
constexpr Name events[]={{"InventoryFull",13},{"PickupEvent",11}};
constexpr size_t other_base=1+std::size(sreads)+std::size(swrites),write_base=other_base+std::size(oreads),event_base=write_base+std::size(owrites);
struct Component {void* ptr;uint64_t size;};
Component component(uint8_t* world,uint32_t entity,const char* name,size_t size){uint64_t out[2]{};std::string qualified="keen::ecs::";qualified+=name;f.get(out,world,hash(qualified.c_str()),entity);if(out[0]&&out[1]!=size)throw std::runtime_error(std::string("component layout changed: ")+name);return {reinterpret_cast<void*>(out[0]),out[1]};}
void* need(uint8_t* world,uint32_t entity,const char* name,size_t size){auto c=component(world,entity,name,size);if(!c.ptr)throw std::runtime_error(std::string("missing component: ")+name);return c.ptr;}
void* singleton(uint8_t* world,const char* name){auto entries=rd<uint8_t*>(world,0x7462d0);auto n=rd<uint64_t>(world,0x7462d8);if(!entries||n>4096)throw std::runtime_error("singleton table invalid");for(uint64_t i=0;i<n;++i){auto p=entries+i*0xe8;if(rd<uint32_t>(p,0xa0)==hash(name))return rd<void*>(p,0xa8);}return nullptr;}
bool session(uint8_t* world,Task& t){auto server=reinterpret_cast<uint8_t*>(server_ptr.load());if(!server||rd<uint8_t*>(server,0x1d0)!=world)return false;const auto index=t.handle&0x3f;if(index>=16)return false;auto slot=server+0x1e0+index*0x148;return rd<uint8_t>(slot,0xa)==3&&exact_session(t.handle,t.account,rd<uint32_t>(slot),rd<uint64_t>(slot,0x48))&&rd<uint32_t>(slot,0x18)==t.entity;}
int32_t* attribute(uint8_t* world,uint32_t entity,const char* type,size_t size,uint32_t id){auto comp=static_cast<uint8_t*>(need(world,entity,type,size));auto reg=singleton(world,"impact::AttributeRegistry");if(!reg)throw std::runtime_error("attribute registry missing");alignas(8) uint8_t result[16]{};f.find(result,id,reg);auto info=rd<uint8_t*>(result,8);if(result[0]||!info||rd<uint32_t>(info)!=rd<uint32_t>(comp)||(comp[6]&1)==0)throw std::runtime_error("attribute identity mismatch");auto slot=rd<uint16_t>(info,12),offset=rd<uint16_t>(comp,4);if(slot>=comp[7]||offset<8||offset+4ull*slot+4>size)throw std::runtime_error("attribute slot outside component");return reinterpret_cast<int32_t*>(comp+offset+4*slot);}
bool alive(uint8_t* world,uint32_t entity){auto actor=component(world,entity,"Actor",3832);if(!actor.ptr||(rd<uint64_t>(actor.ptr,0xbf8)&(uint64_t{1}<<50))==0)return false;return *attribute(world,entity,"Health",40,health_id)>0;}
std::vector<uint32_t> inventories(uint8_t* world,uint32_t entity){auto setup=static_cast<uint8_t*>(need(world,entity,"InventorySetup",84));if(!setup[83])throw std::runtime_error("inventory not initialized");std::vector<uint32_t> ids;for(int i=0;i<16;++i){auto id=rd<uint32_t>(setup,i*4);if(id&&std::find(ids.begin(),ids.end(),id)==ids.end())ids.push_back(id);}return ids;}
bool empty(uint8_t* world,uint32_t entity){for(auto id:inventories(world,entity)){auto inv=static_cast<Inventory*>(need(world,id,"Inventory",96));for(const auto& item:inv->slots)if(item.item&&item.count)return false;}return true;}
bool skills_empty(uint8_t* world,uint32_t entity){auto nodes=static_cast<uint8_t*>(need(world,entity,"UnlockedSkillNodes",2316));for(int i=0;i<256;++i)if(nodes[2052+i]||rd<uint32_t>(nodes,1028+i*4))return false;return true;}
bool proof(uint8_t* world,uint32_t entity){return progress_proof(*attribute(world,entity,"Level",16,level_id),*attribute(world,entity,"Experience",20,xp_id),*attribute(world,entity,"Experience",20,pending_xp_id),empty(world,entity),skills_empty(world,entity));}
// StartLocation is resolved from the world's real spawn point, never hard-coded coordinates.
uint32_t start_point(uint8_t* world,Transform& transform){auto ecs=world+storage;auto registry=rd<uint8_t*>(ecs);if(!registry)return 0;auto types=rd<uint8_t*>(registry,8);auto count=rd<uint64_t>(registry,0x10);if(!types||count<0x100||count>0x280)throw std::runtime_error("type registry invalid");int type=-1;for(uint64_t i=0;i<count;++i)if(rd<uint32_t>(types+i*0x100,0x20)==hash("keen::ecs::PlayerSpawnPoint"))type=static_cast<int>(i);if(type<0)return 0;auto table=rd<uint8_t**>(ecs,0x130);auto capacity=rd<uint32_t>(ecs,0x140);if(!table||capacity>65536)return 0;std::vector<uint8_t*> sorted(table,table+capacity);std::sort(sorted.begin(),sorted.end());uint32_t found=0;
    for(uint32_t i=0;i<capacity;++i){auto arch=table[i];if(!arch||std::count(sorted.begin(),sorted.end(),arch)!=1)continue;if((rd<uint64_t>(arch,(type>>6)*8)&(uint64_t{1}<<(type&63)))==0)continue;auto offset=rd<uint16_t>(arch,0x84+type*2),size=rd<uint16_t>(arch,0xa84+type*2);if(size!=204)throw std::runtime_error("spawn-point size mismatch");auto chunk=rd<uint8_t*>(arch,0x50);for(int guard=0;chunk&&guard<20000;++guard,chunk=rd<uint8_t*>(chunk,8)){auto rows=rd<uint32_t>(chunk,0x10);auto ids=rd<uint16_t>(chunk,0x14);if(rows>8192||!ids)throw std::runtime_error("spawn chunk invalid");for(uint32_t j=0;j<rows;++j)if(chunk[offset+j*size]==0){auto id=rd<uint32_t>(chunk,ids+j*16);auto point=component(world,id,"CurrentTransform",56);if(!point.ptr)continue;if(found&&found!=id)return 0;found=id;std::memcpy(&transform,point.ptr,56);}}}return found;
}
void validate_accessor(void* ctx,const uint64_t* iter,uint8_t* world,size_t index,uint32_t entity,const char* name,size_t size){auto job=rd<uint8_t*>(ctx);void* out[2]{};f.read(out,rd<void*>(job,0x30),iter[write_base+index],entity);auto actual=component(world,entity,name,size);if(!actual.ptr||out[0]!=actual.ptr)throw std::runtime_error(std::string("unverified inventory accessor: ")+name);}
void clear_inventory(void* ctx,const uint64_t* iter,uint8_t* world,uint32_t entity){auto ids=inventories(world,entity);validate_accessor(ctx,iter,world,3,entity,"InventorySetup",84);if(ids.empty())return;validate_accessor(ctx,iter,world,1,ids[0],"Inventory",96);validate_accessor(ctx,iter,world,2,ids[0],"InventoryEmptyState",2);
    auto job=rd<void*>(ctx);if(iter[event_base]!=reinterpret_cast<uintptr_t>(job)||iter[event_base+1]!=reinterpret_cast<uintptr_t>(job))throw std::runtime_error("event writer/iterator mismatch");
    alignas(16) static uint8_t tx[0x71b0];alignas(16) uint8_t setup[0xf0]{};setup[0]=5;wr(setup,8,ctx);wr(setup,0x10,iter[2]);wr(setup,0x18,iter[1]);wr(setup,0x20,iter[3]);
    const std::pair<size_t,size_t> fields[]={{0x28,3},{0x30,1},{0x38,6},{0x40,4},{0x48,5},{0x50,3},{0x58,2},{0x70,0}};for(const auto& field:fields)wr(setup,field.first,iter[write_base+field.second]);wr(setup,0xa0,iter[event_base+1]);wr(setup,0xa8,iter[event_base]);wr(setup,0xc0,iter[4]);wr(setup,0xc8,entity);
    if(reinterpret_cast<void*>(iter[2])!=singleton(world,"g38_items::ItemRegistry")||reinterpret_cast<void*>(iter[3])!=singleton(world,"impact::AttributeRegistry"))throw std::runtime_error("inventory singleton slots mismatch");
    struct Take {uint32_t item,count;uint64_t slot;};std::vector<Take> entries;for(auto id:ids){auto inv=static_cast<Inventory*>(need(world,id,"Inventory",96));for(uint32_t slot=0;slot<8;++slot){auto item=inv->slots[slot];if(item.item&&item.count)entries.push_back({item.item,item.count,uint64_t{id}|(uint64_t{slot}<<32)});}}
    if(entries.empty())return;f.ctor(tx);f.init(tx,setup);bool ok=true;for(const auto& entry:entries){alignas(8) uint8_t result[16]{};f.take(result,tx,entity,entry.item,entry.slot,entry.count);if(result[0]||rd<uint32_t>(result,8)){ok=false;break;}}if(ok)f.commit(tx);else f.rollback(tx);f.free(tx);if(!ok)throw std::runtime_error("atomic inventory removal refused");if(!empty(world,entity))throw std::runtime_error("inventory removal incomplete");
}
std::vector<uint32_t> skill_impacts(uint8_t* world,uint32_t entity){auto nodes=static_cast<uint8_t*>(need(world,entity,"UnlockedSkillNodes",2316));std::vector<uint32_t> impacts;for(int i=0;i<256;++i){auto id=rd<uint32_t>(nodes,1028+i*4);if(id&&std::find(impacts.begin(),impacts.end(),id)==impacts.end())impacts.push_back(id);}for(auto id:impacts){auto owner=component(world,id,"OwnerRelationship",4);if(id==entity||!owner.ptr||rd<uint32_t>(owner.ptr)!=entity)throw std::runtime_error("skill impact ownership unverified");}return impacts;}
void reset_progress(uint8_t* world,uint32_t entity){auto balance=singleton(world,"BalancingRegistry");auto reg=singleton(world,"impact::AttributeRegistry");if(!balance||!reg)throw std::runtime_error("progress registries missing");auto required=f.required(1,balance);if(!required||required>INT32_MAX)throw std::runtime_error("level-one XP curve invalid");struct Change{const char* type;size_t size;uint32_t id;int32_t value;};const Change changes[]={{"Experience",20,required_xp_id,static_cast<int32_t>(required)},{"Experience",20,xp_id,0},{"Experience",20,pending_xp_id,0},{"Level",16,level_id,1}};
    int32_t previous[4]{};for(size_t i=0;i<4;++i)previous[i]=*attribute(world,entity,changes[i].type,changes[i].size,changes[i].id);
    auto impacts=skill_impacts(world,entity);auto nodes=static_cast<uint8_t*>(need(world,entity,"UnlockedSkillNodes",2316));
    for(size_t i=0;i<4;++i){const int64_t delta=int64_t{changes[i].value}-previous[i];if(delta<INT32_MIN||delta>INT32_MAX)throw std::runtime_error("progress delta overflow");if(delta)f.add(need(world,entity,changes[i].type,changes[i].size),reg,static_cast<int32_t>(delta),changes[i].id);}
    // After WaitWorldJobs: no jobs retain these component pointers. Destroy the
    // old skill-impact entities as well as ranks, so their stat buffs cannot survive.
    std::memset(nodes+1028,0,1024);std::memset(nodes+2052,0,256);wr(nodes,2308,rd<uint32_t>(nodes,2308)+1);nodes[2312]++;
    for(auto id:impacts)if(id!=entity)f.destroy(world+storage,id,1,0);
}
void teleport(void* ctx,uint8_t* world,Task& task){auto current=static_cast<Transform*>(need(world,task.entity,"CurrentTransform",56));auto counter=static_cast<uint8_t*>(need(world,task.entity,"TeleportCount",16));auto job=rd<uint8_t*>(ctx);auto buffer=rd<uint8_t*>(job,8);if(!buffer||rd<uint64_t>(buffer,0x18)>uint64_t{1}<<30||rd<uint32_t>(buffer,0x20)>rd<uint64_t>(buffer,0x18))throw std::runtime_error("command buffer invalid");
    Teleport data{};data.target=*current;data.target.position[0]=task.target.position[0];data.target.position[1]=task.target.position[1];data.target.position[2]=task.target.position[2];data.entity=task.start;data.ground=1;
    auto rec=f.queue(buffer,buffer+0x10,3,0x6c+sizeof data,task.entity);if(!rec)return;wr(rec,0x0c,hash("keen::ecs::Teleport"));wr<uint16_t>(rec,0x4c,sizeof data);std::memcpy(rec+0x6c,&data,sizeof data);task.before_count=*counter;task.phase=3;
    // A native checkpoint asks clients to accept the authoritative server state.
    auto checkpoint=f.queue(buffer,buffer+0x10,0x10,0x10,task.entity);if(checkpoint)wr(checkpoint,0x0c,task.entity);
}
void fail(const char* why){faulted.store(true);log("HARDCORE disabled: %s. Pending resets/backups retained; no kick requested.",why);}
void program_run(void* ctx){if(faulted.load())return;auto g=guardian();if(!g.enabled||!g.enabled())return;auto job=rd<uint8_t*>(ctx);auto data=rd<uint8_t*>(job);if(!data||data[8]!=0)return;auto world=data-storage;if(world[0]!=2)return;g.backend(1);auto server=reinterpret_cast<uint8_t*>(server_ptr.load());if(!server||rd<uint8_t*>(server,0x1d0)!=world)return;
    uint64_t iter[128]{};f.begin(ctx,iter,sizeof iter);std::lock_guard lock(tasks_mutex);
    for(int i=0;i<16;++i){auto slot=server+0x1e0+i*0x148;const auto handle=rd<uint32_t>(slot);const auto account=rd<uint64_t>(slot,0x48);const auto entity=rd<uint32_t>(slot,0x18);if(!handle||!account||!entity||rd<uint8_t>(slot,0xa)!=3)continue;auto prior=tasks.find(handle);if(prior!=tasks.end()&&prior->second.account==account&&prior->second.entity==entity&&prior->second.wait_dead&&!alive(world,entity))prior->second.wait_dead=false;auto token=g.poll(handle,account);if(!token)continue;
        auto& task=tasks[handle];if(task.token!=token||task.account!=account||task.entity!=entity)task=Task{handle,entity,0,account,token,0};if(task.phase==4)continue;if(task.phase>0&&GetTickCount64()-task.began>60000){log("HARDCORE %u: timed out waiting for verified native respawn/teleport; pending reset retained.",handle);task.phase=4;continue;}
        if(task.wait_dead||!alive(world,entity)||component(world,entity,"Teleport",80).ptr)continue;
        if(task.phase==0){task.start=start_point(world,task.target);if(!task.start)continue;
            // Preflight all progression structures before touching inventory.
            attribute(world,entity,"Level",16,level_id);attribute(world,entity,"Experience",20,xp_id);attribute(world,entity,"Experience",20,pending_xp_id);attribute(world,entity,"Experience",20,required_xp_id);need(world,entity,"UnlockedSkillNodes",2316);need(world,entity,"LastTransform",56);need(world,entity,"TeleportCount",16);
            skill_impacts(world,entity);task.began=GetTickCount64();clear_inventory(ctx,iter,world,entity);task.phase=1;
        }else if(task.phase==2&&proof(world,entity)){teleport(ctx,world,task);}
    }
}
void post_run(uint8_t* data){if(faulted.load()||!data||data[8]!=0)return;auto g=guardian();if(!g.enabled||!g.enabled())return;auto world=data-storage;if(world[0]!=2)return;f.wait(world);std::lock_guard lock(tasks_mutex);for(auto it=tasks.begin();it!=tasks.end();){auto& task=it->second;if(!session(world,task)){it=tasks.erase(it);continue;}if(task.phase==1){reset_progress(world,task.entity);if(!proof(world,task.entity))throw std::runtime_error("native reset verification failed");task.phase=2;}else if(task.phase==3){auto counter=static_cast<uint8_t*>(need(world,task.entity,"TeleportCount",16));auto current=static_cast<Transform*>(need(world,task.entity,"CurrentTransform",56));if(*counter!=task.before_count&&!component(world,task.entity,"Teleport",80).ptr&&alive(world,task.entity)&&near_start(*current,task.target)&&proof(world,task.entity)){if(g.complete(task.handle,task.account,task.token))log("HARDCORE %u: level/XP, empty inventory, skills and native start-point teleport verified without disconnect.",task.handle);task.phase=4;}}++it;}}
bool seh(void(*fn)(void*),void* arg){__try{fn(arg);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
void program(void* ctx){try{if(!seh(program_run,ctx))fail("native program access fault");}catch(const std::exception& e){fail(e.what());}catch(...){fail("native program exception");}}
void post(void* data){post_run(static_cast<uint8_t*>(data));}
void step(void* data,char ticked){sim_hook.call<void>(data,ticked);try{if(!seh(post,data))fail("native post-step access fault");}catch(const std::exception&e){fail(e.what());}catch(...){fail("native post-step exception");}}
void all_programs(void* world){register_hook.call<void>(world);if(faulted.load())return;try{const auto result=f.reg(world,&descriptor,0);if(result&0xff){registered.store(true);log("HARDCORE native ECS program registered; inactive unless guardian.json hardcore=true.");}else fail("engine refused hardcore program registration");}catch(...){fail("hardcore program registration exception");}}
uintptr_t find(const uint8_t* image,const char* pattern,uint32_t expected){std::vector<int> bytes;std::string p(pattern);for(size_t at=0;at<p.size();){if(p[at]==' '){++at;continue;}if(p[at]=='?'){bytes.push_back(-1);while(at<p.size()&&p[at]=='?')++at;}else{bytes.push_back(std::stoi(p.substr(at,2),nullptr,16));at+=2;}}
    const auto pe=rd<uint32_t>(image,0x3c);const auto section=image+pe+24+rd<uint16_t>(image,pe+20);const auto count=rd<uint16_t>(image,pe+6);uintptr_t match=0;for(uint16_t s=0;s<count;++s){auto head=section+s*40;if(std::memcmp(head,".text",5))continue;auto rva=rd<uint32_t>(head,12),len=rd<uint32_t>(head,8);for(uint32_t i=0;i+bytes.size()<=len;++i){bool same=true;for(size_t k=0;k<bytes.size();++k)if(bytes[k]>=0&&image[rva+i+k]!=bytes[k]){same=false;break;}if(same){if(match)throw std::runtime_error("native pattern is ambiguous");match=reinterpret_cast<uintptr_t>(image)+rva+i;}}}if(!match||match!=reinterpret_cast<uintptr_t>(image)+expected)throw std::runtime_error("native pattern/RVA mismatch at expected " + std::to_string(expected));return match;
}
}
#ifdef HARDCORE_TEST
uint32_t test_start(uint8_t* world,void(*get)(uint64_t*,uint8_t*,uint32_t,uint32_t)){f.get=get;Transform t{};return start_point(world,t);}
#endif
void manager(uintptr_t value){server_ptr.store(value);}
void death(uint32_t handle,uint64_t account){if(faulted.load()||!registered.load())return;auto g=guardian();if(g.enabled&&g.enabled()){g.backend(1);const auto token=g.death(handle,account);if(!token){log("HARDCORE death ignored for handle %u: no verified Guardian identity/reference.",handle);return;}auto server=reinterpret_cast<uint8_t*>(server_ptr.load());if(!server||(handle&0x3f)>=16)return;auto slot=server+0x1e0+(handle&0x3f)*0x148;if(!exact_session(handle,account,rd<uint32_t>(slot),rd<uint64_t>(slot,0x48)))return;auto entity=rd<uint32_t>(slot,0x18);if(!entity)return;std::lock_guard lock(tasks_mutex);auto& task=tasks[handle];if(task.token!=token){task=Task{handle,entity,0,account,token,0};task.wait_dead=true;}}}
bool resolve(const uint8_t* image,bool probe_only){try{const auto pe=rd<uint32_t>(image,0x3c);if(rd<uint32_t>(image,pe+8)!=0x6aeb143e||rd<uint32_t>(image,pe+24+56)!=0x1dde000){log("HARDCORE unavailable on unverified executable; regular Live Map unaffected.");return false;}
#define RESOLVE(field,rva,pattern) f.field=reinterpret_cast<decltype(f.field)>(find(image,pattern,rva))
    RESOLVE(get,0x64ed50,"48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8D BA ?? ?? ?? 00 44 89");
    RESOLVE(read,0x64e9e0,"40 53 48 83 EC 20 48 8B D9 E8 ?? ?? ?? ?? 48 8B C3 48 83 C4 20 5B C3 CC CC CC CC CC CC CC CC CC 4C 8B D1 4D");
    RESOLVE(reg,0x67ede0,"48 89 5C 24 08 48 89 74 24 18 55 57 41 54 41 56 41 57 48 8D 6C 24 C9 48 81 EC A0 00");
    RESOLVE(begin,0x663b30,"48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 60 4C 8B 09 48");
    RESOLVE(queue,0x5f9f60,"48 89 74 24 10 57 48 83 EC 20 44 8B 52 10 48 8B FA 41 8B C2 83 E0 0F 74 07 44 2B D0 41 83 C2 10");
    RESOLVE(find,0x991810,"89 54 24 10 53 48 83 EC 20 48 8B D9 48 8D 54 24 38 49 8D 48 68");
    RESOLVE(required,0x93e900,"4C 8B 4A 20 44 8B D1 4D 85 C9 75 06 B8 01 00 00 00 C3 41 83 FA 05 77 0D 41 8D 4A FF 41 8B 84 89 D8 01 00 00 C3");
    RESOLVE(destroy,0x63e8a0,"48 89 5C 24 18 48 89 74 24 20 89 54 24 10 55 57 41 55 41 56 41 57 48 8D");
    RESOLVE(ctor,0x3ed10,"33 D2 33 C0 48 89 11 48 89 51 08 48 89 51 10 48 89 51 18 48 89 51 20 48 89 51 28 48 89 51 30 48 89 51 38 48 89 51 40 48 89 51 48 48 89 51 50 48 89 51 58 48 89 51 60 48 89 51 68 48 89 51 70 48 89 51 78 48");
    RESOLVE(init,0x1b9300,"C6 81 A8 71 00 00 00 48 8B 42 08 48");RESOLVE(take,0x1b0d90,"48 89 5C 24 08 48 89 74 24 18 48 89 7C 24 20 48 89 54 24 10 55 41 54 41 55 41 56 41 57 48 8D AC 24 20 FF FF");
    RESOLVE(commit,0x1af860,"40 55 53 41 55 48 8D AC 24 30 FC FF");RESOLVE(rollback,0x1b8310,"4C 8B DC 55 56 41 55 41 56 41 57 49");RESOLVE(free,0x3ee60,"48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8B F9 45 33 C0 48 81 C1 40 71 00 00");RESOLVE(wait,0x6ca080,"48 8B 89 ?? ?? E1 00 E9 ?? ?? ?? ??");
#undef RESOLVE
    const auto attr_call=find(image,"44 8B 0D ?? ?? ?? ?? 44 8B C6 48 8B 55 A0 49 8B CE E8 ?? ?? ?? ??",0x191521)+17;f.add=reinterpret_cast<AddAttr>(attr_call+5+rd<int32_t>(reinterpret_cast<void*>(attr_call),1));if(reinterpret_cast<uintptr_t>(f.add)!=reinterpret_cast<uintptr_t>(image)+0x190810)throw std::runtime_error("attribute writer target mismatch");
    const auto sim=find(image,"40 55 53 56 41 55 48 8D AC 24 F8 FF FD FF B8 08 01 02 00 E8 ?? ?? ?? ??",0x6c8f10);
    const auto all=find(image,"40 53 48 83 EC 50 48 8B D9 48 8D 15 ?? ?? ?? ?? 48 8D 4C 24 40 41 B8 24",0x245960);
    descriptor.name={"guardian_hardcore",17};descriptor.update=program;descriptor.sys_read={sreads,std::size(sreads)};descriptor.sys_write={swrites,std::size(swrites)};descriptor.other_read={oreads,std::size(oreads)};descriptor.other_write={owrites,std::size(owrites)};descriptor.add={addtypes,1};descriptor.events_out={events,2};descriptor.flags=0x14;descriptor.interval=1;
    if(probe_only)return true;
    sim_hook=safetyhook::create_inline(reinterpret_cast<void*>(sim),reinterpret_cast<void*>(step));if(!sim_hook)throw std::runtime_error("sim-step hook failed");register_hook=safetyhook::create_inline(reinterpret_cast<void*>(all),reinterpret_cast<void*>(all_programs));if(!register_hook){sim_hook.reset();throw std::runtime_error("program-registration hook failed");}
    log("HARDCORE backend hooks installed for verified server build; no client mod or EMF runtime required.");return true;
}catch(const std::exception&e){log("HARDCORE backend unavailable: %s. Regular Live Map unaffected.",e.what());return false;}}
bool install(const uint8_t* image){return resolve(image,false);}
bool probe(const uint8_t* image){return resolve(image,true);}
}
