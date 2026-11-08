// Phantasm Showcase - Items: see Items.h.

#include "PCH.h"

#include "Items.h"

#include "People.h"
#include "Text.h"

namespace Showcase::Items
{
    namespace
    {
        std::vector<Entry>                          g_entries;   // by name
        std::unordered_map<std::uint32_t, std::size_t> g_byId;

        std::string PluginOf(const RE::TESForm* f)
        {
            const auto* file = f->GetFile(0);
            return file ? std::string(file->GetFilename()) : std::string();
        }

        bool HasModel(RE::TESObjectARMO* a)
        {
            for (int i = 0; i < 2; ++i) {
                const char* m = a->worldModels[i].GetModel();
                if (m && *m) return true;
            }
            return false;
        }

        template <class T>
        void Add(T* f, ItemCore::Kind kind, bool enchanted)
        {
            const char* name = f->GetFullName();
            if (!name || !*name) return;
            Entry e;
            e.id = f->GetFormID();
            e.name = name;
            e.folded = Text::Fold(name);
            e.plugin = PluginOf(f);
            e.kind = kind;
            e.enchanted = enchanted;
            e.value = f->GetGoldValue();
            g_entries.push_back(std::move(e));
        }

        bool Matches(const Entry& e, const std::vector<std::string>& words, std::string_view filter)
        {
            return ItemCore::InFilter(e.kind, filter) && Text::MatchesAll(e.folded, words);
        }
    }

    void Build()
    {
        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data) return;
        for (auto* w : data->GetFormArray<RE::TESObjectWEAP>()) {
            if (!w || !w->GetPlayable()) continue;
            const char* model = w->GetModel();
            if (!model || !*model) continue;
            Add(w, ItemCore::WeaponKind(static_cast<std::uint32_t>(w->GetWeaponType())), w->formEnchanting != nullptr);
        }
        for (auto* a : data->GetFormArray<RE::TESObjectARMO>()) {
            if (!a || !a->GetPlayable() || !HasModel(a)) continue;
            Add(a, ItemCore::ArmorKind(a->GetSlotMask().underlying()), a->formEnchanting != nullptr);
        }
        std::sort(g_entries.begin(), g_entries.end(), [](const Entry& a, const Entry& b) {
            return a.folded != b.folded ? a.folded < b.folded : a.id < b.id;
        });
        g_byId.clear();
        for (std::size_t i = 0; i < g_entries.size(); ++i) g_byId[g_entries[i].id] = i;
        SKSE::log::info("items: {} weapons and armour indexed", g_entries.size());
    }

    const Entry* Get(std::uint32_t id)
    {
        const auto it = g_byId.find(id);
        return it == g_byId.end() ? nullptr : &g_entries[it->second];
    }

    nlohmann::json Card(const Entry& e)
    {
        return { { "formId", People::Hex(e.id) },
                 { "name", e.name },
                 { "plugin", e.plugin },
                 { "kind", std::string(ItemCore::Word(e.kind)) },
                 { "enchanted", e.enchanted },
                 { "value", e.value } };
    }

    nlohmann::json Search(std::string_view text, std::string_view filter, std::size_t offset, std::size_t limit)
    {
        const auto words = Text::Words(text);
        auto items = nlohmann::json::array();
        std::size_t total = 0;
        for (const auto& e : g_entries) {
            if (!Matches(e, words, filter)) continue;
            if (total >= offset && items.size() < limit) items.push_back(Card(e));
            ++total;
        }
        return { { "total", total }, { "items", std::move(items) } };
    }

    nlohmann::json Held(RE::Actor* actor, std::string_view text, std::string_view filter)
    {
        auto items = nlohmann::json::array();
        if (!actor) return { { "total", 0 }, { "items", std::move(items) } };
        const auto words = Text::Words(text);
        std::vector<std::pair<const Entry*, std::pair<std::int32_t, bool>>> found;
        for (const auto& [obj, data] : actor->GetInventory()) {
            if (!obj || data.first <= 0) continue;
            const auto* e = Get(obj->GetFormID());
            if (!e || !Matches(*e, words, filter)) continue;
            found.push_back({ e, { data.first, data.second && data.second->IsWorn() } });
        }
        std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first->folded < b.first->folded; });
        for (const auto& [e, cw] : found) {
            auto c = Card(*e);
            c["count"] = cw.first;
            c["worn"] = cw.second;
            items.push_back(std::move(c));
        }
        return { { "total", items.size() }, { "items", std::move(items) } };
    }
}
