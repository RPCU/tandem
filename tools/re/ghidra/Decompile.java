// Decompiles the functions at the given addresses (hex), creating them first:
// the program is imported without analysis. "+N" as first argument also
// decompiles callees, N levels down.
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import java.util.ArrayDeque;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;

public class Decompile extends GhidraScript {
  private Function ensure(Address a) throws Exception {
    Function f = getFunctionAt(a);
    if (f != null) return f;
    if (getInstructionAt(a) == null) new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
    return createFunction(a, null);
  }

  @Override
  public void run() throws Exception {
    String[] args = getScriptArgs();
    int depth = 0, first = 0;
    if (args.length > 0 && args[0].startsWith("+")) {
      depth = Integer.parseInt(args[0].substring(1));
      first = 1;
    }
    Map<Address, Integer> todo = new LinkedHashMap<>();
    ArrayDeque<Address> queue = new ArrayDeque<>();
    for (int i = first; i < args.length; i++) {
      Address a = toAddr(Long.parseUnsignedLong(args[i].replace("0x", ""), 16));
      todo.put(a, 0);
      queue.add(a);
    }
    DecompInterface dec = new DecompInterface();
    dec.openProgram(currentProgram);
    Set<Address> done = new HashSet<>();
    while (!queue.isEmpty()) {
      Address a = queue.poll();
      if (!done.add(a)) continue;
      Function f = ensure(a);
      if (f == null) {
        println("// no function at " + a);
        continue;
      }
      int level = todo.get(a);
      if (level < depth) {
        for (Instruction in : currentProgram.getListing().getInstructions(f.getBody(), true)) {
          if (!in.getFlowType().isCall() && !in.getFlowType().isJump()) continue;
          for (Reference r : in.getReferencesFrom()) {
            Address t = r.getToAddress();
            if (r.getReferenceType().isFlow() && !f.getBody().contains(t) && t.isMemoryAddress()
                && currentProgram.getMemory().getBlock(t) != null
                && currentProgram.getMemory().getBlock(t).isExecute() && !todo.containsKey(t)) {
              todo.put(t, level + 1);
              queue.add(t);
            }
          }
        }
      }
      DecompileResults res = dec.decompileFunction(f, 120, monitor);
      println("\n// ===== " + a + " " + f.getName() + " =====");
      println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "// failed: " + res.getErrorMessage());
    }
  }
}
