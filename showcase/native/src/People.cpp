// Phantasm Showcase - People: see People.h.

#include "PCH.h"

#include "People.h"

#include "PeopleCore.h"

namespace Showcase::People
{
    namespace
    {
        struct Found
        {
            RE::Actor*         actor = nullptr;
            PeopleCore::Person person;
        };

        nlohmann::json Row(const Found& f)
        {
            const auto* base = f.actor->GetActorBase();
            return { { "formId", Hex(f.actor->GetFormID()) },
                     { "name", f.person.name },
                     { "group", std::string(PeopleCore::Word(f.person.group)) },
                     { "female", base && base->GetSex() == RE::SEX::kFemale },
                     { "child", f.actor->IsChild() },
                     { "distance", static_cast<int>(f.person.distance) } };
        }
    }

    std::string Hex(std::uint32_t id) { return std::format("{:08X}", id); }

    nlohmann::json List()
    {
        auto out = nlohmann::json::array();
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* lists = RE::ProcessLists::GetSingleton();
        if (!player) return out;
        std::vector<Found> found;
        {
            const char* n = player->GetDisplayFullName();
            found.push_back({ player, { PeopleCore::Group::kPlayer, 0.0f, n && *n ? n : "You" } });
        }
        if (lists) {
            for (const auto& handle : lists->highActorHandles) {
                auto  ptr = handle.get();
                auto* actor = ptr.get();
                if (!actor || actor == player || actor->IsDead() || actor->IsDisabled() || !actor->Is3DLoaded()) continue;
                const auto  group = actor->IsPlayerTeammate() ? PeopleCore::Group::kFollower : PeopleCore::Group::kNearby;
                const float distance = actor->GetPosition().GetDistance(player->GetPosition());
                if (!PeopleCore::Listed(group, distance, actor->IsHostileToActor(player))) continue;
                const char* name = actor->GetDisplayFullName();
                if (!name || !*name) continue;
                found.push_back({ actor, { group, distance, name } });
            }
        }
        std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return PeopleCore::Before(a.person, b.person); });
        for (const auto& f : found) out.push_back(Row(f));
        return out;
    }

    RE::Actor* Showable(std::uint32_t id)
    {
        auto* actor = id ? RE::TESForm::LookupByID<RE::Actor>(id) : nullptr;
        if (!actor || actor->IsDead() || actor->IsDisabled() || !actor->Is3DLoaded()) return nullptr;
        return actor;
    }

    std::uint32_t CrosshairActor()
    {
        auto* pick = RE::CrosshairPickData::GetSingleton();
        if (!pick) return 0;
        const auto ref = pick->GetActiveTarget().get();
        auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
        return actor && !actor->IsPlayerRef() && !actor->IsDead() ? actor->GetFormID() : 0;
    }
}
