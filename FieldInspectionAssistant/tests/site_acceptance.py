#!/usr/bin/env python3
"""Exercise login persistence and project evidence on an isolated application profile."""

import argparse
import json
import os
from pathlib import Path
import secrets
import sqlite3
import subprocess
import tempfile


def audit(binary, root, steps, name):
    run_dir = root / name
    run_dir.mkdir()
    script = run_dir / "steps.json"
    script.write_text(json.dumps([
        {"name": key, "expression": expression, "delay": 150, **(options[0] if options else {})}
        for key, expression, *options in steps
    ], ensure_ascii=False), encoding="utf-8")
    env = os.environ.copy()
    env.pop("FIELDOPS_PAGE", None)
    env.update(
        XDG_DATA_HOME=str(root / "data"), XDG_CONFIG_HOME=str(root / "config"),
        XDG_CACHE_HOME=str(root / "cache"), QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software",
        FIELDOPS_QA_SCRIPT=str(script), FIELDOPS_QA_DIR=str(run_dir),
    )
    result = subprocess.run([str(binary)], env=env, capture_output=True, text=True, timeout=60)
    if result.returncode:
        raise RuntimeError(result.stderr)
    report = json.loads((run_dir / "report.json").read_text())
    failures = [row for row in report if row.get("error") or row.get("result") is not True]
    if len(report) != len(steps) or failures:
        raise RuntimeError(f"{name}: {failures}")
    print(f"{name}: {len(report)} checks passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    password = json.dumps(secrets.token_urlsafe(24))
    with tempfile.TemporaryDirectory(prefix="fieldops-site-") as temp:
        root = Path(temp)
        thermal = root / "thermal-fixture-celsius.csv"
        thermal.write_text(("\n".join(",".join(str(20 + x % 3) for x in range(80))
                                      for _ in range(60))) + "\n", encoding="utf-8")
        notes = root / "notes.txt"
        notes.write_text("现场验收", encoding="utf-8")
        date = '"2026-11-11T08:30:00Z"'
        asset = "App.projects.devices[0].id"
        project_name = "验收工程"
        data = root / "data/FieldOps/FieldInspectionAssistant"
        project = data / "projects" / (project_name + ".miproj")
        audit(binary, root, [
            ("empty-session", "!App.session.loggedIn && App.session.setupRequired"),
            ("remember-admin", f'App.session.setupAdministrator("qa-admin",{password},true)'),
            ("create-inspector", f'App.session.saveUser("","qa-inspector","inspector",true,{password}).ok'),
            ("create-project", f'App.projects.createProject("{project_name}").ok'),
            ("register-device", 'App.projects.addDevice("循环泵").ok'),
            ("import-thermal", f'App.projects.importEvidence({json.dumps(thermal.as_uri())},{asset},"轴承","thermal",{date}).ok'),
            ("import-notes", f'App.projects.importEvidence({json.dumps(notes.as_uri())},{asset},"轴承","text",{date}).ok'),
            ("fuse", 'App.projects.fuse(App.projects.evidence[0].id).ok'),
            ("diagnostic-input", 'App.projects.analysisInput(App.projects.events[0].id).summary.includes("temperature_matrix")'),
            ("overview", 'auditWindow.section === "projects" && auditWindow.pageIndex() >= 0'),
            ("combined-diagnosis", 'App.nav.push("fusion",{event_id:App.projects.events[0].id}); '
             'App.nav.page === "projectDiagnosis" && auditWindow.section === "diagnostics" && '
             'App.nav.param("event_id") === App.projects.events[0].id'),
            ("graph-belongs-to-evidence", 'App.nav.push("projectGraph"); '
             'auditWindow.section === "projects" && auditWindow.subsection === "projectEvidence"'),
            ("one-audio-workspace", 'App.nav.push("analysis",{analysis_tab:1}); '
             'auditWindow.section === "projects" && auditWindow.subsection === "analysis"'),
            ("open-manual-event", "true", {"tapObject": "registerAnalysisEvent"}),
            ("cancel-manual-event-in-place", 'App.nav.page === "analysis"', {"tapObject": "manualEventCancel"}),
            ("report-center", 'auditWindow.navigate("reports"); '
             'auditWindow.section === "reports" && auditWindow.subsection === "formalReports"'),
            ("event-report", 'auditWindow.navigateSubsection("reports"); '
             'auditWindow.section === "reports" && auditWindow.subsection === "reports"'),
            ("unconfirmed-ai", 'App.projects.saveFinding(App.projects.project.id,App.projects.events[0].id,'
             '"判定: NORMAL\\n验收建议","fixture").ok && App.projects.events[0].classification === "unknown"'),
        ], "create")
        token = data / "maintenance/local-session"
        if token.stat().st_mode & 0o077:
            raise RuntimeError("Local login credential is accessible to another OS user")
        with sqlite3.connect(project / "database/project.db") as db:
            evidence_id, = db.execute("SELECT id FROM evidence WHERE kind='thermal'").fetchone()
            event_id, = db.execute("SELECT id FROM fusion_events").fetchone()
            db.execute("INSERT INTO project_evidence_records VALUES (?,?)", (evidence_id, "transfer-origin"))
            db.execute("UPDATE project_event_details SET record_id=? WHERE event_id=?",
                       ("transfer-origin", event_id))
        url = json.dumps(project.as_uri())
        audit(binary, root, [
            ("restored-account", 'App.session.loggedIn && App.session.userName === "qa-admin"'),
            ("portable-links", f'App.projects.openProject({url}).warning.length > 0'),
            ("manager-can-open", f'!!App.projects.evidenceFile("{evidence_id}").url'),
            ("logout", 'App.session.logout(); !App.session.loggedIn'),
            ("inspect-login", f'App.session.authenticate("qa-inspector",{password},false)'),
            ("inspect-project", f'App.projects.openProject({url}).ok'),
            ("protected-media", f'Object.keys(App.projects.evidenceFile("{evidence_id}")).length === 0'),
            ("protected-event", 'App.projects.events.length === 0'),
        ], "restore")
        if token.exists():
            raise RuntimeError("Explicit logout retained a local login credential")
        audit(binary, root, [
            ("manual-login-required", "!App.session.loggedIn"),
        ], "sign-out")


if __name__ == "__main__":
    main()
