// Our own harness agent (ADR-002) for the mindbender scenario: steers games into Mind Bender rules that
// stock agents almost never reach (Random picks uniformly among ~1000 actions; Rule Based never spawns
// Mind Benders). Policy only: it picks among the legal actions Tribes generated, so the difftest still
// checks Tribes' rules, not ours.
//   1. any Convert / Heal others available -> uniform among them;
//   2. else a Mind Bender spawn -> take it;
//   3. else, half the time, a Mind Bender move (wander into contact) -> uniform among them;
//   4. else uniform among all actions (as RandomAgent).
// Seeded with the agent seed; it never touches the game RNG.

import core.Types;
import core.actions.Action;
import core.actions.cityactions.Spawn;
import core.actions.unitactions.UnitAction;
import core.actors.units.Unit;
import core.game.GameState;
import players.Agent;
import utils.ElapsedCpuTimer;

import java.util.ArrayList;
import java.util.List;
import java.util.Random;

public class MindBenderAgent extends Agent {
    private final Random rnd;

    public MindBenderAgent(long seed) {
        super(seed);
        rnd = new Random(seed);
    }

    private Action pick(List<Action> from) {
        return from.get(rnd.nextInt(from.size()));
    }

    @Override
    public Action act(GameState gs, ElapsedCpuTimer ect) {
        ArrayList<Action> all = gs.getAllAvailableActions();
        List<Action> mind = new ArrayList<>();
        List<Action> spawns = new ArrayList<>();
        List<Action> moves = new ArrayList<>();
        for (Action a : all) {
            Types.ACTION t = a.getActionType();
            if (t == Types.ACTION.CONVERT || t == Types.ACTION.HEAL_OTHERS) {
                mind.add(a);
            } else if (t == Types.ACTION.SPAWN && ((Spawn) a).getUnitType() == Types.UNIT.MIND_BENDER) {
                spawns.add(a);
            } else if (t == Types.ACTION.MOVE) {
                Unit u = (Unit) gs.getActor(((UnitAction) a).getUnitId());
                if (u.getType() == Types.UNIT.MIND_BENDER) moves.add(a);
            }
        }
        if (!mind.isEmpty()) return pick(mind);
        if (!spawns.isEmpty()) return pick(spawns);
        if (!moves.isEmpty() && rnd.nextBoolean()) return pick(moves);
        return pick(all);
    }

    @Override
    public Agent copy() {
        return new MindBenderAgent(seed);
    }
}
