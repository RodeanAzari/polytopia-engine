// Our own harness (ADR-002): logs one seeded Tribes game for differential tests (see
// core/game/OracleDriver.java for the log format). Agents are built exactly as in OracleRun, plus our
// "MindBender" agent (MindBenderAgent.java) for the mindbender scenario.
//
// Usage: OracleLog <levelSeed> <gameSeed> <agentSeed> <Capitals|Score> <players,...> <tribes,...>
//                  [--no-states|--init-only] [--scenario=mindbender] [--probe=N]

import core.Types;
import core.game.OracleDriver;
import players.Agent;

import java.io.BufferedOutputStream;
import java.io.PrintStream;

public class OracleLog {
    public static void main(String[] args) throws Exception {
        String flag = "";
        String scenario = "";
        boolean ok = args.length >= 6;
        for (int i = 6; i < args.length; ++i) {
            if (args[i].equals("--no-states") || args[i].equals("--init-only")) {
                ok = ok && flag.isEmpty();
                flag = args[i];
            } else if (args[i].equals("--scenario=mindbender")) {
                scenario = "mindbender";
            } else if (args[i].startsWith("--probe=")) {
                core.game.OracleDriver.probeEvery = Integer.parseInt(args[i].substring(8));
            } else {
                ok = false;
            }
        }
        if (!ok) {
            System.err.println("usage: OracleLog <levelSeed> <gameSeed> <agentSeed> <Capitals|Score> <players,...> <tribes,...> [--no-states|--init-only] [--scenario=mindbender] [--probe=N]");
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
            agents[i] = playerNames[i].equals("MindBender") ? new MindBenderAgent(agentSeed + i)
                    : Run.getAgent(Run.parsePlayerTypeStr(playerNames[i]), agentSeed);
            if (agents[i] == null) {
                System.err.println("unknown player: " + playerNames[i]);
                System.exit(2);
            }
        }
        // Tribes prints diagnostics to System.out; keep them out of the log by giving the log its own stream.
        PrintStream log = new PrintStream(new BufferedOutputStream(new java.io.FileOutputStream(java.io.FileDescriptor.out), 1 << 16), false, "UTF-8");
        System.setOut(System.err);
        OracleDriver.run(levelSeed, gameSeed, mode, tribes, agents, flag.isEmpty(), flag.equals("--init-only"), scenario, log);
        log.flush();
    }
}
