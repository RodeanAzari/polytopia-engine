// Our own harness (ADR-002): drives one Tribes game through GameState's own turn methods and logs
// everything a differential test needs. It sits in package core.game only to reach GameState's
// package-private turn methods; no Tribes code is copied. The turn loop follows rule facts 04 §2.
//
// Log lines (one record per line, space-separated; our schema, not Tribes' savegame format):
//   GAME <levelSeed> <gameSeed> <mode> <fullObs 0|1>
//   LEVEL <n>, then n lines of the generated level
//   SEAT <tribeId> <tribeType> <playerIndex>
//   SETUP <scenario>                               state edits applied after map load (see applyScenario)
//   RAND <caller> <method> <bound|-> <value>       every draw on the game RNG, in order
//   TURN <tick> <tribeId>                          before turn start
//   ACTIONS <count>, then one "A <action>" line each (sorted)   legal actions when the agent is asked
//   ACT <action>                                   the action executed next
//     <action> is Tribes' toString, except RESOURCE_GATHERED gets " at <x> : <y>" appended (its
//     toString omits the tile, so two gatherable tiles of one resource would be indistinguishable)
//   STATE ... END                                  full native state (see dumpState)
//   OBS ... END                                    the copy the agent is given (gs.copy(id)), same schema,
//                                                  logged before each decision; other tribes' city and
//                                                  extra-unit lists sorted (the copy fills them in HashMap order)
//   GAMEEND <tick>, then RESULT <tribeId> <result> <score> per tribe in id order
//   Probes (--probe=N, every N-th decision, after its OBS): Tribes' agent-side forward model (rule facts 04
//   §9) on a copy of the observation, as search agents use it:
//   PROBE <steps>                                  start, from the OBS just logged
//   PRAND <caller> <method> <bound|-> <value>      draws on the probe copy's RNG (seeded here; Tribes' own is not)
//   PACT <action>, then PSTATE ... END (OBS schema) and PACTIONS <count> + "A" lines, per step
//   PROBEEND
package core.game;

import core.Diplomacy;
import core.TechnologyTree;
import core.Types;
import core.actors.Building;
import core.actors.City;
import core.actors.Temple;
import core.actors.Tribe;
import core.actors.units.Battleship;
import core.actors.units.Boat;
import core.actors.units.Ship;
import core.actors.units.Unit;
import core.actions.Action;
import core.actions.cityactions.ResourceGathering;
import core.actions.tribeactions.EndTurn;
import players.Agent;
import utils.ElapsedCpuTimer;

import java.io.PrintStream;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.TreeMap;

public final class OracleDriver {

    /** Game RNG that logs every draw with the first Tribes frame that asked for it. */
    static final class LoggingRandom extends Random {
        private final PrintStream out;
        private final String tag;
        private boolean inside = false;  // a draw method calling another must log once

        LoggingRandom(long seed, PrintStream out) {
            this(seed, out, "RAND");
        }

        LoggingRandom(long seed, String tag, PrintStream out) {
            this(seed, out, tag);
        }

        private LoggingRandom(long seed, PrintStream out, String tag) {
            super(seed);
            this.out = out;
            this.tag = tag;
        }

        private static String caller() {
            // First frame outside this class and java.*; mark a JDK hop (e.g. Collections.shuffle).
            StackTraceElement[] st = Thread.currentThread().getStackTrace();
            String jdk = null;
            for (StackTraceElement e : st) {
                String c = e.getClassName();
                if (c.startsWith("java.lang.Thread") || c.equals(LoggingRandom.class.getName())) continue;
                if (c.startsWith("java.") || c.startsWith("jdk.")) {
                    if (jdk == null) jdk = c.substring(c.lastIndexOf('.') + 1) + "." + e.getMethodName();
                    continue;
                }
                String own = c.substring(c.lastIndexOf('.') + 1) + "." + e.getMethodName();
                return jdk == null ? own : jdk + "<" + own;
            }
            return "?";
        }

        private void log(String method, String bound, Object value) {
            out.println(tag + " " + caller() + " " + method + " " + bound + " " + value);
        }

        @Override public int nextInt(int bound) {
            boolean outer = !inside; inside = true;
            int v = super.nextInt(bound);
            if (outer) { inside = false; log("nextInt", String.valueOf(bound), v); }
            return v;
        }
        @Override public int nextInt() {
            boolean outer = !inside; inside = true;
            int v = super.nextInt();
            if (outer) { inside = false; log("nextInt", "-", v); }
            return v;
        }
        @Override public double nextDouble() {
            boolean outer = !inside; inside = true;
            double v = super.nextDouble();
            if (outer) { inside = false; log("nextDouble", "-", v); }
            return v;
        }
        @Override public boolean nextBoolean() {
            boolean outer = !inside; inside = true;
            boolean v = super.nextBoolean();
            if (outer) { inside = false; log("nextBoolean", "-", v); }
            return v;
        }
        @Override public long nextLong() {
            boolean outer = !inside; inside = true;
            long v = super.nextLong();
            if (outer) { inside = false; log("nextLong", "-", v); }
            return v;
        }
        @Override public float nextFloat() {
            boolean outer = !inside; inside = true;
            float v = super.nextFloat();
            if (outer) { inside = false; log("nextFloat", "-", v); }
            return v;
        }
        @Override public synchronized double nextGaussian() {
            boolean outer = !inside; inside = true;
            double v = super.nextGaussian();
            if (outer) { inside = false; log("nextGaussian", "-", v); }
            return v;
        }
    }

    private OracleDriver() {}

    /** Probe every N-th agent decision (0 = never), PROBE_STEPS forward-model steps each (04 §9). */
    public static int probeEvery = 0;
    static final int PROBE_STEPS = 30;
    static final double PROBE_END_TURN = 0.2;  // chance a probe step picks End Turn when it is legal
    private static long decisions = 0;
    private static long probeSeed = 0;

    /**
     * Runs one game. agentsByRequest[i] plays tribes[i] (seats bind by tribe type, rule facts 05 §1.2).
     * @param dumpStates false = skip STATE/ACTIONS records (faster; for the loop-equivalence check).
     * @param initOnly   stop after the initial state (map-load difftests).
     * @param scenario   "" or "mindbender" (see applyScenario).
     */
    public static void run(long levelSeed, long gameSeed, Types.GAME_MODE mode, Types.TRIBE[] tribes,
                           Agent[] agentsByRequest, boolean dumpStates, boolean initOnly, String scenario,
                           PrintStream out)
            throws Exception {
        LoggingRandom rnd = new LoggingRandom(gameSeed, out);
        probeSeed = gameSeed;
        decisions = 0;
        out.println("GAME " + levelSeed + " " + gameSeed + " " + mode + " " + (core.Constants.PLAY_WITH_FULL_OBS ? 1 : 0));

        // Level: generated exactly as GameState.init does, logged, then loaded through the same path.
        core.levelgen.LevelGenerator gen = new core.levelgen.LevelGenerator(levelSeed);
        gen.init(core.TribesConfig.DEFAULT_MAP_SIZE[tribes.length - 1], 3, 4, 0.5, tribes);
        gen.generate();
        String[] lines = gen.gelLevelLines();
        out.println("LEVEL " + lines.length);
        for (String l : lines) out.println(l);

        GameState gs = new GameState(rnd, mode);
        gs.init(levelSeed, tribes);  // regenerates the same level (own seeded generator) and loads it

        Tribe[] tt = gs.getTribes();
        int n = tt.length;
        Agent[] players = new Agent[n];
        ArrayList<Integer> allIds = new ArrayList<>();
        for (int i = 0; i < n; ++i) allIds.add(i);
        for (int id = 0; id < n; ++id) {
            int req = -1;
            for (int i = 0; i < tribes.length; ++i) if (tribes[i] == tt[id].getType()) req = i;
            players[id] = agentsByRequest[req];
            players[id].setPlayerIDs(id, allIds);
            out.println("SEAT " + id + " " + tt[id].getType() + " " + req);
        }
        if (!scenario.isEmpty()) {
            applyScenario(gs, scenario);
            out.println("SETUP " + scenario);
        }
        if (dumpStates || initOnly) dumpState(gs, out);
        if (initOnly) return;

        // Turn loop (rule facts 04 §1–§2; Game.tick/processTurn without GUI, stats or time limit).
        gameLoop:
        while (!gs.gameOver()) {
            for (int id = 0; id < n; ++id) {
                Tribe tribe = tt[id];
                if (tribe.getWinner() != Types.RESULT.INCOMPLETE) continue;
                out.println("TURN " + gs.getTick() + " " + id);
                playTurn(gs, id, tribe, players[id], dumpStates, out);
                if (gs.gameOver()) break gameLoop;
            }
            gs.incTick();
        }

        out.println("GAMEEND " + gs.getTick());
        for (int id = 0; id < n; ++id)
            out.println("RESULT " + id + " " + tt[id].getWinner() + " " + tt[id].getScore());
    }

    /**
     * Targeted scenarios for rules that normal games rarely reach. Applied after map load, before the
     * first state dump, so the replay applies the same edit and the first STATE checks it.
     * mindbender: every tribe gets Climbing, Meditation and Philosophy marked researched (no score, no
     * "all researched" re-check), so Mind Benders (Heal others, Convert) can be spawned from turn 0.
     */
    private static void applyScenario(GameState gs, String scenario) {
        if (!scenario.equals("mindbender")) throw new IllegalArgumentException("unknown scenario " + scenario);
        for (Tribe t : gs.getTribes()) {
            t.getTechTree().doResearchInit(Types.TECHNOLOGY.CLIMBING);
            t.getTechTree().doResearchInit(Types.TECHNOLOGY.MEDITATION);
            t.getTechTree().doResearchInit(Types.TECHNOLOGY.PHILOSOPHY);
        }
    }

    private static void playTurn(GameState gs, int id, Tribe tribe, Agent ag, boolean dumpStates,
                                 PrintStream out) {
        gs.initTurn(tribe);
        gs.computePlayerActions(tribe);
        if (dumpStates) dumpState(gs, out);

        ElapsedCpuTimer ect = new ElapsedCpuTimer();
        ect.setMaxTimeMillis(core.Constants.TURN_TIME_MILLIS);
        long remaining = core.Constants.TURN_TIME_MILLIS;
        boolean continueTurn = true;

        while (true) {
            Action action;
            if (continueTurn) {
                if (dumpStates) dumpActions(gs, out);
                ect.setMaxTimeMillis(remaining);
                GameState obs = gs.copy(id);
                if (dumpStates) dumpState(obs, "OBS", id, out);
                if (dumpStates && probeEvery > 0 && decisions++ % probeEvery == 0) probe(obs, id, out);
                action = ag.act(obs, ect);
                remaining = ect.remainingTimeMillis();
                continueTurn = !gs.isTurnEnding() && gs.existAvailableActions(tribe);
            } else {
                action = new EndTurn(gs.getActiveTribeID());
            }
            if (action != null && action.getActionType() == Types.ACTION.END_TURN) {
                out.println("ACT " + describe(action));
                break;
            }
            if (action != null) {
                out.println("ACT " + describe(action));
                gs.next(action);
                gs.computePlayerActions(tribe);
                if (dumpStates) dumpState(gs, out);
            }
            if (gs.gameOver()) break;
        }
        gs.endTurn(tribe);
        if (dumpStates) dumpState(gs, out);
    }

    /**
     * Steps a copy of the observation with GameState.advance, as Tribes' search agents do (copy() of the
     * copy, computeActions = true). Random actions from the copy's legal list, End Turn with probability
     * PROBE_END_TURN when legal. The copy's RNG is replaced by a seeded, logging one (PRAND). The agent's
     * own observation object is not touched.
     */
    private static void probe(GameState obs, int observer, PrintStream out) {
        GameState g = obs.copy();
        try {
            Field f = GameState.class.getDeclaredField("rnd");
            f.setAccessible(true);
            f.set(g, new LoggingRandom(probeSeed * 1_000_003L + decisions, "PRAND", out));
        } catch (ReflectiveOperationException e) {
            throw new IllegalStateException(e);
        }
        Random pick = new Random(probeSeed * 7_919L + decisions);
        out.println("PROBE " + PROBE_STEPS);
        for (int step = 0; step < PROBE_STEPS && !g.isGameOver(); ++step) {
            ArrayList<Action> acts = g.getAllAvailableActions();
            if (acts.isEmpty()) break;
            Action a = null;
            if (pick.nextDouble() < PROBE_END_TURN)
                for (Action c : acts) if (c.getActionType() == Types.ACTION.END_TURN) a = c;
            if (a == null) a = acts.get(pick.nextInt(acts.size()));
            out.println("PACT " + describe(a));
            g.advance(a, true);
            dumpState(g, "PSTATE", observer, out);
            dumpActions(g, "PACTIONS", out);
        }
        out.println("PROBEEND");
    }

    private static String describe(Action a) {
        if (a instanceof ResourceGathering) {
            return a + " at " + ((ResourceGathering) a).getTargetPos();
        }
        return String.valueOf(a);
    }

    private static void dumpActions(GameState gs, PrintStream out) {
        dumpActions(gs, "ACTIONS", out);
    }

    private static void dumpActions(GameState gs, String header, PrintStream out) {
        List<String> acts = new ArrayList<>();
        for (Action a : gs.getAllAvailableActions()) acts.add(describe(a));
        java.util.Collections.sort(acts);
        out.println(header + " " + acts.size());
        for (String a : acts) out.println("A " + a);
    }

    // ---- state dump (our schema) ----

    private static Object field(Object o, String name) {
        try {
            Class<?> c = o.getClass();
            while (c != null) {
                try {
                    Field f = c.getDeclaredField(name);
                    f.setAccessible(true);
                    return f.get(o);
                } catch (NoSuchFieldException e) {
                    c = c.getSuperclass();
                }
            }
            throw new IllegalStateException("no field " + name + " in " + o.getClass());
        } catch (IllegalAccessException e) {
            throw new IllegalStateException(e);
        }
    }

    private static String bits(boolean[][] g) {
        StringBuilder sb = new StringBuilder();
        for (boolean[] row : g) for (boolean b : row) sb.append(b ? '1' : '0');
        return sb.toString();
    }

    static void dumpState(GameState gs, PrintStream out) {
        dumpState(gs, "STATE", -1, out);
    }

    private static List<Integer> sortedIf(boolean sort, List<Integer> ids) {
        if (!sort) return ids;
        List<Integer> s = new ArrayList<>(ids);
        java.util.Collections.sort(s);
        return s;
    }

    // observer >= 0: an agent's copy; tribes other than the observer get sorted city/extra-unit lists.
    static void dumpState(GameState gs, String header, int observer, PrintStream out) {
        Board b = gs.getBoard();
        int size = b.getSize();
        Tribe[] tt = gs.getTribes();
        StringBuilder s = new StringBuilder();
        s.append(header).append(" tick=").append(gs.getTick())
         .append(" active=").append(b.getActiveTribeID())
         .append(" over=").append(gs.isGameOver() ? 1 : 0)
         .append(" mustEnd=").append(gs.isTurnEnding() ? 1 : 0)
         .append(" levelingUp=").append(gs.isLevelingUp() ? 1 : 0)
         .append(" nextActorId=").append(b.getActorIDcounter() + 1)
         .append(" canEnd=");
        for (int i = 0; i < tt.length; ++i) s.append(gs.canEndTurn(i) ? '1' : '0');
        s.append('\n');

        String[] names = {"terrain", "resource", "building", "unit", "cityTile", "network"};
        for (int k = 0; k < names.length; ++k) {
            s.append(names[k]);
            for (int x = 0; x < size; ++x)
                for (int y = 0; y < size; ++y) {
                    s.append(' ');
                    switch (k) {
                        case 0: { Types.TERRAIN tr = b.getTerrainAt(x, y);  // FOG's map char is ' '
                                  s.append(tr == Types.TERRAIN.FOG ? '#' : tr.getMapChar()); break; }
                        case 1: { Types.RESOURCE r = b.getResourceAt(x, y); s.append(r == null ? '-' : r.getMapChar()); break; }
                        case 2: { Types.BUILDING bd = b.getBuildingAt(x, y); s.append(bd == null ? "-" : bd.name()); break; }
                        case 3: s.append(b.getUnitIDAt(x, y)); break;
                        case 4: s.append(b.getCityIdAt(x, y)); break;
                        default: s.append(b.getNetworkTilesAt(x, y) ? 1 : 0); break;
                    }
                }
            s.append('\n');
        }

        int[][] alleg = ((Diplomacy) field(b, "diplomacy")).getAllegianceStatus();
        for (Tribe t : tt) {
            TechnologyTree tech = t.getTechTree();
            StringBuilder techs = new StringBuilder();
            for (boolean r : tech.getResearched()) techs.append(r ? '1' : '0');
            Map<String, String> mon = new TreeMap<>();
            for (Map.Entry<Types.BUILDING, Types.BUILDING.MONUMENT_STATUS> e : t.getMonuments().entrySet())
                mon.put(e.getKey().name(), e.getValue().name());
            s.append("TRIBE ").append(t.getTribeId()).append(' ').append(t.getType())
             .append(" stars=").append(t.getStars()).append(" score=").append(t.getScore())
             .append(" result=").append(t.getWinner()).append(" capital=").append(t.getCapitalID())
             .append(" cities=").append(sortedIf(observer >= 0 && t.getTribeId() != observer, t.getCitiesID()))
             .append(" extraUnits=").append(sortedIf(observer >= 0 && t.getTribeId() != observer, t.getExtraUnits()))
             .append(" met=").append(t.getTribesMet()).append(" connected=").append(t.getConnectedCities())
             .append(" tech=").append(techs).append(" allTech=").append(tech.isEverythingResearched() ? 1 : 0)
             .append(" kills=").append(t.getnKills()).append(" pacifist=").append(t.getnPacifistCount())
             .append(" starsSentTurn=").append(t.getStarsSent()).append(" declaredWarTurn=").append(t.getHasDeclaredWar() ? 1 : 0)
             .append(" wars=").append(t.getnWarsDeclared()).append(" starsSentTotal=").append(t.getnStarsSent())
             .append(" monuments=").append(mon).append(" allegiance=").append(java.util.Arrays.toString(alleg[t.getTribeId()]))
             .append(" vis=").append(bits(t.getObsGrid())).append('\n');
        }

        for (int id = 1; id <= b.getActorIDcounter(); ++id) {
            Object a = b.getActor(id);
            if (a instanceof City) {
                City c = (City) a;
                s.append("CITY ").append(id).append(" tribe=").append(c.getTribeId())
                 .append(" pos=").append(c.getPosition().x).append(',').append(c.getPosition().y)
                 .append(" level=").append(c.getLevel()).append(" pop=").append(c.getPopulation())
                 .append(" need=").append(c.getPopulation_need()).append(" prodBonus=").append(field(c, "production"))
                 .append(" capital=").append(c.isCapital() ? 1 : 0).append(" walls=").append(c.hasWalls() ? 1 : 0)
                 .append(" bound=").append(c.getBound()).append(" worth=").append(c.getPointsWorth())
                 .append(" units=").append(c.getUnitsID()).append(" buildings=[");
                boolean first = true;
                for (Building bd : c.getBuildings()) {
                    if (!first) s.append(' ');
                    first = false;
                    s.append(bd.position.x).append(',').append(bd.position.y).append(':').append(bd.type.name());
                    if (bd instanceof Temple)
                        s.append(':').append(((Temple) bd).getLevel()).append(':').append(((Temple) bd).getTurnsToScore());
                }
                s.append("]\n");
            } else if (a instanceof Unit) {
                Unit u = (Unit) a;
                s.append("UNIT ").append(id).append(' ').append(u.getType()).append(" tribe=").append(u.getTribeId())
                 .append(" city=").append(u.getCityId())
                 .append(" pos=").append(u.getPosition().x).append(',').append(u.getPosition().y)
                 .append(" hp=").append(u.getCurrentHP()).append('/').append(u.getMaxHP())
                 .append(" kills=").append(u.getKills()).append(" vet=").append(u.isVeteran() ? 1 : 0)
                 .append(" status=").append(u.getStatus());
                Types.UNIT base = u instanceof Boat ? ((Boat) u).getBaseLandUnit()
                        : u instanceof Ship ? ((Ship) u).getBaseLandUnit()
                        : u instanceof Battleship ? ((Battleship) u).getBaseLandUnit() : null;
                if (base != null) s.append(" carries=").append(base);
                s.append('\n');
            }
        }
        s.append("END");
        out.println(s);
    }
}
