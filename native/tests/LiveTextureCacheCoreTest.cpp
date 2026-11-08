// Phantasm - native self-tests: LiveTextureCacheCore (the live views' path-texture working set).
//
// Author: Severause

#include "LiveTextureCacheCore.h"
#include "TestHarness.h"

#include <string>

namespace Phantasm::Tests
{
    void RunLiveTextureCacheCoreTests()
    {
        using Cache = Phantasm::LiveTextureCacheCore::WorkingSetCache<int>;

        // 1. A miss is null; an insert is found with its value
        {
            Cache c;
            c.BeginCollect();
            Check(c.Find("a.dds") == nullptr, "texcache: an empty cache misses");
            c.Insert("a.dds", 7);
            const int* v = c.Find("a.dds");
            Check(v && *v == 7 && c.Size() == 1, "texcache: an inserted path is found with its value");
        }

        // 2. The prune after a collect keeps what it inserted and what it found, and drops the rest
        {
            Cache c;
            c.BeginCollect();
            c.Insert("old.dds", 1);
            c.Insert("kept.dds", 2);
            c.BeginCollect();
            c.Find("kept.dds");
            c.Insert("new.dds", 3);
            const auto dropped = c.PruneToCurrent();
            Check(dropped == 1 && c.Size() == 2, "texcache: the prune drops only the path the collect did not use");
            Check(c.Find("old.dds") == nullptr, "texcache: the unused path is gone");
            Check(c.Find("kept.dds") && c.Find("new.dds"), "texcache: the found and the inserted paths stay");
        }

        // 3. A miss does not stamp anything, and a collect that uses nothing prunes everything
        {
            Cache c;
            c.BeginCollect();
            c.Insert("a.dds", 1);
            c.BeginCollect();
            c.Find("b.dds");
            Check(c.PruneToCurrent() == 1 && c.Size() == 0, "texcache: a collect that finds nothing cached prunes it all");
        }

        // 4. Progressive fill: entries a pass loaded survive the next pass that finds them, whose prune keeps both
        {
            Cache c;
            c.BeginCollect();
            c.Insert("t1.dds", 1);
            c.Insert("t2.dds", 2);
            c.BeginCollect();
            c.Find("t1.dds");
            c.Find("t2.dds");
            c.Insert("t3.dds", 3);
            Check(c.PruneToCurrent() == 0 && c.Size() == 3, "texcache: a pass that finds the earlier loads keeps them");
        }

        // 5. Re-inserting a path replaces its value and stamps it
        {
            Cache c;
            c.BeginCollect();
            c.Insert("a.dds", 1);
            c.BeginCollect();
            c.Insert("a.dds", 5);
            Check(c.PruneToCurrent() == 0, "texcache: a re-inserted path belongs to the current collect");
            const int* v = c.Find("a.dds");
            Check(v && *v == 5 && c.Size() == 1, "texcache: a re-insert replaces the value");
        }

        // 6. Clear drops everything and reports the count
        {
            Cache c;
            c.BeginCollect();
            c.Insert("a.dds", 1);
            c.Insert("b.dds", 2);
            Check(c.Clear() == 2 && c.Size() == 0 && c.Find("a.dds") == nullptr, "texcache: Clear empties the cache");
        }
    }
}
