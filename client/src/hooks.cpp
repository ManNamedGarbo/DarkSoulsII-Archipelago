#include "hooks.h"
#include "offsets.h"
#include <spdlog/spdlog.h>
#include "ds2.h"

extern Hooks* GameHooks;

// ============================= Utils =============================

bool showItem = true;

struct Item {
    int idk;
    int itemId;
    int durability;
    short amount;
    char upgrade;
    char infusion;
};

struct ItemStruct {
    Item items[8];
};

#ifdef _M_IX86
struct ParamRow {
    uint32_t paramId;
    uint32_t rewardOffset;
    uint32_t unknown;
};
#elif defined(_M_X64)
struct ParamRow {
    uint8_t padding1[4];
    uint32_t paramId;
    uint8_t padding2[4];
    uint32_t rewardOffset;
    uint8_t padding3[4];
    uint32_t unknown;
};
#endif

// function that allows us to get the itemLotId on giveItemsOnPickup
#ifdef _M_IX86
extern "C" int __cdecl getItemLotId(UINT_PTR thisPtr, UINT_PTR arg1, UINT_PTR arg2, UINT_PTR baseAddress);
#elif defined(_M_X64)
extern "C" int getItemLotId(UINT_PTR thisPtr, UINT_PTR arg1, UINT_PTR baseAddress);
#endif

// hooking this function to always start the game offline
// by blocking the game's dns lookup
typedef INT(__stdcall* getaddrinfo_t)(PCSTR pNodeName, PCSTR pServiceName, const ADDRINFOA* pHints, PADDRINFOA* ppResult);

// fuction is called when player receives a reward (boss, covenant, npc or event)
typedef void(__thiscall* giveItemsOnReward_t)(UINT_PTR thisPtr, UINT_PTR pItemLot, INT idk1, INT idk2, INT idk3);
// fuction is called when player picks up an item
#ifdef _M_IX86
typedef void(__thiscall* giveItemsOnPickup_t)(UINT_PTR thisPtr, UINT_PTR idk1, UINT_PTR idk2);
#elif defined(_M_X64)
typedef void(__thiscall* giveItemsOnPickup_t)(UINT_PTR thisPtr, UINT_PTR idk1);
#endif
// this function is called when the player buys an item
typedef char(__thiscall* giveShopItem_t)(UINT_PTR thisPtr, UINT_PTR param_2, INT param_3);

// this function adds the item to the players inventory
typedef void(__thiscall* addShopItemToInventory_t)(UINT_PTR, UINT_PTR, UINT_PTR);
// this function adds the item to the players inventory
typedef char(__thiscall* addItemsToInventory_t)(UINT_PTR thisPtr, ItemStruct* itemsList, INT amountToGive, INT param_3);
// this function creates the structure that is passed to the function that displays the item popup
typedef void(__cdecl* createPopupStructure_t)(UINT_PTR displayStruct, ItemStruct* items, INT amountOfItems, INT displayMode);
// this function displays the item popup
typedef void(__thiscall* showItemPopup_t)(UINT_PTR thisPtr, UINT_PTR displayStruct);

// this function given an itemId return a pointer to the item's name
typedef const wchar_t* (__cdecl* getItemNameFromId_t)(INT32 arg1, INT32 itemId);

getaddrinfo_t originalGetaddrinfo = nullptr;

giveItemsOnReward_t originalGiveItemsOnReward = nullptr;
giveItemsOnPickup_t originalGiveItemsOnPickup = nullptr;
giveShopItem_t originalGiveShopItem = nullptr;

addShopItemToInventory_t originalAddShopItemToInventory = nullptr;
addItemsToInventory_t originalAddItemsToInventory = nullptr;
createPopupStructure_t originalCreatePopupStructure = nullptr;
showItemPopup_t originalShowItemPopup = nullptr;

getItemNameFromId_t originalGetItemNameFromId = nullptr;

uintptr_t baseAddress;
int unusedItemForShop = 60375000;
int unusedItemForPopup = 65240000;
std::wstring messageToDisplay = L"archipelago message";

// this strategy with the booleans is not the best
// but if we dont call the original the item wont be removed from the map
bool giveNextItem = true;
bool showNextItem = true;

uintptr_t GetPointerAddress(uintptr_t gameBaseAddr, uintptr_t address, std::vector<uintptr_t> offsets)
{
    uintptr_t offset_null = NULL;
    ReadProcessMemory(GetCurrentProcess(), (LPVOID*)(gameBaseAddr + address), &offset_null, sizeof(offset_null), 0);
    uintptr_t pointeraddress = offset_null; // the address we need
    for (size_t i = 0; i < offsets.size() - 1; i++) // we dont want to change the last offset value so we do -1
    {
        ReadProcessMemory(GetCurrentProcess(), (LPVOID*)(pointeraddress + offsets.at(i)), &pointeraddress, sizeof(pointeraddress), 0);
    }
    return pointeraddress += offsets.at(offsets.size() - 1); // adding the last offset
}

void PatchMemory(uintptr_t address, const std::vector<BYTE>& bytes) {
    DWORD oldProtect;
    VirtualProtect(reinterpret_cast<void*>(address), bytes.size(), PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy(reinterpret_cast<void*>(address), bytes.data(), bytes.size());
    VirtualProtect(reinterpret_cast<void*>(address), bytes.size(), oldProtect, &oldProtect);
}

void overrideItemPrices() {

    uintptr_t itemPramPtr = GetPointerAddress(baseAddress, PointerOffsets::BaseA, ParamOffsets::ItemParam);
    ParamRow* rowPtr = reinterpret_cast<ParamRow*>(itemPramPtr + 0x44 - sizeof(uintptr_t)); // 0x3C for x64 and 0x40 for x40

    for (int i = 0; i < 10000; ++i) {
        if (rowPtr[i].paramId == 0) return; // return if we reach the end
        uintptr_t rewardPtr = itemPramPtr + rowPtr[i].rewardOffset;

        uint32_t price = 1;
        uintptr_t pricePtr = rewardPtr + 0x30;
        WriteProcessMemory(GetCurrentProcess(), (LPVOID*)pricePtr, &price, sizeof(uint32_t), NULL);
    }
}

void Hooks::overrideShopParams() {

#ifdef _M_IX86
    // for some reason in vanilla when you go through the start menu
    // they always override the params with the defaults
    // this patch makes the game not override the ShopLineupParam (and maybe others?)
    // this doesnt happen in sotfs
    PatchMemory(baseAddress + 0x316A9F, { 0x90, 0x90, 0x90, 0x90, 0x90 });
#endif

    // set all items base prices to 1 so that we can
    // set the values properly in the shop params
    overrideItemPrices();

    uintptr_t shopParamPtr = GetPointerAddress(baseAddress, PointerOffsets::BaseA, ParamOffsets::ShopLineupParam);
    ParamRow *rowPtr = reinterpret_cast<ParamRow*>(shopParamPtr+0x44-sizeof(uintptr_t)); // 0x3C for x64 and 0x40 for x40

    for (int i = 0; i < 10000; ++i) {      
        if (rowPtr[i].paramId == 0) return; // return if we reach the end

        uintptr_t rewardPtr = shopParamPtr + rowPtr[i].rewardOffset;

        // we need to always set the price rate for situations where the item price is set to 1 but the location is excluded
        // this happens, for example, with the infinite lifegems if the infinite lifegem option is set
        float_t price_rate = shopPrices[rowPtr[i].paramId];
        uintptr_t ratePtr = rewardPtr + 0x1C;
        WriteProcessMemory(GetCurrentProcess(), (LPVOID*)ratePtr, &price_rate, sizeof(float_t), NULL);

        if (!locationRewards.contains(rowPtr[i].paramId)) continue; // skip if its not an archipelago location

        locationReward archipelagoReward = locationRewards[rowPtr[i].paramId];
        if (archipelagoReward.isLocal) {
            WriteProcessMemory(GetCurrentProcess(), (LPVOID*)rewardPtr, &archipelagoReward.item_id, sizeof(uint32_t), NULL);
        }
        else {
            WriteProcessMemory(GetCurrentProcess(), (LPVOID*)rewardPtr, &unusedItemForShop, sizeof(uint32_t), NULL);
        }

        //float_t enable_flag = -1.0f;
        //uintptr_t enablePtr = rewardPtr + 0x8;
        //WriteProcessMemory(GetCurrentProcess(), (LPVOID*)enablePtr, &enable_flag, sizeof(float_t), NULL);

        // make sure items are not removed from the shops
        // so that the player doesnt lose any checks
        float_t disable_flag = -1.0f;
        uintptr_t disablePtr = rewardPtr + 0xC;
        WriteProcessMemory(GetCurrentProcess(), (LPVOID*)disablePtr, &disable_flag, sizeof(float_t), NULL);

        uint8_t amount = 1;
        uintptr_t amountPtr = rewardPtr + 0x20;
        WriteProcessMemory(GetCurrentProcess(), (LPVOID*)amountPtr, &amount, sizeof(uint8_t), NULL);  
    }
}

// TODO: receive the other item information like amount, upgrades and infusions
void Hooks::giveItems(std::vector<int> ids) {

    if (ids.size() > 8) {
        return;
    }

    ItemStruct itemStruct;

    for (size_t i = 0; i < ids.size() && i < 8; ++i) {
        Item item;
        item.idk = 0;
        item.itemId = ids[i];
        item.durability = -1;
        item.amount = 1;
        item.upgrade = 0;
        item.infusion = 0;

        itemStruct.items[i] = item;
    }

    unsigned char displayStruct[0x200];

    if (giveNextItem) {
        originalAddItemsToInventory(GetPointerAddress(baseAddress, PointerOffsets::BaseA, PointerOffsets::AvailableItemBag), &itemStruct, ids.size(), 0);
    }
    originalCreatePopupStructure((UINT_PTR)displayStruct, &itemStruct, ids.size(), 1);
    if (showNextItem) {
        originalShowItemPopup(GetPointerAddress(baseAddress, PointerOffsets::BaseA, PointerOffsets::ItemGiveWindow), (UINT_PTR)displayStruct);
    }
}

std::wstring removeSpecialCharacters(const std::wstring& input) {
    std::set<wchar_t> allowedChars = {
        L'-', L'\'', L',', L' ', L':'
    };

    std::wstring output;
    for (wchar_t ch : input) {
        // Keep the character if it's alphanumeric or in the allowed special characters list
        if (std::isalnum(ch) || allowedChars.find(ch) != allowedChars.end()) {
            output += ch;
        }
    }
    return output;
}

void Hooks::showLocationRewardMessage(int32_t locationId) {
    if (!locationRewards.contains(locationId)) {
        return;
    }

    locationReward reward = locationRewards[locationId];

    // we don't want to show this message
    // for item from our world
    if (reward.isLocal) {
        return;
    }

    // the game uses utf-16 so we convert to wide string
    std::wstring player_name_wide(reward.player_name.begin(), reward.player_name.end());
    std::wstring item_name_wide(reward.item_name.begin(), reward.item_name.end());

    messageToDisplay = player_name_wide + L"'s " + item_name_wide;

    // remove most special characters, since things like # crash the game
    messageToDisplay = removeSpecialCharacters(messageToDisplay);

    showNextItem = true;
    giveNextItem = false;
    giveItems({ unusedItemForPopup });
}

// ============================= HOOKS =============================

INT __stdcall detourGetaddrinfo(PCSTR address, PCSTR port, const ADDRINFOA* pHints, PADDRINFOA* ppResult) {

    if (address && strcmp(address, GameHooks->addressToBlock) == 0) {
        return EAI_FAIL;
    }

    return originalGetaddrinfo(address, port, pHints, ppResult);
}

#ifdef _M_IX86
void __fastcall detourGiveItemsOnReward(UINT_PTR thisPtr, void* Unknown, UINT_PTR pItemLot, INT idk1, INT idk2, INT idk3) {
#elif defined(_M_X64)
void __cdecl detourGiveItemsOnReward(UINT_PTR thisPtr, UINT_PTR pItemLot, INT idk1, INT idk2, INT idk3) {
#endif

    int32_t itemLotId;
    ReadProcessMemory(GetCurrentProcess(), (LPVOID*)(pItemLot), &itemLotId, sizeof(itemLotId), NULL);
    spdlog::debug("was rewarded: {}", itemLotId);

    if (GameHooks->locationsToCheck.contains(itemLotId)) {
        GameHooks->checkedLocations.push_back(itemLotId);
        GameHooks->locationsToCheck.erase(itemLotId);
        GameHooks->showLocationRewardMessage(itemLotId);
        return;
    }

    return originalGiveItemsOnReward(thisPtr, pItemLot, idk1, idk2, idk3);
}

void detourGiveItemsOnPickupLogic(int32_t itemLotId){
    spdlog::debug("picked up: {}", itemLotId);

    if (itemLotId == -1) spdlog::warn("error finding out what itemLot was picked up");

    // 0 means its an item we dropped
    if (itemLotId != 0 && GameHooks->locationsToCheck.contains(itemLotId)) {
        GameHooks->checkedLocations.push_back(itemLotId);
        GameHooks->locationsToCheck.erase(itemLotId);
        GameHooks->showLocationRewardMessage(itemLotId);
        giveNextItem = false;
        showNextItem = false;
    }
}

#ifdef _M_IX86
void __fastcall detourGiveItemsOnPickup(UINT_PTR thisPtr, void* Unknown, UINT_PTR idk1, UINT_PTR idk2) {
    int32_t itemLotId = getItemLotId(thisPtr, idk1, idk2, baseAddress);
    detourGiveItemsOnPickupLogic(itemLotId);
    return originalGiveItemsOnPickup(thisPtr, idk1, idk2);
}
#elif defined(_M_X64)
void __cdecl detourGiveItemsOnPickup(UINT_PTR thisPtr, UINT_PTR idk1) {
    int32_t itemLotId = getItemLotId(thisPtr, idk1, baseAddress);
    detourGiveItemsOnPickupLogic(itemLotId);
    return originalGiveItemsOnPickup(thisPtr, idk1);
}
#endif

#ifdef _M_IX86
char __fastcall detourGiveShopItem(UINT_PTR thisPtr, void* Unknown, UINT_PTR param_2, INT param_3) {
#elif defined(_M_X64)
char __cdecl detourGiveShopItem(UINT_PTR thisPtr, UINT_PTR param_2, INT param_3) {
#endif
    int32_t shopLineupId;
    uintptr_t ptr = param_2 + 2 * sizeof(uintptr_t); // 0x8 in x32 and 0x10 in x64
    ReadProcessMemory(GetCurrentProcess(), (LPVOID*)ptr, &shopLineupId, sizeof(shopLineupId), NULL);
    spdlog::debug("just bought: {}", shopLineupId);

    if (GameHooks->locationsToCheck.contains(shopLineupId)) {
        GameHooks->checkedLocations.push_back(shopLineupId);
        GameHooks->locationsToCheck.erase(shopLineupId);
        GameHooks->showLocationRewardMessage(shopLineupId);
        giveNextItem = false;
        showNextItem = false;
    }

    return originalGiveShopItem(thisPtr, param_2, param_3);
}

#ifdef _M_X64
void __cdecl detourAddShopItemToInventory(UINT_PTR thisPtr, UINT_PTR param_2, UINT_PTR param_3) {
    if (!giveNextItem) {
        giveNextItem = true;
        return;
    }

    return originalAddShopItemToInventory(thisPtr, param_2, param_3);
}
#endif

#ifdef _M_IX86
char __fastcall detourAddItemsToInventory(UINT_PTR thisPtr, void* Unknown, ItemStruct* itemsList, INT amountToGive, INT param_3) {
#elif defined(_M_X64)
char __cdecl detourAddItemsToInventory(UINT_PTR thisPtr, ItemStruct* itemsList, INT amountToGive, INT param_3) {
#endif
    if (!giveNextItem) {
        giveNextItem = true;
        return 1;
    }

    return originalAddItemsToInventory(thisPtr, itemsList, amountToGive, param_3);
}

#ifdef _M_IX86
void __fastcall detourShowItemPopup(UINT_PTR thisPtr, void* Unknown, UINT_PTR displayStruct) {
#elif defined(_M_X64)
void __cdecl detourShowItemPopup(UINT_PTR thisPtr, UINT_PTR displayStruct) {
#endif
    if (!showNextItem) {
        showNextItem = true;
        return;
    }

    return originalShowItemPopup(thisPtr, displayStruct);
}

const wchar_t* __cdecl detourGetItemNameFromId(INT32 arg1, INT32 itemId) {

    if (itemId == unusedItemForPopup) {
        return messageToDisplay.c_str();
    }
    if (itemId == unusedItemForShop) {
        return L"archipelago item";
    }

    return originalGetItemNameFromId(arg1, itemId);
}

void Hooks::patchWeaponRequirements() {
    // makes it so the function that checks requirements on onehand/twohand return without checking
    PatchMemory(baseAddress + PatchesOffsets::noWeaponReqPatchOffset, Patches::noWeaponReqPatch);
    // makes it so it doesnt load the values for the requirements to show in the menu
    // this is mostly to not show the "unable to use this item efficiently" message
    PatchMemory(baseAddress + PatchesOffsets::menuWeaponReqPatchOffset, Patches::menuWeaponReqPatch);
}

bool Hooks::initHooks() {

    HMODULE hModule = GetModuleHandleA("DarkSoulsII.exe");
    baseAddress = (uintptr_t)hModule;

    MH_Initialize();

    MH_CreateHookApi(L"ws2_32", "getaddrinfo", &detourGetaddrinfo, (LPVOID*)&originalGetaddrinfo);

    MH_CreateHook((LPVOID)(baseAddress + FunctionOffsets::GiveItemsOnReward), &detourGiveItemsOnReward, (LPVOID*)&originalGiveItemsOnReward);
    MH_CreateHook((LPVOID)(baseAddress + FunctionOffsets::GiveItemsOnPickup), &detourGiveItemsOnPickup, (LPVOID*)&originalGiveItemsOnPickup);
    MH_CreateHook((LPVOID)(baseAddress + FunctionOffsets::GiveShopItem), &detourGiveShopItem, (LPVOID*)&originalGiveShopItem);

#ifdef _M_X64
    MH_CreateHook((LPVOID)(baseAddress + FunctionOffsets::AddShopItemToInventory), &detourAddShopItemToInventory, (LPVOID*)&originalAddShopItemToInventory);
#endif
    MH_CreateHook((LPVOID)(baseAddress + FunctionOffsets::AddItemsToInventory), &detourAddItemsToInventory, (LPVOID*)&originalAddItemsToInventory);
    originalCreatePopupStructure = reinterpret_cast<createPopupStructure_t>(baseAddress + FunctionOffsets::CreatePopUpStruct);
    MH_CreateHook((LPVOID)(baseAddress + FunctionOffsets::ShowItemPopup), &detourShowItemPopup, (LPVOID*)&originalShowItemPopup);

    MH_CreateHook((LPVOID)(baseAddress + FunctionOffsets::GetItemNameFromId), &detourGetItemNameFromId, (LPVOID*)&originalGetItemNameFromId);

    MH_EnableHook(MH_ALL_HOOKS);

    return true;
}

int prevHp, curHp = -1;
bool Hooks::playerJustDied() {
    prevHp = curHp;
    ReadProcessMemory(GetCurrentProcess(), (LPVOID*)GetPointerAddress(baseAddress, PointerOffsets::BaseA, PointerOffsets::HP), &curHp, sizeof(int), NULL);
    if (prevHp != curHp && prevHp > 0 && curHp <= 0) {
        spdlog::debug("YOU DIED");
        return true;
    }
    return false;
}

bool Hooks::killPlayer() {
    int curHp;
    ReadProcessMemory(GetCurrentProcess(), (LPVOID*)GetPointerAddress(baseAddress, PointerOffsets::BaseA, PointerOffsets::HP), &curHp, sizeof(int), NULL);
    if (curHp > 0) {
        int zeroHp = 0;
        WriteProcessMemory(GetCurrentProcess(), (LPVOID*)GetPointerAddress(baseAddress, PointerOffsets::BaseA, PointerOffsets::HP), &zeroHp, sizeof(int), NULL);
        return true;
    }
    return false;
}

bool Hooks::isPlayerInGame() {
    int value;
    ReadProcessMemory(GetCurrentProcess(), (LPVOID*)GetPointerAddress(baseAddress, PointerOffsets::BaseA, PointerOffsets::PlayerCtrl), &value, sizeof(int), NULL);
    if (value != 0) {
        return true;
    }
    return false;
}