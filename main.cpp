// Andromeda - Geometry Dash input buffer (Geode mod)
//
// On every click press, simulate the next N frames. If the click would kill
// the player, try delaying it by 1, 2, 3... frames until a delay survives,
// then fire the click at that frame instead. Releases are delayed by the same
// amount so hold length stays consistent.

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <map>
#include <vector>

using namespace geode::prelude;

namespace andromeda {
    struct Pending {
        int framesLeft;
        bool down;
        int button;
        bool p1;
    };

    inline bool simulating = false;                 // true while we run lookahead
    inline bool diedInSim = false;                  // set by destroyPlayer hook
    inline std::vector<Pending> queue;              // delayed inputs waiting to fire
    inline std::map<std::pair<int, bool>, int> heldDelay; // delay applied to current hold

    inline int lookahead() { return (int)Mod::get()->getSettingValue<int64_t>("lookahead"); }
    inline int maxDelay()  { return (int)Mod::get()->getSettingValue<int64_t>("max-delay"); }

    inline void reset() {
        queue.clear();
        heldDelay.clear();
        simulating = false;
        diedInSim = false;
    }
}

class $modify(AndromedaBGL, GJBaseGameLayer) {
    // Step the game forward `H` frames, pressing at frame `delay`.
    // Returns true if the player survives the whole window.
    bool simulateSurvives(int delay, int H, int button, bool p1) {
        andromeda::diedInSim = false;
        for (int i = 0; i < H; i++) {
            if (i == delay) {
                GJBaseGameLayer::handleButton(true, button, p1);
            }
            GJBaseGameLayer::update(1.f / 240.f);
            if (andromeda::diedInSim) return false;
        }
        return true;
    }

    // Returns 0 = let click through now, >0 = delay that many frames.
    int findSafeDelay(int button, bool p1) {
        auto pl = PlayLayer::get();
        if (!pl) return 0;

        int H = andromeda::lookahead();
        int K = andromeda::maxDelay();

        CheckpointObject* snapshot = pl->createCheckpoint();
        if (!snapshot) return 0;
        snapshot->retain();

        andromeda::simulating = true;
        int result = -1;
        for (int d = 0; d <= K; d++) {
            bool ok = simulateSurvives(d, H, button, p1);
            pl->loadFromCheckpoint(snapshot);   // roll back
            if (ok) { result = d; break; }
        }
        andromeda::simulating = false;
        snapshot->release();

        // -1 means no delay works (doomed anyway), so just pass the click through.
        return result > 0 ? result : 0;
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        auto pl = PlayLayer::get();
        bool inPlay = pl && static_cast<GJBaseGameLayer*>(pl) == this;

        if (andromeda::simulating || !inPlay || pl->m_player1->m_isDead) {
            return GJBaseGameLayer::handleButton(down, button, isPlayer1);
        }

        auto key = std::make_pair(button, isPlayer1);

        if (down) {
            int delay = findSafeDelay(button, isPlayer1);
            andromeda::heldDelay[key] = delay;
            if (delay > 0) {
                andromeda::queue.push_back({delay, true, button, isPlayer1});
                return; // swallow; fires later
            }
        } else {
            int delay = andromeda::heldDelay[key];
            if (delay > 0) {
                andromeda::queue.push_back({delay, false, button, isPlayer1});
                return;
            }
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    // Runs every physics step: count down delayed inputs and fire them.
    void processCommands(float dt) {
        GJBaseGameLayer::processCommands(dt);
        if (andromeda::simulating) return;

        for (auto& p : andromeda::queue) {
            if (--p.framesLeft <= 0) {
                GJBaseGameLayer::handleButton(p.down, p.button, p.p1);
            }
        }
        std::erase_if(andromeda::queue, [](auto& p) { return p.framesLeft <= 0; });
    }
};

class $modify(AndromedaPL, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        andromeda::reset();
        // Test mode means no stats/completions get submitted. Andromeda is a cheat.
        m_isTestMode = true;
        return true;
    }

    void resetLevel() {
        andromeda::queue.clear();
        andromeda::heldDelay.clear();
        PlayLayer::resetLevel();
    }

    // During lookahead, record the death instead of actually dying.
    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (andromeda::simulating) {
            andromeda::diedInSim = true;
            return;
        }
        PlayLayer::destroyPlayer(player, obj);
    }
};
