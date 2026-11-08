// Our own harness (ADR-002): times Tribes' forward model on uniform-random playouts, for the
// simulator benchmark (`make bench`, docs/buildplan.md §6 Speed). It sits in package core.game only to
// reach GameState's package-private turn methods; no Tribes code is copied.
//
// Workload per game (the same loop as OracleDriver, rule facts 04 §2, minus logging): from the initial
// state of a generated level, every tribe picks uniformly among GameState.getAllAvailableActions() of
// the true state (what RandomAgent does, without the per-decision gs.copy) until the game ends. Only
// the turn loop is timed; level generation and loading are not. The C++ side (tools/bench.cpp) runs
// the same loop on the same levels, which are written to <levelDir>.
//
// Output, one CSV row per timed game on stdout:
//   side,players,mode,levelSeed,ticks,steps,turns,seconds
// steps = agent decisions (actions executed, End turn included); turns = turns played.
//
// Usage: OracleBench <players> <Capitals|Score> <firstLevelSeed> <games> <warmupGames> <levelDir>
package core.game;

import core.Types;
import core.actions.Action;
import core.actors.Tribe;

import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Random;

public final class OracleBench {
    private static final Types.TRIBE[] ALL = {
        Types.TRIBE.XIN_XI, Types.TRIBE.IMPERIUS, Types.TRIBE.BARDUR, Types.TRIBE.OUMAJI,
        Types.TRIBE.KICKOO, Types.TRIBE.HOODRICK, Types.TRIBE.LUXIDOOR, Types.TRIBE.VENGIR,
        Types.TRIBE.ZEBASI, Types.TRIBE.AI_MO, Types.TRIBE.QUETZALI, Types.TRIBE.YADAKK};

    private OracleBench() {}

    public static void main(String[] args) throws Exception {
        if (args.length != 6) {
            System.err.println("usage: OracleBench <players> <Capitals|Score> <firstLevelSeed> <games> <warmupGames> <levelDir>");
            System.exit(2);
        }
        int players = Integer.parseInt(args[0]);
        Types.GAME_MODE mode = args[1].equalsIgnoreCase("Capitals") ? Types.GAME_MODE.CAPITALS : Types.GAME_MODE.SCORE;
        long first = Long.parseLong(args[2]);
        int games = Integer.parseInt(args[3]);
        int warmup = Integer.parseInt(args[4]);
        Path levelDir = Path.of(args[5]);
        Files.createDirectories(levelDir);
        java.io.PrintStream out = System.out;
        System.setOut(System.err);  // Tribes prints diagnostics; keep stdout for the CSV

        // Warm-up games (JIT) use seeds below the timed range and are not reported.
        for (int k = 0; k < warmup; ++k) play(players, mode, first - warmup + k, null);
        for (int k = 0; k < games; ++k) {
            long seed = first + k;
            long[] r = play(players, mode, seed, levelDir.resolve("level-" + players + "-" + args[1] + "-" + seed + ".txt"));
            out.println("tribes," + players + "," + args[1] + "," + seed + "," + r[0] + "," + r[1] + "," + r[2] + ","
                    + String.format("%.6f", r[3] / 1e9));
        }
        out.flush();
    }

    /** One random game on level seed `seed` (game RNG and agent RNG seeded from it too). Returns {ticks, steps, turns, nanos}. */
    private static long[] play(int players, Types.GAME_MODE mode, long seed, Path levelFile) throws Exception {
        Types.TRIBE[] tribes = new Types.TRIBE[players];
        for (int i = 0; i < players; ++i) tribes[i] = ALL[(int) ((seed + 5L * i) % 12)];  // 5 coprime to 12: distinct

        if (levelFile != null) {  // the level GameState.init generates, for the C++ side
            core.levelgen.LevelGenerator gen = new core.levelgen.LevelGenerator(seed);
            gen.init(core.TribesConfig.DEFAULT_MAP_SIZE[players - 1], 3, 4, 0.5, tribes);
            gen.generate();
            String[] lines = gen.gelLevelLines();
            try (PrintWriter w = new PrintWriter(Files.newBufferedWriter(levelFile))) {
                w.println(mode == Types.GAME_MODE.CAPITALS ? "Capitals" : "Score");
                for (String l : lines) w.println(l);
            }
        }

        GameState gs = new GameState(new Random(seed), mode);
        gs.init(seed, tribes);
        Random agent = new Random(seed ^ 0x5DEECE66DL);
        Tribe[] tt = gs.getTribes();
        long steps = 0;
        long turns = 0;

        long t0 = System.nanoTime();
        gameLoop:
        while (!gs.gameOver()) {
            for (int id = 0; id < tt.length; ++id) {
                Tribe tribe = tt[id];
                if (tribe.getWinner() != Types.RESULT.INCOMPLETE) continue;
                ++turns;
                gs.initTurn(tribe);
                gs.computePlayerActions(tribe);
                boolean continueTurn = true;
                while (true) {
                    Action action;
                    if (continueTurn) {
                        ArrayList<Action> all = gs.getAllAvailableActions();
                        action = all.get(agent.nextInt(all.size()));
                        ++steps;
                        continueTurn = !gs.isTurnEnding() && gs.existAvailableActions(tribe);
                    } else {
                        action = null;
                    }
                    if (action == null || action.getActionType() == Types.ACTION.END_TURN) break;
                    gs.next(action);
                    gs.computePlayerActions(tribe);
                    if (gs.gameOver()) break;
                }
                gs.endTurn(tribe);
                if (gs.gameOver()) break gameLoop;
            }
            gs.incTick();
        }
        long nanos = System.nanoTime() - t0;
        return new long[] {gs.getTick(), steps, turns, nanos};
    }
}
