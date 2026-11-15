// Decompiles the functions containing the given addresses (script arguments, hex VAs) into the file named by the first argument.
// An argument "r:<hex>" lists the references to that address instead.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import java.io.FileWriter;
import java.util.HashSet;
import java.util.Set;

public class DecompAt extends GhidraScript {
	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		FileWriter out = new FileWriter(args[0]);
		DecompInterface di = new DecompInterface();
		di.openProgram(currentProgram);
		Set<String> done = new HashSet<>();
		for (int i = 1; i < args.length; i++) {
			if (args[i].startsWith("r:")) {
				Address t = toAddr(Long.parseLong(args[i].substring(2), 16));
				out.write("// xrefs to " + args[i] + ":\n");
				for (Reference ref : getReferencesTo(t)) {
					Function rf = getFunctionContaining(ref.getFromAddress());
					out.write("//   " + ref.getFromAddress() + " " + ref.getReferenceType() + " in " + (rf == null ? "?" : rf.getName() + "@" + rf.getEntryPoint()) + "\n");
				}
				out.write("\n");
				continue;
			}
			Address a = toAddr(Long.parseLong(args[i], 16));
			Function f = getFunctionContaining(a);
			if (f == null) {
				out.write("// no function at " + args[i] + "\n\n");
				continue;
			}
			String key = f.getEntryPoint().toString();
			if (!done.add(key))
				continue;
			out.write("// ===== " + f.getName() + " at " + key + " (asked " + args[i] + ")\n");
			DecompileResults r = di.decompileFunction(f, 60, monitor);
			if (r.decompileCompleted())
				out.write(r.getDecompiledFunction().getC() + "\n");
			else
				out.write("// decompile failed: " + r.getErrorMessage() + "\n");
		}
		out.close();
	}
}
