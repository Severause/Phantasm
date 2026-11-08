// Phantasm Showcase - Runtime: see Runtime.h.

#include "PCH.h"

#include "Runtime.h"

#include "Anims.h"
#include "ImGuiPanel.h"
#include "Items.h"
#include "Lane.h"
#include "Settings.h"
#include "TryOn.h"
#include "Ui.h"
#include "Views.h"

namespace Showcase::Runtime
{
    void OnDataLoaded()
    {
        Settings::Load();
        Anims::Prewarm();
        Items::Build();
        Views::Initialize();
        Ui::Initialize();
        ImGuiPanel::Initialize();
    }

    void OnPreLoad()
    {
        ++Lane::g_generation;
        // PrismaUI does not close a page for a load the way Magelight does: close it here, ending its views.
        if (Ui::IsPrisma()) Ui::Close();
        // The game load ends every Phantasm view; a try-on session belongs to the session that is ending.
        TryOn::Forget();
        ImGuiPanel::OnPreLoad();
    }

    void OnPostLoad() {}
}
