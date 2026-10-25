// For each string given in the script arguments (use \n for newline), find it in memory, list
// every code reference to it, the containing function, and the next few CALL targets after the
// reference. Optional first argument "-decomp" also prints the decompiled containing function.
// Usage (headless): -postScript XrefStrings.java [-decomp] "exec default.cfg\n" "cmdlist" ...
// @category mw2coop
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import java.util.HashSet;
import java.util.Set;

public class XrefStrings extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		boolean decomp = false;
		DecompInterface di = null;
		long base = currentProgram.getImageBase().getOffset();
		for (String raw : args) {
			if (raw.equals("-decomp")) {
				decomp = true;
				di = new DecompInterface();
				di.openProgram(currentProgram);
				continue;
			}
			if (raw.startsWith("ref:")) {
				// "ref:<rva hex>": every reference to that address, with the containing function
				Address ta = currentProgram.getImageBase().add(Long.parseLong(raw.substring(4).replace("0x", ""), 16));
				println("=== refs to " + ta);
				for (Reference r : getReferencesTo(ta)) {
					Function f = getFunctionContaining(r.getFromAddress());
					println("  " + r.getReferenceType() + " " + r.getFromAddress() + " in " + (f == null ? "?" : f.getName()
							+ " (rva 0x" + Long.toHexString(f.getEntryPoint().getOffset() - base) + ")"));
				}
				continue;
			}
			if (raw.startsWith("sym:")) {
				// "sym:Name": list call sites of a (usually imported) function by name
				String sym = raw.substring(4);
				println("=== symbol " + sym);
				for (ghidra.program.model.symbol.Symbol sy : currentProgram.getSymbolTable().getSymbols(sym)) {
					for (Reference r : getReferencesTo(sy.getAddress())) {
						Address from = r.getFromAddress();
						Function f = getFunctionContaining(from);
						println("  ref " + from + " in " + (f == null ? "?" : f.getName() + " (rva 0x"
								+ Long.toHexString(f.getEntryPoint().getOffset() - base) + ")"));
						// thunk/IAT pointer: follow one level to real call sites
						for (Reference r2 : getReferencesTo(from)) {
							Function f2 = getFunctionContaining(r2.getFromAddress());
							println("    via " + r2.getFromAddress() + " in " + (f2 == null ? "?" : f2.getName()
									+ " (rva 0x" + Long.toHexString(f2.getEntryPoint().getOffset() - base) + ")"));
						}
					}
				}
				continue;
			}
			if (raw.startsWith("dis:")) {
				// "dis:<address hex>:<count>": the count instructions from that address (image base + address)
				String[] p = raw.substring(4).split(":");
				Address da = currentProgram.getImageBase().add(Long.parseLong(p[0].replace("0x", ""), 16));
				int count = p.length > 1 ? Integer.parseInt(p[1]) : 40;
				println("=== disassembly " + da);
				Instruction ins = getInstructionAt(da);
				if (ins == null)
					ins = getInstructionContaining(da);
				for (int i = 0; ins != null && i < count; i++, ins = ins.getNext())
					println("  " + ins.getAddress() + "  " + ins.toString());
				continue;
			}
			if (raw.startsWith("@")) {
				// "@<rva hex>": decompile the function at image base + rva
				Address fa = currentProgram.getImageBase().add(Long.parseLong(raw.substring(1).replace("0x", ""), 16));
				Function f = getFunctionAt(fa);
				if (f == null)
					f = getFunctionContaining(fa);
				println("=== function " + raw + " -> " + (f == null ? "none" : f.getName() + "@" + f.getEntryPoint()));
				if (f != null) {
					if (di == null) {
						di = new DecompInterface();
						di.openProgram(currentProgram);
					}
					DecompileResults res = di.decompileFunction(f, 60, monitor);
					if (res.decompileCompleted()) {
						String c = res.getDecompiledFunction().getC();
						println(c.length() > 8000 ? c.substring(0, 8000) + "\n  ...[cut]" : c);
					}
				}
				continue;
			}
			String s = raw.replace("\\n", "\n");
			byte[] needle = new byte[s.length() + 1];
			for (int i = 0; i < s.length(); i++)
				needle[i] = (byte) s.charAt(i);
			needle[s.length()] = 0;
			println("=== \"" + raw + "\"");
			Address a = currentProgram.getMinAddress();
			int found = 0;
			Set<Function> shown = new HashSet<>();
			while (found < 4) {
				a = find(a, needle);
				if (a == null)
					break;
				found++;
				println(" string @ " + a + " (rva 0x" + Long.toHexString(a.getOffset() - base) + ")");
				for (Reference r : getReferencesTo(a)) {
					Address from = r.getFromAddress();
					Function f = getFunctionContaining(from);
					String fn = f == null ? "?" : f.getName() + "@" + f.getEntryPoint() + " (rva 0x"
							+ Long.toHexString(f.getEntryPoint().getOffset() - base) + ")";
					StringBuilder calls = new StringBuilder();
					Instruction ins = getInstructionAt(from);
					for (int n = 0; ins != null && n < 12; n++, ins = ins.getNext()) {
						if (ins.getFlowType().isCall()) {
							Address[] fl = ins.getFlows();
							String t = fl.length > 0 ? fl[0] + " (rva 0x" + Long.toHexString(fl[0].getOffset() - base) + ")"
									: ins.toString();
							calls.append("\n      call ").append(t);
							if (calls.length() > 600)
								break;
						}
					}
					println("  ref " + from + " in " + fn + calls);
					if (decomp && f != null && !shown.contains(f)) {
						shown.add(f);
						DecompileResults res = di.decompileFunction(f, 60, monitor);
						if (res.decompileCompleted()) {
							String c = res.getDecompiledFunction().getC();
							println(c.length() > 6000 ? c.substring(0, 6000) + "\n  ...[cut]" : c);
						}
					}
				}
				a = a.add(1);
			}
			if (found == 0)
				println(" not found");
		}
	}
}
