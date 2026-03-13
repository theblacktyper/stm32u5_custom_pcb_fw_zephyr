"""
Edge AI Proto — Firmware Upload GUI
Uses mcumgr.exe to upload firmware via MCUBoot/MCUmgr over UART.
"""

import os
import re
import shutil
import subprocess
import sys
import threading
import time
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from pathlib import Path

# ---------------------------------------------------------------------------
# Dependency check
# ---------------------------------------------------------------------------
try:
    import serial.tools.list_ports  # pyserial
except ImportError:
    # Show a Tk error dialog then exit
    _root = tk.Tk()
    _root.withdraw()
    messagebox.showerror(
        "Missing dependency",
        "pyserial is not installed.\n\n"
        "Run:  pip install pyserial\n"
        "or:   python -m pip install pyserial",
    )
    sys.exit(1)

MCUMGR = shutil.which("mcumgr")
if MCUMGR is None:
    _root = tk.Tk()
    _root.withdraw()
    messagebox.showerror(
        "mcumgr not found",
        "mcumgr.exe is not on PATH.\n\n"
        "Install it and make sure it is available in your system PATH.",
    )
    sys.exit(1)

# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------
DEFAULT_BAUD = "921600"
BAUD_OPTIONS = ["115200", "230400", "460800", "921600", "1000000"]
PROGRESS_RE = re.compile(r"(\d+\.?\d*)\s*%")
HASH_RE = re.compile(r"hash:\s*([0-9a-fA-F]+)")
VERSION_RE = re.compile(r"version:\s*(\S+)")
SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_FW_PATH = SCRIPT_DIR.parent / "build" / "kk_edge_ai_tflm_hello" / "zephyr" / "zephyr.signed.bin"


class FwUploadApp:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("Edge AI Proto — Firmware Upload")
        self.root.minsize(620, 520)
        self.root.resizable(True, True)

        self._running = False  # True while a subprocess is active
        self._process: subprocess.Popen | None = None
        self._dfu_ready = False  # True after successful DFU? check

        style = ttk.Style()
        try:
            style.theme_use("vista")
        except tk.TclError:
            pass  # fallback to default theme

        self._build_ui()
        self._refresh_ports()
        self._try_default_firmware()

    # ------------------------------------------------------------------
    # UI construction
    # ------------------------------------------------------------------
    def _build_ui(self):
        pad = dict(padx=6, pady=3)

        # --- Connection frame ---
        conn = ttk.LabelFrame(self.root, text="Connection")
        conn.pack(fill="x", **pad)

        ttk.Label(conn, text="COM Port:").grid(row=0, column=0, sticky="w", **pad)
        self.port_var = tk.StringVar()
        self.port_cb = ttk.Combobox(conn, textvariable=self.port_var, width=18, state="readonly")
        self.port_cb.grid(row=0, column=1, sticky="w", **pad)
        self.refresh_btn = ttk.Button(conn, text="Refresh", command=self._refresh_ports)
        self.refresh_btn.grid(row=0, column=2, sticky="w", **pad)

        ttk.Label(conn, text="Baud:").grid(row=1, column=0, sticky="w", **pad)
        self.baud_var = tk.StringVar(value=DEFAULT_BAUD)
        self.baud_cb = ttk.Combobox(conn, textvariable=self.baud_var, values=BAUD_OPTIONS, width=18, state="readonly")
        self.baud_cb.grid(row=1, column=1, sticky="w", **pad)

        # --- Firmware frame ---
        fw = ttk.LabelFrame(self.root, text="Firmware")
        fw.pack(fill="x", **pad)

        ttk.Label(fw, text="File:").grid(row=0, column=0, sticky="w", **pad)
        self.file_var = tk.StringVar()
        self.file_var.trace_add("write", self._on_file_changed)
        self.file_entry = ttk.Entry(fw, textvariable=self.file_var, width=48)
        self.file_entry.grid(row=0, column=1, sticky="ew", **pad)
        self.browse_btn = ttk.Button(fw, text="Browse", command=self._browse_file)
        self.browse_btn.grid(row=0, column=2, sticky="w", **pad)
        fw.columnconfigure(1, weight=1)

        self.size_var = tk.StringVar(value="Size: —")
        ttk.Label(fw, textvariable=self.size_var).grid(row=1, column=1, sticky="w", **pad)

        # --- Progress bar ---
        self.progress_var = tk.DoubleVar(value=0.0)
        pbar_frame = ttk.Frame(self.root)
        pbar_frame.pack(fill="x", **pad)
        self.progress_bar = ttk.Progressbar(pbar_frame, variable=self.progress_var, maximum=100)
        self.progress_bar.pack(side="left", fill="x", expand=True)
        self.pct_label = ttk.Label(pbar_frame, text="0.0%", width=8)
        self.pct_label.pack(side="left", padx=(6, 0))

        # --- Action buttons ---
        btn_frame = ttk.Frame(self.root)
        btn_frame.pack(fill="x", **pad)
        self.dfu_btn = ttk.Button(btn_frame, text="DFU?", command=self._do_dfu_check)
        self.dfu_btn.pack(side="left", **pad)
        self.upload_btn = ttk.Button(btn_frame, text="Upload", command=self._do_upload, state="disabled")
        self.upload_btn.pack(side="left", **pad)

        # --- Active version label (between buttons and log) ---
        self.version_var = tk.StringVar(value="")
        self.version_label = tk.Label(
            self.root,
            textvariable=self.version_var,
            fg="red",
            font=("Consolas", 10, "bold"),
        )
        self.version_label.pack(fill="x", padx=6)

        # --- Log area ---
        log_frame = ttk.LabelFrame(self.root, text="Log")
        log_frame.pack(fill="both", expand=True, **pad)

        self.log_text = tk.Text(
            log_frame,
            height=10,
            bg="#1e1e1e",
            fg="#d4d4d4",
            insertbackground="#d4d4d4",
            font=("Consolas", 10),
            state="disabled",
            wrap="word",
        )
        scroll = ttk.Scrollbar(log_frame, command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=scroll.set)
        scroll.pack(side="right", fill="y")
        self.log_text.pack(fill="both", expand=True)

        # --- Status bar ---
        status_frame = ttk.Frame(self.root)
        status_frame.pack(fill="x", **pad)
        self.status_var = tk.StringVar(value="Ready")
        ttk.Label(status_frame, textvariable=self.status_var).pack(side="left")
        ttk.Button(status_frame, text="Clear Log", command=self._clear_log).pack(side="right")

    # ------------------------------------------------------------------
    # Port helpers
    # ------------------------------------------------------------------
    def _refresh_ports(self):
        ports = sorted(serial.tools.list_ports.comports(), key=lambda p: p.device)
        names = [p.device for p in ports]
        self.port_cb["values"] = names
        if names:
            if self.port_var.get() not in names:
                self.port_var.set(names[0])
            self.status_var.set(f"{len(names)} COM port(s) found")
        else:
            self.port_var.set("")
            self.status_var.set("No COM ports found — connect board and click Refresh")

    # ------------------------------------------------------------------
    # File helpers
    # ------------------------------------------------------------------
    def _try_default_firmware(self):
        if DEFAULT_FW_PATH.is_file():
            self.file_var.set(str(DEFAULT_FW_PATH))

    def _browse_file(self):
        initial_dir = str(SCRIPT_DIR.parent / "build")
        if not Path(initial_dir).is_dir():
            initial_dir = str(SCRIPT_DIR)
        path = filedialog.askopenfilename(
            title="Select signed firmware binary",
            initialdir=initial_dir,
            filetypes=[("Signed binary", "*.signed.bin"), ("Binary", "*.bin"), ("All files", "*.*")],
        )
        if path:
            self.file_var.set(path)

    def _on_file_changed(self, *_args):
        p = Path(self.file_var.get())
        if p.is_file():
            size_kb = p.stat().st_size / 1024
            self.size_var.set(f"Size: {size_kb:.1f} KB")
        else:
            self.size_var.set("Size: —")
        self._update_upload_btn()

    # ------------------------------------------------------------------
    # Upload button enable/disable logic
    # ------------------------------------------------------------------
    def _update_upload_btn(self):
        """Enable Upload only when DFU is confirmed and a file is selected."""
        if self._running:
            return
        file_ok = Path(self.file_var.get()).is_file()
        if self._dfu_ready and file_ok:
            self.upload_btn.configure(state="normal")
        else:
            self.upload_btn.configure(state="disabled")

    # ------------------------------------------------------------------
    # Logging
    # ------------------------------------------------------------------
    def _log(self, text: str, replace_last: bool = False):
        self.log_text.configure(state="normal")
        if replace_last:
            # Delete the last line and replace
            self.log_text.delete("end-2l linestart", "end-1l lineend")
            if self.log_text.get("end-2c", "end-1c") == "\n":
                pass  # fine
            else:
                self.log_text.insert("end", "\n")
        self.log_text.insert("end", text + "\n")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def _clear_log(self):
        self.log_text.configure(state="normal")
        self.log_text.delete("1.0", "end")
        self.log_text.configure(state="disabled")

    # ------------------------------------------------------------------
    # Connection string
    # ------------------------------------------------------------------
    def _connstring(self) -> list[str]:
        port = self.port_var.get()
        baud = self.baud_var.get()
        if not port:
            raise ValueError("No COM port selected")
        return ["--conntype=serial", f"--connstring=dev={port},baud={baud}"]

    # ------------------------------------------------------------------
    # Button state management during operations
    # ------------------------------------------------------------------
    def _set_buttons(self, enabled: bool):
        if enabled:
            self.dfu_btn.configure(state="normal")
            self._update_upload_btn()
        else:
            self.dfu_btn.configure(state="disabled")
            self.upload_btn.configure(state="disabled")

    # ------------------------------------------------------------------
    # Generic command runner (for simple commands)
    # ------------------------------------------------------------------
    def _run_cmd(self, args: list[str], label: str, callback=None, err_callback=None):
        if self._running:
            return
        try:
            conn = self._connstring()
        except ValueError as e:
            self._log(f"ERROR: {e}")
            self.status_var.set(str(e))
            return

        self._running = True
        self._set_buttons(False)
        self.status_var.set(f"{label}...")
        self._log(f"> {label}...")

        cmd = [MCUMGR] + conn + args

        def _worker():
            try:
                result = subprocess.run(
                    cmd,
                    capture_output=True,
                    text=True,
                    timeout=30,
                    creationflags=subprocess.CREATE_NO_WINDOW,
                )
                output = (result.stdout or "") + (result.stderr or "")
                self.root.after(0, self._cmd_done, label, output, result.returncode, callback, err_callback)
            except subprocess.TimeoutExpired:
                self.root.after(0, self._cmd_done, label, "ERROR: Command timed out", 1, None, err_callback)
            except Exception as exc:
                self.root.after(0, self._cmd_done, label, f"ERROR: {exc}", 1, None, err_callback)

        threading.Thread(target=_worker, daemon=True).start()

    def _cmd_done(self, label: str, output: str, returncode: int, callback, err_callback=None):
        self._running = False
        for line in output.strip().splitlines():
            self._log(f"  {line}")
        if returncode == 0:
            self.status_var.set(f"{label} complete")
            self._log(f"> {label} complete")
        else:
            self.status_var.set(f"{label} failed")
            self._log(f"> {label} FAILED (exit code {returncode})")
        self._set_buttons(True)
        if callback and returncode == 0:
            callback(output)
        if err_callback and returncode != 0:
            err_callback(output)

    # ------------------------------------------------------------------
    # DFU? — check if target is in DFU mode
    # ------------------------------------------------------------------
    def _do_dfu_check(self):
        def _check_response(output: str):
            if HASH_RE.search(output) or "slot=" in output.lower():
                self._dfu_ready = True
                self._log("> Target is in DFU mode — firmware update can proceed.")
                self.status_var.set("Target ready for firmware update")
                # Extract active version from slot 0
                self._show_active_version(output)
            else:
                self._dfu_ready = False
                self._log("> WARNING: Target must be put in DFU mode first!")
                self.status_var.set("Target not in DFU mode")
                self.version_var.set("")
            self._update_upload_btn()

        def _on_error(_output: str):
            self._dfu_ready = False
            self._log("> WARNING: Target must be put in DFU mode first!")
            self.status_var.set("Target not in DFU mode")
            self.version_var.set("")
            self._update_upload_btn()

        self._run_cmd(["image", "list"], "DFU?", callback=_check_response, err_callback=_on_error)

    def _show_active_version(self, output: str):
        """Parse slot 0 version from image list output and display it."""
        in_slot0 = False
        for line in output.splitlines():
            if "slot=0" in line:
                in_slot0 = True
            elif "slot=1" in line:
                in_slot0 = False
            if in_slot0:
                m = VERSION_RE.search(line)
                if m:
                    self.version_var.set(f"Current firmware: v{m.group(1)}")
                    return
        self.version_var.set("")

    # ------------------------------------------------------------------
    # Upload — upload + image list + image test + reset (full sequence)
    # ------------------------------------------------------------------
    def _do_upload(self):
        if self._running:
            return
        fw_path = self.file_var.get()
        if not fw_path or not Path(fw_path).is_file():
            self._log("ERROR: Select a valid firmware file first")
            self.status_var.set("No firmware file selected")
            return
        try:
            conn = self._connstring()
        except ValueError as e:
            self._log(f"ERROR: {e}")
            self.status_var.set(str(e))
            return

        self._running = True
        self._set_buttons(False)
        self.progress_var.set(0.0)
        self.pct_label.configure(text="0.0%")
        self.status_var.set("Uploading...")
        self._log(f"> Step 1/4: Uploading {Path(fw_path).name}...")

        upload_cmd = [MCUMGR, "-t", "60"] + conn + ["image", "upload", fw_path]

        STEP_DELAY = 1.0  # seconds between post-upload steps

        def _run_subcmd(args, timeout=30):
            """Run an mcumgr command synchronously (called from worker thread)."""
            cmd = [MCUMGR] + conn + args
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=timeout,
                creationflags=subprocess.CREATE_NO_WINDOW,
            )
            output = (result.stdout or "") + (result.stderr or "")
            return output, result.returncode

        def _parse_slot_hashes(output: str) -> tuple[str | None, str | None]:
            """Extract slot 0 and slot 1 hashes from image list output."""
            slot0_hash = None
            slot1_hash = None
            current_slot = None
            for line in output.splitlines():
                if "slot=0" in line:
                    current_slot = 0
                elif "slot=1" in line:
                    current_slot = 1
                m = HASH_RE.search(line)
                if m and current_slot == 0:
                    slot0_hash = m.group(1)
                elif m and current_slot == 1:
                    slot1_hash = m.group(1)
            # Fallback
            if not slot0_hash or not slot1_hash:
                all_hashes = HASH_RE.findall(output)
                if len(all_hashes) >= 1 and not slot0_hash:
                    slot0_hash = all_hashes[0]
                if len(all_hashes) >= 2 and not slot1_hash:
                    slot1_hash = all_hashes[1]
            return slot0_hash, slot1_hash

        def _worker():
            try:
                # --- Step 1: Upload ---
                proc = subprocess.Popen(
                    upload_cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    creationflags=subprocess.CREATE_NO_WINDOW,
                )
                self._process = proc

                buf = ""
                last_pct_line = None
                while True:
                    ch = proc.stdout.read(1)
                    if not ch:
                        break
                    ch = ch.decode("utf-8", errors="replace")
                    if ch == "\r" or ch == "\n":
                        line = buf.strip()
                        buf = ""
                        if not line:
                            continue
                        m = PROGRESS_RE.search(line)
                        if m:
                            pct = float(m.group(1))
                            replace = last_pct_line is not None
                            last_pct_line = line
                            self.root.after(0, self._upload_progress, pct, line, replace)
                        else:
                            last_pct_line = None
                            self.root.after(0, self._log, f"  {line}", False)
                    else:
                        buf += ch

                if buf.strip():
                    self.root.after(0, self._log, f"  {buf.strip()}", False)

                proc.wait()
                rc = proc.returncode
                self._process = None

                if rc != 0:
                    self.root.after(0, self._full_upload_fail, "Upload failed (exit code {})".format(rc))
                    return

                self.root.after(0, self._upload_progress, 100.0, "", False)
                self.root.after(0, self._log, "> Upload complete", False)

                time.sleep(STEP_DELAY)

                # --- Step 2: Image List ---
                self.root.after(0, self._log, "> Step 2/4: Image List...", False)
                self.root.after(0, lambda: self.status_var.set("Image List..."))
                output, rc = _run_subcmd(["image", "list"])
                for line in output.strip().splitlines():
                    self.root.after(0, self._log, f"  {line}", False)
                if rc != 0:
                    self.root.after(0, self._full_upload_fail, f"Image List failed (exit code {rc})")
                    return

                slot0_hash, slot1_hash = _parse_slot_hashes(output)
                if not slot1_hash:
                    self.root.after(0, self._full_upload_fail, "No slot 1 hash found")
                    return
                self.root.after(0, self._log, f"> Slot 1 hash: {slot1_hash[:16]}...", False)

                if slot0_hash and slot0_hash == slot1_hash:
                    self.root.after(0, self._full_upload_fail,
                                    "Slot 0 and Slot 1 have the same firmware")
                    return

                time.sleep(STEP_DELAY)

                # --- Step 3: Image Test ---
                self.root.after(0, self._log, "> Step 3/4: Image Test...", False)
                self.root.after(0, lambda: self.status_var.set("Marking image for test..."))
                output, rc = _run_subcmd(["image", "test", slot1_hash])
                for line in output.strip().splitlines():
                    self.root.after(0, self._log, f"  {line}", False)
                if rc != 0 or "Error" in output:
                    self.root.after(0, self._full_upload_fail, f"Image Test failed (exit code {rc})")
                    return

                time.sleep(STEP_DELAY)

                # --- Step 4: Reset ---
                self.root.after(0, self._log, "> Step 4/4: Reset...", False)
                self.root.after(0, lambda: self.status_var.set("Resetting board..."))
                output, rc = _run_subcmd(["reset"])
                for line in output.strip().splitlines():
                    self.root.after(0, self._log, f"  {line}", False)
                if rc != 0:
                    self.root.after(0, self._full_upload_fail, f"Reset failed (exit code {rc})")
                    return

                self.root.after(0, self._full_upload_done)

            except subprocess.TimeoutExpired:
                self._process = None
                self.root.after(0, self._full_upload_fail, "Command timed out")
            except Exception as exc:
                self._process = None
                self.root.after(0, self._full_upload_fail, str(exc))

        threading.Thread(target=_worker, daemon=True).start()

    def _upload_progress(self, pct: float, line: str, replace: bool):
        self.progress_var.set(pct)
        self.pct_label.configure(text=f"{pct:.1f}%")
        if line:
            self._log(f"  {line}", replace_last=replace)
        self.status_var.set(f"Uploading... {pct:.1f}%")

    def _full_upload_fail(self, msg: str):
        self._running = False
        self._dfu_ready = False
        self._log(f"> FAILED: {msg}")
        self.status_var.set(f"Failed: {msg}")
        self.version_var.set("")
        self._set_buttons(True)

    def _full_upload_done(self):
        self._running = False
        self._dfu_ready = False
        self._log("> All done — board is rebooting into new firmware")
        self.status_var.set("Complete — board rebooting")
        self.version_var.set("")
        self._set_buttons(True)


def main():
    root = tk.Tk()
    try:
        root.iconbitmap(default="")
    except tk.TclError:
        pass
    app = FwUploadApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
