// Our own harness (ADR-002): one headless Tribes game with every seed fixed, for tournaments of Tribes' own
// agents (oracle/scripts/tribes_tournament.py). Same construction as OracleRun (stock agent parameters from the
// stock tournament.json), except that agent i gets seed agentSeed + i, and the result is printed as data.
// Tribes' search agents still draw from an unseeded Random inside GameState.copy(), so a game is reproducible
// only statistically (progress.md, oracle caveats).
//
// Usage: OracleMatch <levelSeed> <gameSeed> <agentSeed> <Capitals|Score> <players,...> <tribes,...>
// Output (stdout), one line per requested player i, in request order:
//   RESULT <i> <player> <tribe> <WIN|LOSS|INCOMPLETE> <score> <techs> <cities> <production> <seconds>

import core.Types;
import core.game.Game;
import core.game.TribeResult;
import players.Agent;

import java.io.PrintStream;
import java.util.ArrayList;

public class OracleMatch {
    public static void main(String[] args) throws Exception {
        if (args.length != 6) {
            System.err.println("usage: OracleMatch <levelSeed> <gameSeed> <agentSeed> <Capitals|Score> <players,...> <tribes,...>");
            System.exit(2);
        }
        long levelSeed = Long.parseLong(args[0]);
        long gameSeed = Long.parseLong(args[1]);
        long agentSeed = Long.parseLong(args[2]);
        Types.GAME_MODE mode = args[3].equalsIgnoreCase("Capitals") ? Types.GAME_MODE.CAPITALS : Types.GAME_MODE.SCORE;
        String[] playerNames = args[4].split(",");
        String[] tribeNames = args[5].split(",");
        if (playerNames.length != tribeNames.length) {
            System.err.println("player and tribe counts differ");
            System.exit(2);
        }
        PrintStream out = System.out;
        System.setOut(System.err);  // Tribes prints progress; keep stdout for results
        core.Constants.VERBOSE = false;
        OracleRun.setStockAgentParams();

        Types.TRIBE[] tribes = new Types.TRIBE[tribeNames.length];
        ArrayList<Integer> allIds = new ArrayList<>();
        ArrayList<Agent> players = new ArrayList<>();
        for (int i = 0; i < tribeNames.length; ++i) {
            tribes[i] = Run.parseTribeStr(tribeNames[i]);
            allIds.add(i);
        }
        for (int i = 0; i < playerNames.length; ++i) {
            Agent ag = Run.getAgent(Run.parsePlayerTypeStr(playerNames[i]), agentSeed + i);
            if (ag == null) {
                System.err.println("unknown player: " + playerNames[i]);
                System.exit(2);
            }
            ag.setPlayerIDs(i, allIds);
            players.add(ag);
        }

        long t0 = System.nanoTime();
        Game game = new Game();
        game.init(players, levelSeed, tribes, gameSeed, mode);
        game.run(null, null);
        double seconds = (System.nanoTime() - t0) / 1e9;

        // Seats bind by tribe type (rule facts 05 §1.2): map each ranked tribe back to its requested player.
        String[] lines = new String[playerNames.length];
        for (TribeResult tr : game.getCurrentRanking()) {
            Types.TRIBE type = game.getBoard().getTribe(tr.getId()).getType();
            for (int i = 0; i < tribes.length; ++i) {
                if (tribes[i] == type) {
                    lines[i] = "RESULT " + i + " " + playerNames[i].replace(' ', '_') + " " + type + " " + tr.getResult()
                            + " " + (long) tr.getScore() + " " + tr.getNumTechsResearched() + " " + tr.getNumCities()
                            + " " + tr.getProduction() + " " + String.format("%.3f", seconds);
                }
            }
        }
        for (String l : lines) out.println(l);
        out.flush();
    }
}
