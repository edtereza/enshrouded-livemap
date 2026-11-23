#include "hardcore_native.h"
#include <cstdio>
#include <vector>
namespace livemap::hardcore { uint32_t test_start(uint8_t*,void(*)(uint64_t*,uint8_t*,uint32_t,uint32_t)); }
using namespace livemap::hardcore::native;
template<class T>void put(uint8_t* p,size_t off,T value){std::memcpy(p+off,&value,sizeof value);}
Transform spawn_transform{};
void fake_component(uint64_t* out,uint8_t*,uint32_t type,uint32_t entity){out[0]=0;out[1]=0;if(type==hash("keen::ecs::CurrentTransform")&&(entity==91||entity==92)){out[0]=reinterpret_cast<uintptr_t>(&spawn_transform);out[1]=56;}}
int main(){
#define CHECK(x) if(!(x)){std::fprintf(stderr,"Failed line %d: %s\n",__LINE__,#x);return 1;}
    CHECK(exact_session(65,100,65,100));
    CHECK(!exact_session(65,100,129,100)); // same slot, another connection
    CHECK(!exact_session(65,100,65,101)); // another account
    CHECK(!exact_session(65,0,65,0));
    CHECK(progress_proof(1,0,0,true,true));
    CHECK(!progress_proof(1,0,1,true,true));
    CHECK(!progress_proof(1,0,0,false,true));
    CHECK(!progress_proof(1,0,0,true,false));
    CHECK(!progress_proof(2,0,0,true,true));
    Transform a{},b{};a.position[0]=int64_t{25}<<32;CHECK(near_start(a,b));a.position[0]+=1LL<<32;CHECK(!near_start(a,b));
    CHECK(hash("keen::ecs::Teleport")==0x23384d40);
    CHECK(hash("keen::ecs::PlayerSpawnPoint")==0x08060e71);
    // Actual scanner over synthetic ECS memory: the outer world starts with a state,
    // while its entity storage owns the registry and archetype table.
    std::vector<uint8_t> world(storage+0x150),registry(0x20),types(0x100*0x100),arch(0xb00),chunk(0x400);
    uint8_t* arches[]={arch.data()};world[0]=2;
    put(world.data(),storage,registry.data());put(registry.data(),8,types.data());put<uint64_t>(registry.data(),16,0x100);
    put<uint32_t>(types.data(),5*0x100+0x20,hash("keen::ecs::PlayerSpawnPoint"));
    put(world.data(),storage+0x130,arches);put<uint32_t>(world.data(),storage+0x140,1);
    put<uint64_t>(arch.data(),0,uint64_t{1}<<5);put(arch.data(),0x50,chunk.data());put<uint16_t>(arch.data(),0x84+10,0x100);put<uint16_t>(arch.data(),0xa84+10,204);
    put<uint32_t>(chunk.data(),0x10,1);put<uint16_t>(chunk.data(),0x14,0x80);put<uint32_t>(chunk.data(),0x80,91);
    CHECK(livemap::hardcore::test_start(world.data(),fake_component)==91);
    put<uint32_t>(chunk.data(),0x10,2);put<uint32_t>(chunk.data(),0x90,92);
    CHECK(livemap::hardcore::test_start(world.data(),fake_component)==0); // ambiguous start
    chunk[0x100+204]=1; // second is a save point, not the initial location
    CHECK(livemap::hardcore::test_start(world.data(),fake_component)==91);
    chunk[0x100]=2;
    CHECK(livemap::hardcore::test_start(world.data(),fake_component)==0);
    return 0;
}
