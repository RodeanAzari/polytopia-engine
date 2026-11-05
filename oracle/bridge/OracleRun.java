// Our own harness around the (unlicensed) Tribes framework: calls its public API only, no Tribes
// code copied (ADR-002). Runs one headless game with every seed fixed.
//
// Usage: OracleRun <levelSeed> <gameSeed> <agentSeed> <Capitals|Score> <player0>,<player1>[,...] <tribe0>,<tribe1>[,...]
// Player names as in Tribes configs: "Random", "Rule Based", "OSLA", "MC", "MCTS", "RHEA", ...

import core.Types;
import core.game.Game;
import players.Agent;

import java.util.ArrayList;

public class OracleRun {
    public static void main(String[] args) throws Exception {
        if (args.length != 6) {
            System.err.println("usage: OracleRun <levelSeed> <gameSeed> <agentSeed> <Capitals|Score> <players,...> <tribes,...>");
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

        // Agent parameters: same values as the stock tribes tournament.json.
        Run.MAX_LENGTH = 20;
        Run.FORCE_TURN_END = false;
        Run.MCTS_ROLLOUTS = false;
        Run.POP_SIZE = 1;
        Run.PRUNING = true;
        Run.PROGBIAS = true;
        Run.K_INIT_MULT = 0.5;
        Run.T_MULT = 2.0;
        Run.A_MULT = 1.5;
        Run.B = 1.3;

        Types.TRIBE[] tribes = new Types.TRIBE[tribeNames.length];
        ArrayList<Integer> allIds = new ArrayList<>();
        for (int i = 0; i < tribeNames.length; ++i) {
            tribes[i] = Run.parseTribeStr(tribeNames[i]);
            allIds.add(i);
        }
        ArrayList<Agent> players = new ArrayList<>();
        for (int i = 0; i < playerNames.length; ++i) {
            Agent ag = Run.getAgent(Run.parsePlayerTypeStr(playerNames[i]), agentSeed);
            if (ag == null) {
                System.err.println("unknown player: " + playerNames[i]);
                System.exit(2);
            }
            ag.setPlayerIDs(i, allIds);
            players.add(ag);
        }

        Game game = new Game();
        game.init(players, levelSeed, tribes, gameSeed, mode);
        game.run(null, null);
    }
}
