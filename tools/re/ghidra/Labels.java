// Applies labels from a "address<TAB>name" file to the program (run once after import).
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.SourceType;
import java.io.BufferedReader;
import java.io.FileReader;

public class Labels extends GhidraScript {
  @Override
  public void run() throws Exception {
    int n = 0;
    try (BufferedReader r = new BufferedReader(new FileReader(getScriptArgs()[0]))) {
      for (String line; (line = r.readLine()) != null; ) {
        String[] p = line.split("\t");
        if (p.length < 2) continue;
        try {
          createLabel(toAddr(Long.parseUnsignedLong(p[0], 16)), p[1], true, SourceType.IMPORTED);
          n++;
        } catch (Exception e) {
          // an unusable name: skip
        }
      }
    }
    println(n + " labels");
  }
}
