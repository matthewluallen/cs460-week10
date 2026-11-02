import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;
import java.util.*;

public class ghidra_export extends GhidraScript {
    public void run() throws Exception {
        String[] a = getScriptArgs();
        String outBase = a.length > 0 ? a[0] : "/tmp/ghidra_out";
        String focus = (a.length > 1 && a[1] != null) ? a[1].trim().toLowerCase() : "";
        FunctionManager fm = currentProgram.getFunctionManager();
        List<Function> funcs = new ArrayList<>();
        for (Function f : fm.getFunctions(true)) funcs.add(f);
        int named = 0;
        for (Function f : funcs) if (!f.getName().startsWith("FUN_")) named++;
        PrintWriter fi = new PrintWriter(new FileWriter(outBase + ".functions.txt"));
        fi.println("# " + funcs.size() + " functions in " + currentProgram.getName()
                   + " (" + named + " named / DWARF)");
        for (Function f : funcs) fi.printf("%-48s %s%n", f.getName(), f.getEntryPoint());
        fi.close();
        // the lab's security story, in teaching order (Parts 3,1,9,6,8)
        String[] SEC = {
          "fc_write_single_coil","fc_write_multiple_coils","plc_force_coil","cmd_force","plc_tag_write",
          "plc_clear_all_forces","eth_ota_begin","eth_ota_topup","_upload_file","_verifyend",
          "_partitionisbootable","eth_http_handle","_handlerequest","modbus_handle_update_coils",
          "modbus_daemon","set_coil","settimeofday","sntp_setserver","plc_network_http_allowed",
          "plc_network_legacy_snapshot_allowed","plc_network_deferred_allowed"
        };
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        PrintWriter c = new PrintWriter(new FileWriter(outBase + ".decompiled.c"));
        c.println("/* Ghidra decompilation of " + currentProgram.getName() + " - "
                  + funcs.size() + " functions (" + named + " named).");
        c.println("   Default view: the firmware's security-relevant functions, in lab order.");
        c.println("   Re-run with a name to focus, e.g.  ./firmware-re/ghidra.sh fc_write_single_coil */\n");
        LinkedHashSet<Function> picked = new LinkedHashSet<>();
        if (!focus.isEmpty()) {
            for (Function f : funcs) if (f.getName().toLowerCase().contains(focus)) picked.add(f);
        } else {
            for (String key : SEC)
                for (Function f : funcs)
                    if (f.getName().toLowerCase().contains(key)) picked.add(f);
        }
        int cnt = 0;
        for (Function f : picked) {
            DecompileResults res = dec.decompileFunction(f, 60, monitor);
            if (res != null && res.getDecompiledFunction() != null) {
                c.println("/* ==== " + f.getName() + " @ " + f.getEntryPoint() + " ==== */");
                c.println(res.getDecompiledFunction().getC());
                cnt++;
            }
            if (cnt >= 40) break;
        }
        c.close();
        println("GHIDRA_EXPORT wrote " + cnt + " decompiled functions; " + named + " named of " + funcs.size());
    }
}
