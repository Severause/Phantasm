// Phantasm Showcase - TryOn: see TryOn.h.

#include "PCH.h"

#include "TryOn.h"

#include "Items.h"
#include "ItemCore.h"
#include "Lane.h"
#include "People.h"
#include "TryOnCore.h"
#include "Views.h"

namespace Showcase::TryOn
{
    namespace
    {
        struct Piece
        {
            std::uint32_t      item = 0;
            RE::ExtraDataList* list = nullptr;   // the stack it was worn from; checked against the inventory before use
        };

        struct Session
        {
            std::uint32_t       actor = 0;
            std::uint32_t       generation = 0;
            std::vector<Piece>  original;
            TryOnCore::Ledger   lent;
        };

        // Lane only (every job below runs there), so no lock.
        std::optional<Session> g_session;

        bool IsWorn(const RE::ExtraDataList* list)
        {
            return list && (list->HasType<RE::ExtraWorn>() || list->HasType<RE::ExtraWornLeft>());
        }

        /// Every armour stack the actor wears now.
        std::vector<Piece> WornArmor(RE::Actor* actor)
        {
            std::vector<Piece> out;
            auto* changes = actor->GetInventoryChanges();
            if (!changes || !changes->entryList) return out;
            for (auto* entry : *changes->entryList) {
                if (!entry || !entry->object || !entry->object->IsArmor() || !entry->extraLists) continue;
                for (auto* list : *entry->extraLists) {
                    if (IsWorn(list)) out.push_back({ entry->object->GetFormID(), list });
                }
            }
            return out;
        }

        nlohmann::json WornCards(RE::Actor* actor)
        {
            auto out = nlohmann::json::array();
            for (const auto& p : WornArmor(actor)) {
                if (const auto* e = Items::Get(p.item)) out.push_back(Items::Card(*e));
            }
            return out;
        }

        std::int32_t CountOf(RE::Actor* actor, RE::TESBoundObject* obj)
        {
            const auto counts = actor->GetInventoryCounts();
            const auto it = counts.find(obj);
            return it != counts.end() ? it->second : 0;
        }

        std::int32_t StackCount(RE::ExtraDataList* list)
        {
            const auto* xc = list ? list->GetByType<RE::ExtraCount>() : nullptr;
            return (xc && xc->count > 0) ? xc->count : 1;
        }

        bool PlainList(RE::ExtraDataList* list)
        {
            ItemCore::Stack s;
            if (list) {
                s.soul = list->GetByType<RE::ExtraSoul>() != nullptr;
                s.tempered = list->GetByType<RE::ExtraHealth>() != nullptr;
                if (const auto* text = list->GetByType<RE::ExtraTextDisplayData>()) {
                    s.named = ItemCore::IsName(text->displayName.size() > 0, text->IsPlayerSet(),
                                               text->ownerQuest || text->displayNameText, s.tempered);
                }
                s.enchanted = list->GetByType<RE::ExtraEnchantment>() != nullptr;
            }
            return ItemCore::Plain(s);
        }

        /// The stacks of obj and how many copies sit in plain lists, and in no list at all.
        struct Stacks
        {
            std::vector<RE::ExtraDataList*> lists;
            std::int32_t listed = 0;
            std::int32_t plainListed = 0;
        };

        Stacks StacksOf(RE::Actor* actor, RE::TESBoundObject* obj)
        {
            Stacks out;
            auto* changes = actor->GetInventoryChanges();
            if (!changes || !changes->entryList) return out;
            for (auto* entry : *changes->entryList) {
                if (!entry || entry->object != obj || !entry->extraLists) continue;
                for (auto* list : *entry->extraLists) {
                    if (!list) continue;
                    out.lists.push_back(list);
                    out.listed += StackCount(list);
                    if (PlainList(list)) out.plainListed += StackCount(list);
                }
            }
            return out;
        }

        /// Takes back up to `count` plain copies of obj: copies in no list first (capped at their number, so the
        /// engine never chooses a list), then each plain list by its own pointer. Returns how many went.
        std::int32_t TakeBackPlain(RE::Actor* actor, RE::TESBoundObject* obj, std::int32_t count)
        {
            std::int32_t removed = 0;
            // One stack per pass, found again each time: a removal can free the list just used.
            for (int guard = 0; removed < count && guard < 64; ++guard) {
                const auto stacks = StacksOf(actor, obj);
                const std::int32_t held = CountOf(actor, obj);
                const std::int32_t countOnly = std::max(0, held - stacks.listed);
                RE::ExtraDataList* match = nullptr;
                std::int32_t       inMatch = 0;
                for (auto* list : stacks.lists) {
                    if (PlainList(list)) {
                        match = list, inMatch = StackCount(list);
                        break;
                    }
                }
                if (!match && countOnly == 0) break;
                const std::int32_t take = std::min(count - removed, countOnly > 0 ? countOnly : inMatch);
                actor->RemoveItem(obj, take, RE::ITEM_REMOVE_REASON::kRemove, countOnly > 0 ? nullptr : match, nullptr);
                const std::int32_t went = held - CountOf(actor, obj);
                if (went <= 0) break;
                removed += went;
            }
            return removed;
        }

        RE::Actor* Resolve(std::uint32_t id, std::string& refusal)
        {
            auto* actor = id ? RE::TESForm::LookupByID<RE::Actor>(id) : nullptr;
            TryOnCore::Who who;
            who.player = actor && actor->IsPlayerRef();
            who.child = actor && actor->IsChild();
            who.dead = actor && actor->IsDead();
            who.loaded = actor && !actor->IsDisabled() && actor->Is3DLoaded();
            refusal = actor ? std::string(TryOnCore::Refusal(who)) : std::string("notLoaded");
            return refusal.empty() ? actor : nullptr;
        }

        /// Ends the session: unequips what it put on, puts the recorded pieces back, takes the lent copies back.
        /// Lane. Returns the copies taken back.
        std::int32_t RevertNow(Session& s)
        {
            if (s.generation != Lane::Generation()) return 0;   // the save it belonged to is gone
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(s.actor);
            if (!actor) return 0;
            auto* eq = RE::ActorEquipManager::GetSingleton();
            auto  inOriginal = [&](const Piece& p) {
                return std::any_of(s.original.begin(), s.original.end(),
                                   [&](const Piece& o) { return o.item == p.item && o.list == p.list; });
            };
            for (const auto& p : WornArmor(actor)) {
                if (inOriginal(p)) continue;
                if (auto* obj = RE::TESForm::LookupByID<RE::TESBoundObject>(p.item))
                    eq->UnequipObject(actor, obj, p.list, 1, nullptr, false, false, false, true);
            }
            for (const auto& o : s.original) {
                auto* obj = RE::TESForm::LookupByID<RE::TESBoundObject>(o.item);
                if (!obj || CountOf(actor, obj) <= 0) continue;
                const auto stacks = StacksOf(actor, obj);
                const bool listHere = std::find(stacks.lists.begin(), stacks.lists.end(), o.list) != stacks.lists.end();
                if (listHere && IsWorn(o.list)) continue;
                eq->EquipObject(actor, obj, listHere ? o.list : nullptr, 1, nullptr, false, false, false, true);
            }
            std::int32_t taken = 0;
            for (const auto& [item, count] : s.lent.All()) {
                auto* obj = RE::TESForm::LookupByID<RE::TESBoundObject>(item);
                if (!obj) continue;
                const auto stacks = StacksOf(actor, obj);
                const std::int32_t plain = stacks.plainListed + std::max(0, CountOf(actor, obj) - stacks.listed);
                taken += TakeBackPlain(actor, obj, TryOnCore::TakeBack(count, plain));
            }
            Views::RebuildActor3D(s.actor);
            SKSE::log::info("tryon: reverted {:08X}, {} lent cop(ies) taken back", s.actor, taken);
            return taken;
        }
    }

    void Begin(std::uint32_t actorId, Done done)
    {
        const auto gen = Lane::Generation();
        Lane::Run([actorId, gen, done = std::move(done)]() {
            if (gen != Lane::Generation()) return done(false, "loading");
            std::string refusal;
            auto* actor = Resolve(actorId, refusal);
            if (!actor) return done(true, { { "refusal", refusal } });
            if (g_session && g_session->actor != actorId) {
                RevertNow(*g_session);
                g_session.reset();
            }
            if (!g_session) {
                Session s;
                s.actor = actorId;
                s.generation = gen;
                s.original = WornArmor(actor);
                g_session = std::move(s);
                SKSE::log::info("tryon: session for {:08X}, {} piece(s) worn", actorId, g_session->original.size());
            }
            done(true, { { "worn", WornCards(actor) } });
        });
    }

    void Wear(std::uint32_t actorId, std::uint32_t itemId, Done done)
    {
        Lane::Run([actorId, itemId, done = std::move(done)]() {
            if (!g_session || g_session->actor != actorId || g_session->generation != Lane::Generation())
                return done(false, "noSession");
            std::string refusal;
            auto* actor = Resolve(actorId, refusal);
            if (!actor) return done(false, refusal);
            const auto* entry = Items::Get(itemId);
            auto*       obj = RE::TESForm::LookupByID<RE::TESBoundObject>(itemId);
            if (!entry || !obj || !obj->IsArmor() || !ItemCore::Wearable(entry->kind)) return done(false, "notArmor");
            // A shield goes in the left hand and puts down what it held (a two-handed weapon, a spell, a torch), which
            // the session does not record, so the wardrobe offers outfit pieces only.
            if (entry->kind == ItemCore::Kind::kShield) return done(false, "handItem");
            bool lent = false;
            if (TryOnCore::Lends(CountOf(actor, obj))) {
                actor->AddObjectToContainer(obj, nullptr, 1, nullptr);
                g_session->lent.Lent(itemId);
                lent = true;
            }
            RE::ActorEquipManager::GetSingleton()->EquipObject(actor, obj, nullptr, 1, nullptr, false, false, false, true);
            Views::RebuildActor3D(actorId);
            SKSE::log::info("tryon: {:08X} wears {:08X}{}", actorId, itemId, lent ? " (lent)" : "");
            done(true, { { "lent", lent }, { "worn", WornCards(actor) } });
        });
    }

    void Revert(std::uint32_t actorId, Done done)
    {
        Lane::Run([actorId, done = std::move(done)]() {
            if (!g_session || g_session->actor != actorId) {
                auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorId);
                return done(true, { { "taken", 0 }, { "worn", actor ? WornCards(actor) : nlohmann::json::array() } });
            }
            const auto taken = RevertNow(*g_session);
            g_session.reset();
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorId);
            done(true, { { "taken", taken }, { "worn", actor ? WornCards(actor) : nlohmann::json::array() } });
        });
    }

    void RevertAll()
    {
        Lane::Run([]() {
            if (!g_session) return;
            RevertNow(*g_session);
            g_session.reset();
        });
    }

    void Forget()
    {
        Lane::Run([]() { g_session.reset(); });
    }
}
