#include "objects.h"
#include <cstdio>

int main() {
    using namespace livemap::objects;
    if (!is_player_tombstone("Base_LootContainer_Player_LootCorpse_Tombstone", 0) ||
        !is_player_tombstone("OtherRuntimeName", 12) || is_player_tombstone("Prop_Deco_Villageprops_Tombstone_07", 8) ||
        is_player_tombstone("VFX_Player_LootCorpse_Tombstone_OnSpawn", 0)) return 1;
    Tombstone stone;
    stone.entity = 10; stone.marker = "uuid-a"; stone.x = 100; stone.y = 40; stone.z = 200;
    Store store;
    if (!store.update_tombstones({stone}, {}, 100)) return 2;
    const auto initial = store.version();
    if (store.update_tombstones({stone}, {}, 101) || store.version() != initial) return 3;
    if (!store.update_tombstones({}, {}, 102)) return 4;
    auto json = store.objects_json();
    if (json.find("\"loaded\":false") == json.npos || json.find("\"collected\":false") == json.npos) return 5;
    if (!store.update_tombstones({stone}, {}, 103)) return 6;
    stone.empty = true; stone.inventory_ready = false;
    store.update_tombstones({stone}, {}, 104);
    if (store.objects_json().find("\"collected\":true") != json.npos) return 7;
    stone.inventory_ready = true;
    if (!store.update_tombstones({stone}, {}, 105)) return 8;
    if (store.objects_json().find("\"collected\":true") == json.npos) return 9;
    stone.empty = false;
    store.update_tombstones({stone}, {}, 106);
    if (store.objects_json().find("\"collected\":true") == json.npos) return 10; // Confirmed collection is sticky.
    Store removal;
    stone.removing = true;
    removal.update_tombstones({stone}, {}, 100);
    if (removal.objects_json().find("\"collected\":true") == json.npos) return 11;

    Store owners;
    stone.removing = false;
    const livemap::deaths::Death alice{"Alice", 99, 0, 100, 200};
    const livemap::deaths::Death bob{"Bob", 100, 0, 100, 200};
    owners.update_tombstones({stone}, {alice}, 100);
    json = owners.objects_json();
    if (json.find("\"owner\":\"Alice\"") == json.npos || json.find("nearby-death") == json.npos) return 12;
    owners.update_tombstones({stone}, {alice, bob}, 101);
    if (owners.objects_json().find("\"owner\":\"\"") == json.npos) return 13;
    stone.owner = "Owner \"quoted\" } & <name>";
    owners.update_tombstones({stone}, {alice, bob}, 102);
    json = owners.objects_json();
    if (json.find("\"ownerSource\":\"entity\"") == json.npos) return 14;
    Store restored;
    if (!restored.from_json(json)) return 15;
    if (restored.objects_json().find("\"loaded\":false") == json.npos) return 16;
    stone.owner.clear(); stone.entity = 11; // Stable marker IDs survive changes to runtime entity IDs.
    restored.update_tombstones({stone}, {}, 1000);
    json = restored.objects_json();
    if (json.find("quoted") == json.npos || json.find("\"ownerSource\":\"entity\"") == json.npos) return 17;
    if (json.find("uuid-a") == json.rfind("uuid-a")) return 18; // Both key and marker are serialized.
    Store old;
    if (!old.from_json("{\"chests\":[],\"switches\":[]}")) return 19;
    if (restored.objects_json(true, false).find("uuid-a") != json.npos) return 20;
    store.update_tombstones({}, {}, 4000);
    if (store.objects_json().find("uuid-a") != json.npos) return 21;
    std::puts("Tombstone identity, collection, unloading, persistence and owner confidence: OK");
    return 0;
}
