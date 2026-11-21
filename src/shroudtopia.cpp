// Native MSVC ABI from https://github.com/s0t7x/shroudtopia/blob/main/include/shroudtopia.h
// ModContext stays opaque: Livemap keeps its settings in livemap.ini.
#include <string>
#include "log.h"

struct ModContext;
struct ModMetaData {
    std::string name;
    std::string description;
    std::string version;
    std::string author;
    std::string targetShroudtopiaVersion;
    bool hasClientSupport;
    bool hasServerSupport;
};

class Mod {
public:
    bool loaded = false;
    bool active = false;
    virtual ~Mod() = default;
    virtual ModMetaData GetMetaData() = 0;
    virtual void Load(ModContext*) = 0;
    virtual void Unload(ModContext*) = 0;
    virtual void Activate(ModContext*) = 0;
    virtual void Deactivate(ModContext*) = 0;
    virtual void Update(ModContext*) = 0;
};

extern "C" bool livemap_activate();

class Livemap final : public Mod {
public:
    ModMetaData GetMetaData() override {
        // Keep strings inside MSVC's inline storage to avoid cross-CRT frees.
        return {"Livemap", "Live world map", LIVEMAP_VERSION, "oggii/edtereza", "", true, true};
    }
    void Load(ModContext*) override { loaded = true; }
    void Unload(ModContext*) override {
        if (active) Deactivate(nullptr);
        loaded = false;
    }
    void Activate(ModContext*) override { active = livemap_activate(); }
    void Deactivate(ModContext*) override {
        if (active) livemap::log("Shroudtopia: restart the game/server to stop Livemap safely.");
        active = false;
    }
    void Update(ModContext*) override {}
};

extern "C" __declspec(dllexport) Mod* CreateModInstance() {
    return new Livemap();
}
