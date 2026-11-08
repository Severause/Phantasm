// Phantasm - native self-tests: ViewRegistryCore (the API's clients, views and the drawing rule).

#include "ViewRegistryCore.h"
#include "TestHarness.h"

#include <chrono>
#include <cmath>
#include <limits>

namespace Phantasm::Tests
{
    using namespace Phantasm::ViewRegistryCore;

    void RunViewRegistryCoreTests()
    {
        // Names
        Check(ValidName("sa") && ValidName("outfits-ui_2"), "lower-case letters, digits, - and _ make a name");
        Check(!ValidName("") && !ValidName("SA") && !ValidName("a.b") && !ValidName("a b"),
              "an empty name, capitals, dots and spaces are refused");
        Check(ValidName(std::string(24, 'a')) && !ValidName(std::string(25, 'a')), "a name holds at most 24 characters");
        Check(ImageName("sa", "mirror") == "phantasm.sa.mirror", "the image name joins client and slot");

        // Clients
        Registry r;
        ClientId a = 0, b = 0, dup = 0;
        Check(r.RegisterClient("sa", a) == Result::Ok && a != 0, "a client registers");
        Check(r.RegisterClient("sa", dup) == Result::NameTaken && dup == 0, "a held name is refused");
        Check(r.RegisterClient("Bad", dup) == Result::BadArgument, "a bad name is refused");
        Check(r.RegisterClient("outfits", b) == Result::Ok && b != a, "a second client gets its own id");

        // Views and the drawing rule
        ViewId v1 = 0, v2 = 0, v1again = 0, bad = 0;
        bool   created = false;
        Check(r.Open(a, "mirror", 0x14, 512, 768, Framing::Figure, v1, created) == Result::Ok && created,
              "a view opens in a slot");
        Check(r.Drawer() == v1, "the only view draws");
        Check(r.Open(b, "builder", 0x1A68A, 400, 600, Framing::Figure, v2, created) == Result::Ok && created && v2 != v1,
              "another client's view opens");
        Check(r.Drawer() == v2, "the newest open view draws");
        Check(r.Open(a, "mirror", 0x1A68B, 300, 300, Framing::Figure, v1again, created) == Result::Ok && !created &&
                  v1again == v1,
              "opening a held slot re-targets the same view");
        Check(r.Find(v1)->actor == 0x1A68B && r.Find(v1)->width == 300, "the re-target takes the new actor and size");
        Check(r.Drawer() == v1, "a re-targeted view becomes the newest and draws");
        Check(!r.Find(v1)->item, "OpenView opens an actor view");
        {
            Registry ri;
            ClientId c = 0;
            ViewId   iv = 0, av = 0, iv2 = 0;
            ri.RegisterClient("sa", c);
            Check(ri.Open(c, "item", 0x12EB7, 256, 256, Framing::Figure, iv, created, true) == Result::Ok && created &&
                      ri.Find(iv)->item && ri.Find(iv)->actor == 0x12EB7,
                  "an item view opens with its base form");
            Check(ri.Open(c, "mirror", 0x14, 512, 768, Framing::Figure, av, created) == Result::Ok && ri.Drawer() == av,
                  "an actor view opened after an item view is the newest: the two kinds count alike");
            Check(ri.Open(c, "mirror", 0x13BB9, 200, 200, Framing::Figure, iv2, created, true) == Result::Ok &&
                      iv2 == av && ri.Find(av)->item && ri.Drawer() == av,
                  "re-targeting a slot to an item turns its view into an item view");
        }
        {
            Registry rd;
            ClientId c = 0;
            rd.RegisterClient("many", c);
            std::vector<ViewId> ids(6, 0);
            for (std::size_t i = 0; i < ids.size(); ++i) {
                rd.Open(c, "s" + std::to_string(i), 0x14, 10, 10, Framing::Figure, ids[i], created);
            }
            const auto four = rd.Drawers(kMaxDrawing);
            Check(four == std::vector<ViewId>{ ids[5], ids[4], ids[3], ids[2] },
                  "the newest views draw, newest first, up to the cap");
            Check(rd.Drawers(kMaxDrawingVR) == std::vector<ViewId>{ ids[5], ids[4] }, "VR's cap is smaller");
            View gone;
            rd.Close(ids[4], gone);
            Check(rd.Drawers(kMaxDrawing) == std::vector<ViewId>{ ids[5], ids[3], ids[2], ids[1] },
                  "when a drawer closes, the newest view not drawing takes its place");
            ViewId again = 0;
            rd.Open(c, "s0", 0x15, 10, 10, Framing::Figure, again, created);
            Check(again == ids[0] && rd.Drawers(kMaxDrawing).front() == ids[0], "a re-targeted view draws as the newest");
            Check(rd.Drawers(0).empty() && Registry{}.Drawers(kMaxDrawing).empty(), "no cap or no views, no drawers");
        }
        View closed;
        Check(r.Close(v1, closed) == Result::Ok && closed.image == "phantasm.sa.mirror", "a view closes");
        Check(r.Drawer() == v2, "when the drawer closes the view opened before it draws again");
        Check(r.Close(v1, closed) == Result::UnknownView, "a closed view is unknown");

        // Arguments
        Check(r.Open(a, "face", 0x14, 0, 10, Framing::Figure, bad, created) == Result::BadArgument, "a zero size is refused");
        Check(r.Open(a, "face", 0, 10, 10, Framing::Figure, bad, created) == Result::BadArgument, "actor 0 is refused");
        Check(r.Open(a, "Face", 0x14, 10, 10, Framing::Figure, bad, created) == Result::BadArgument, "a bad slot is refused");
        Check(r.Open(a, "face", 0x14, 10, 10, Framing::Portrait, bad, created) == Result::Unsupported,
              "the portrait framing is not served yet");
        Check(r.Open(99, "face", 0x14, 10, 10, Framing::Figure, bad, created) == Result::UnknownClient,
              "an unknown client cannot open a view");

        // The per-client cap
        Registry cap;
        ClientId c = 0;
        cap.RegisterClient("many", c);
        ViewId   last = 0;
        Result   res  = Result::Ok;
        for (int i = 0; i <= static_cast<int>(kMaxViewsPerClient) && res == Result::Ok; ++i) {
            res = cap.Open(c, "s" + std::to_string(i), 0x14, 10, 10, Framing::Figure, last, created);
        }
        Check(res == Result::TooMany && cap.Views().size() == kMaxViewsPerClient, "a client holds at most 16 views");

        // Release
        std::vector<View> ended;
        Check(r.ReleaseClient(b, ended) == Result::Ok && ended.size() == 1 && ended[0].id == v2,
              "releasing a client ends its views");
        Check(r.Drawer() == 0, "no view left, none draws");
        ClientId again = 0;
        Check(r.RegisterClient("outfits", again) == Result::Ok && again != b, "a released name can be registered again");
        Check(r.ReleaseClient(b, ended) == Result::UnknownClient, "a released client is unknown");

        // Hints
        View h;
        SetHint(h, "Pose", "captured");
        Check(Hint(h, "pose") == "captured", "hint keys are case-insensitive");
        Check(LowerKey("Idles") == "idles", "a query key folds to lower case");
        SetHint(h, "pose", "");
        Check(Hint(h, "pose").empty() && h.hints.empty(), "an empty value removes a hint");
        SetHint(h, "idle", std::string(3000, 'x'));
        Check(Hint(h, "idle").size() == kMaxHintLength, "a hint value is cut to kMaxHintLength bytes");

        // The idle hint's sentinel
        Check(IdleHintValue("Still") == "still" && IdleHintValue("STILL") == "still" && IdleHintValue("still") == "still",
              "the still idle is matched in any case and handed over as the renderer's own spelling");
        Check(IdleHintValue("IdleWarmHands") == "IdleWarmHands" && IdleHintValue("").empty(),
              "a clip name and the empty value pass as given");
        Check(IdleHintValue("stillness") == "stillness", "only the whole word is the sentinel");

        // The scene hint
        {
            const auto cast = SceneCastHint("000AAF99|file:actors\\a.hkx|10, -5,0,90 ; 0x14 | Idle01", 0x1234);
            Check(cast.size() == 2 && cast[0].actor == 0xAAF99 && cast[0].idle == "file:actors\\a.hkx" && cast[0].x == 10.0f &&
                      cast[0].y == -5.0f && cast[0].z == 0.0f && cast[0].r == 90.0f,
                  "scene: an actor, its idle and its offset");
            Check(cast[1].actor == 0x14 && cast[1].idle == "Idle01" && cast[1].x == 0.0f && cast[1].r == 0.0f,
                  "scene: a 0x FormID, a clip name, and no offset at the origin");
            Check(SceneCastHint("AAF99;BBB|;CCC|Still|1,2,3,4", 0x14).empty(),
                  "scene: an entry with no clip (no idle, an empty one or still) is left out");
            Check(SceneCastHint("", 0x14).empty() && SceneCastHint(";;", 0x14).empty(), "scene: nothing is no cast");
            Check(SceneCastHint("14|x;AAF99|y", 0x14).size() == 1 && SceneCastHint("14|x;AAF99|y", 0x14)[0].actor == 0xAAF99,
                  "scene: the view's own actor is left out");
            Check(SceneCastHint("AAF99|a;aaf99|b", 0x14).size() == 1, "scene: a repeat is left out");
            Check(SceneCastHint("0|a;zz|b;123456789|c", 0x14).empty(), "scene: no FormID, a non-hex one or one too long");
            Check(SceneCastHint("1|a;2|b;3|c;4|d;5|e", 0x14).size() == kMaxSceneCast, "scene: at most kMaxSceneCast others");
            const auto offsets = SceneCastHint("AAF99|a|1,2,3;BBB|b|1,2,x,4;CCC|c|1,2,3,nan", 0x14);
            Check(offsets.size() == 3 && offsets[0].x == 0.0f && offsets[1].y == 0.0f && offsets[2].x == 0.0f,
                  "scene: an offset of three numbers, a word or a NaN leaves the entry at the origin");

            const auto approx = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };
            SceneCastEntry e;
            e.x = 10.0f;   // forward
            e.y = 4.0f;    // right
            e.z = 2.0f;
            e.r = 180.0f;
            const auto north = ScenePlace(100.0f, 200.0f, 50.0f, 0.0f, e);
            Check(approx(north.x, 104.0f) && approx(north.y, 210.0f) && approx(north.z, 52.0f) && approx(north.angle, 3.14159265f),
                  "scene place: facing north, forward is +y and right +x, and r turns the heading");
            const auto east = ScenePlace(100.0f, 200.0f, 50.0f, 3.14159265f / 2.0f, e);
            Check(approx(east.x, 110.0f) && approx(east.y, 196.0f), "scene place: facing east, forward is +x and right -y");
        }

        // The props hint
        {
            Check(PropModelPath("AnimObjects\\AnimObjectLute.nif") == "AnimObjects\\AnimObjectLute.nif",
                  "props: a model path under meshes passes as given");
            Check(PropModelPath(" Meshes/Furniture/x.NIF ") == "Furniture\\x.NIF",
                  "props: meshes\\ is dropped, slashes turned, spaces trimmed, the extension in any case");
            Check(PropModelPath("x.hkx").empty() && PropModelPath("").empty() && PropModelPath("meshes\\").empty(),
                  "props: no .nif, nothing, or meshes alone is no model");
            Check(PropModelPath("..\\x.nif").empty() && PropModelPath("a\\..\\..\\x.nif").empty() &&
                      PropModelPath("C:\\x.nif").empty() && PropModelPath("\\x.nif").empty(),
                  "props: a path that could leave Data is refused");

            const auto props = ScenePropsHint("14|AnimObjects\\a.nif ; 0xAAF99|b.nif|AnimObjectB; place|c.nif|10,-5,0,90");
            Check(props.size() == 3, "props: three entries");
            Check(props[0].actor == 0x14 && props[0].model == "AnimObjects\\a.nif" && props[0].node.empty(),
                  "props: on an actor, at the node its model names");
            Check(props[1].actor == 0xAAF99 && props[1].model == "b.nif" && props[1].node == "AnimObjectB",
                  "props: on an actor at a named node");
            Check(props[2].actor == 0 && props[2].model == "c.nif" && props[2].x == 10.0f && props[2].y == -5.0f &&
                      props[2].r == 90.0f,
                  "props: standing at a place");
            Check(ScenePropsHint("PLACE|c.nif")[0].x == 0.0f && ScenePropsHint("place|c.nif|1,2,x,4")[0].y == 0.0f,
                  "props: a place with no offset, or a bad one, stands at the view's actor's");
            Check(ScenePropsHint("0|a.nif;zz|a.nif;14;14|;14|x.hkx;place").empty(),
                  "props: no actor, no model, or a model that is not one is left out");
            Check(ScenePropsHint("14|a.nif;14|A.nif;14|a.nif").size() == 2, "props: an exact repeat is left out");
            std::string many;
            for (int i = 0; i < 20; ++i) many += "14|p" + std::to_string(i) + ".nif;";
            Check(ScenePropsHint(many).size() == kMaxSceneProps, "props: at most kMaxSceneProps");
        }

        // Camera input
        const float nan = std::nanf("");
        const float inf = std::numeric_limits<float>::infinity();
        Check(CameraInputOk({ 12.5f, -3.0f }) && CameraInputOk({ -120.0f, 0.0f, 1.0f }), "finite camera input passes");
        Check(!CameraInputOk({ nan, 0.0f }) && !CameraInputOk({ 0.0f, inf }) && !CameraInputOk({ 1.0f, 0.5f, -inf }),
              "a NaN or an infinity anywhere is refused");

        // The smallest drawn side
        Check(DrawSide(1) == 4 && DrawSide(3) == 4 && DrawSide(4) == 4 && DrawSide(300) == 300,
              "a side under 4 px is drawn at 4, a larger one as asked");
        View tiny;
        bool tinyCreated = false;
        Registry t;
        ClientId tc = 0;
        t.RegisterClient("tiny", tc);
        Check(t.Open(tc, "dot", 0x14, 1, 3, Framing::Figure, tiny.id, tinyCreated) == Result::Ok,
              "a 1x3 view stays accepted (only zero is refused; DrawSide sizes it)");

        // The GPU refusal window
        using Clock = GpuHold::Clock;
        using std::chrono::milliseconds;
        const Clock::time_point t0 = Clock::time_point{} + std::chrono::hours(1);
        GpuHold hold;
        Check(hold.Left(t0) == milliseconds(0), "with no refusal a start is tried");
        hold.Note(t0, milliseconds(10000));
        Check(hold.Left(t0) == milliseconds(10000), "after a refusal starts wait the whole window");
        Check(hold.Left(t0 + milliseconds(9999)) == milliseconds(1), "one millisecond before the end one is left");
        Check(hold.Left(t0 + std::chrono::microseconds(9999500)) == milliseconds(1),
              "a part millisecond left rounds up, so a wait reaches the end");
        Check(hold.Left(t0 + milliseconds(10000)) == milliseconds(0), "at the end of the window a start is tried");
        hold.Note(t0 + milliseconds(2000), milliseconds(10000));
        Check(hold.Left(t0 + milliseconds(11000)) == milliseconds(1000), "a later refusal extends the window");
        hold.Note(t0 + milliseconds(3000), milliseconds(1000));
        Check(hold.Left(t0 + milliseconds(11000)) == milliseconds(1000), "a refusal never shortens the window");
    }
}
