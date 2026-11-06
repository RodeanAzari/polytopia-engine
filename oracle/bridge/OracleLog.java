// Our own harness (ADR-002): logs one seeded Tribes game for differential tests (see
// core/game/OracleDriver.java for the log format). Agents are built exactly as in OracleRun.
//
// Usage: OracleLog <levelSeed> <gameSeed> <agentSeed> <Capitals|Score> <players,...> <tribes,...> [--no-states]

import core.Types;
import core.game.OracleDriver;
import players.Agent;

import java.io.BufferedOutputStream;
import java.io.PrintStream;

public class OracleLog {
    public static void main(String[] args) throws Exception {
        if (args.length != 6 && !(args.length == 7 && args[6].equals("--no-states"))) {
            System.err.println("usage: OracleLog <levelSeed> <gameSeed> <agentSeed> <Capitals|Score> <players,...> <tribes,...> [--no-states]");
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
        OracleRun.setStockAgentParams();

        Types.TRIBE[] tribes = new Types.TRIBE[tribeNames.length];
        Agent[] agents = new Agent[playerNames.length];
        for (int i = 0; i < tribeNames.length; ++i) {
            tribes[i] = Run.parseTribeStr(tribeNames[i]);
            agents[i] = Run.getAgent(Run.parsePlayerTypeStr(playerNames[i]), agentSeed);
            if (agents[i] == null) {
                System.err.println("unknown player: " + playerNames[i]);
                System.exit(2);
            }
        }
        // Tribes prints diagnostics to System.out; keep them out of the log by giving the log its own stream.
        PrintStream log = new PrintStream(new BufferedOutputStream(new java.io.FileOutputStream(java.io.FileDescriptor.out), 1 << 16), false, "UTF-8");
        System.setOut(System.err);
        OracleDriver.run(levelSeed, gameSeed, mode, tribes, agents, args.length == 6, log);
        log.flush();
    }
}
