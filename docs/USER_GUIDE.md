# User guide

Extract the entire portable ZIP. Start `CampusLink.exe` next to `SDL3.dll`; Vulkan is tried first, software is used if it fails. The software renderer is not a Wi-Fi simulation.

- **Virtual Client Lab**: choose a logical client and DHCP pool size, lease, seed and scenario. Start, pause/resume and stop. Recording saves the scenario and a `SIMULATED` CSV under the folder in **Settings**. Replay uses the saved seed; it does not replay packets. Only the first 120 client details appear in the table; counters include all clients.
- **Hardware Lab / Radio Capabilities**: refresh the inventory and distinguish driver data from unknowns. **Network Discovery** scans on explicit request and reports Windows permission errors. Select a BSSID to open **Connection Manager**.
- **Physical Experiment**: edit the packaged scope example as `test_scope.conf` in the user data folder, using an explicitly authorized AP and CLP1 server. Load it in **Experiment Center**. Select the profile+BSSID, read the warnings and authorize the experiment in **Connection Manager**. This can interrupt the host connection. Do not perform the test over a session on the interface being switched. Use **Stop task** to cancel work. Rollback is optional and attempts to restore only the immediately previous usable profile.
- **Performance Lab / Intelligence Engine**: read endpoint-specific results and a weighted score with coverage. Results are not public Internet tests or throughput guarantees.
- **Logs & Diagnostics**: review steps separately, filter warnings and errors and export structured event codes (no WLAN keys or raw network identifiers in the export).
