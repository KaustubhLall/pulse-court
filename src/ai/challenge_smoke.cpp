#include "challenge_smoke.hpp"

#include "pulse_policy.hpp"
#include "pulse_replay.hpp"

#include <array>
#include <iomanip>
#include <sstream>

namespace pulse {

std::pair<std::int8_t, std::int8_t> decode_script_dir(std::int32_t d) {
    auto p = [](std::int8_t x, std::int8_t y) {
        return std::pair<std::int8_t, std::int8_t>(x, y);
    };
    switch (d % 9) {
        case 0: return p(0, 0);
        case 1: return p(0, 1);
        case 2: return p(0, -1);
        case 3: return p(-1, 0);
        case 4: return p(1, 0);
        case 5: return p(-1, 1);
        case 6: return p(1, 1);
        case 7: return p(-1, -1);
        default: return p(1, -1);
    }
}

FrameInput scripted_input(std::uint32_t tick, std::size_t player,
                          Character character) {
    (void)character;
    std::uint32_t t = tick + static_cast<std::uint32_t>(player) * 1000u;
    std::int32_t move = static_cast<std::int32_t>((t / 30u) % 8u) + 1;
    auto [mx, my] = decode_script_dir(move);
    FrameInput in;
    in.move_x = mx;
    in.move_y = my;
    in.aim_x = mx;
    in.aim_y = my;

    if ((t % 120u) == 0u) {
        in.buttons = ButtonStrike;
    } else if ((t % 300u) == 70u) {
        in.buttons = ButtonAbility;
    } else if ((t % 180u) == 35u) {
        in.buttons = ButtonDash;
    }
    return in;
}

bool run_smoke_game(const Policy& policy, const GameSchedule& gs,
                    std::int32_t game_index,
                    const std::filesystem::path& output_dir, GameEvidence& out,
                    std::string& error) {
    std::int32_t human_seat = gs.human_seat;
    std::int32_t bot_seat = gs.bot_seat;

    MatchConfig config;
    config.score_to_win = 1;  // explicit score-to-one for smoke
    Simulation sim(gs.characters, config);
    PolicyController controller(policy, bot_seat, gs.characters[bot_seat],
                                gs.characters[human_seat]);
    controller.reset(sim.state());

    Replay replay;
    replay.characters = gs.characters;
    replay.config = config;

    const std::int32_t kMaxSmokeTicks = 180 * 120;  // regulation
    for (std::int32_t t = 0; t < kMaxSmokeTicks; ++t) {
        std::uint32_t tick = sim.state().tick;
        std::array<FrameInput, 2> in = {};
        in[human_seat] = scripted_input(tick, human_seat,
                                        gs.characters[human_seat]);
        in[bot_seat] = controller.decide(sim.state(), tick, bot_seat);

        StepResult result = sim.step(in);

        RecordedTick rt;
        rt.inputs = in;
        rt.expected_hash = sim.state_hash();
        replay.ticks.push_back(rt);

        if (result.match_finished) break;
    }

    const GameState& state = sim.state();
    std::int8_t winner = -1;
    if (state.score[human_seat] > state.score[bot_seat]) {
        winner = static_cast<std::int8_t>(human_seat);
    } else if (state.score[bot_seat] > state.score[human_seat]) {
        winner = static_cast<std::int8_t>(bot_seat);
    }

    std::ostringstream name;
    name << "game_" << std::setw(3) << std::setfill('0') << game_index
         << ".pulse";
    std::string filename = name.str();
    std::filesystem::path replay_file = output_dir / filename;

    if (!save_replay(replay, replay_file.string(), error)) {
        return false;
    }

    out.game_index = game_index;
    out.characters = gs.characters;
    out.human_seat = static_cast<std::int8_t>(human_seat);
    out.bot_seat = static_cast<std::int8_t>(bot_seat);
    out.winner = winner;
    out.human_score = static_cast<std::int8_t>(state.score[human_seat]);
    out.bot_score = static_cast<std::int8_t>(state.score[bot_seat]);
    out.final_state_hash = std::to_string(sim.state_hash());
    out.replay_path = filename;

    return true;
}

}  // namespace pulse
