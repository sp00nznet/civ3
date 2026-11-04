import AppKit
import Foundation

@MainActor
final class CivilizationLauncher: NSObject, NSApplicationDelegate {
    private let savedProjectKey = "Civ3ProjectDirectory"
    private var game: Process?
    private var logHandle: FileHandle?
    private var logURL: URL?
    private var statusWindow: NSWindow?

    private func isProject(_ folder: URL) -> Bool {
        let binary = folder.appendingPathComponent("build-macos/civ3")
        let gameData = folder.appendingPathComponent("game/Conquests")
        return FileManager.default.isExecutableFile(atPath: binary.path)
            && (try? gameData.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true
    }

    private func locateProject() -> URL? {
        let home = FileManager.default.homeDirectoryForCurrentUser
        let candidates: [URL] =
            (UserDefaults.standard.string(forKey: savedProjectKey).map { [URL(fileURLWithPath: $0)] } ?? [])
            + [home.appendingPathComponent("Projects/civ3-macos/civ3"),
               home.appendingPathComponent("Documents/civ3"),
               home.appendingPathComponent("Projects/civ3")]
        return candidates.first(where: isProject)
    }

    private func chooseProject() -> URL? {
        let panel = NSOpenPanel()
        panel.title = "Find Civilization III"
        panel.message = "Choose the civ3 project folder containing build-macos and game."
        panel.prompt = "Use This Folder"
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        guard panel.runModal() == .OK, let choice = panel.url else { return nil }
        guard isProject(choice) else {
            showError("That folder doesn't contain the built Civilization III game.",
                      details: "Select the civ3 project folder, not the original Steam game directory.")
            return nil
        }
        UserDefaults.standard.set(choice.path, forKey: savedProjectKey)
        return choice
    }

    private func showStarting() {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 430, height: 126),
            styleMask: [.titled],
            backing: .buffered,
            defer: false
        )
        window.title = "Civilization III"
        window.center()
        window.isReleasedWhenClosed = false

        let label = NSTextField(labelWithString: "Starting Civilization III…")
        label.font = NSFont.systemFont(ofSize: 17, weight: .semibold)
        label.frame = NSRect(x: 36, y: 64, width: 360, height: 24)

        let detail = NSTextField(labelWithString: "Opening the native macOS game. No Terminal needed.")
        detail.font = NSFont.systemFont(ofSize: 12)
        detail.textColor = .secondaryLabelColor
        detail.frame = NSRect(x: 36, y: 35, width: 370, height: 20)

        window.contentView?.addSubview(label)
        window.contentView?.addSubview(detail)
        window.makeKeyAndOrderFront(nil)
        statusWindow = window
    }

    private func showError(_ title: String, details: String) {
        let alert = NSAlert()
        alert.alertStyle = .warning
        alert.messageText = title
        alert.informativeText = details
        alert.addButton(withTitle: "OK")
        if logURL != nil { alert.addButton(withTitle: "Open Log") }
        let response = alert.runModal()
        if response == .alertSecondButtonReturn, let log = logURL {
            NSWorkspace.shared.open(log)
        }
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.accessory)
        guard let project = locateProject() ?? chooseProject() else {
            NSApp.terminate(nil)
            return
        }
        showStarting()
        startGame(at: project)
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows: Bool) -> Bool {
        if game?.isRunning == true {
            showError("Civilization III is already running.",
                      details: "Return to the game window to continue playing. Your current game wasn't interrupted.")
        }
        return false
    }

    private func startGame(at project: URL) {
        let binary = project.appendingPathComponent("build-macos/civ3")
        let logs = project.appendingPathComponent("work", isDirectory: true)
        do {
            try FileManager.default.createDirectory(at: logs, withIntermediateDirectories: true)
            let formatter = DateFormatter()
            formatter.dateFormat = "yyyyMMdd-HHmmss"
            let name = "Civ3-Launcher-\(formatter.string(from: Date()))-\(ProcessInfo.processInfo.processIdentifier).log"
            let output = logs.appendingPathComponent(name)
            guard FileManager.default.createFile(atPath: output.path, contents: nil) else {
                throw NSError(domain: "Civ3Launcher", code: 1,
                              userInfo: [NSLocalizedDescriptionKey: "Couldn't create the game log."])
            }
            let handle = try FileHandle(forWritingTo: output)
            logHandle = handle
            logURL = output
            let child = Process()
            child.executableURL = binary
            child.currentDirectoryURL = project
            let supplied = CommandLine.arguments.dropFirst().filter { !$0.hasPrefix("-psn_") }
            child.arguments = ["--run"] + supplied
            child.standardOutput = handle
            child.standardError = handle

            // Finder provides a much smaller PATH than a Terminal shell.
            var env = ProcessInfo.processInfo.environment
            env["PATH"] = "/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"
            child.environment = env
            child.terminationHandler = { [weak self] finished in
                DispatchQueue.main.async {
                    self?.finished(code: finished.terminationStatus, reason: finished.terminationReason)
                }
            }
            game = child
            try child.run()
            // Once the native game has had a moment to create its own window,
            // hide this helper so only Civilization III remains visible.
            DispatchQueue.main.asyncAfter(deadline: .now() + 2) { [weak self] in
                self?.statusWindow?.orderOut(nil)
            }
        } catch {
            logHandle?.closeFile()
            statusWindow?.orderOut(nil)
            showError("Could not launch Civilization III.", details: error.localizedDescription)
            NSApp.terminate(nil)
        }
    }

    private func finished(code: Int32, reason: Process.TerminationReason) {
        statusWindow?.orderOut(nil)
        logHandle?.closeFile()
        logHandle = nil
        let supplied = Array(CommandLine.arguments.dropFirst())
        let isBoundedTest = supplied.contains("--watchdog") && supplied.contains("--headless")
        let expected = reason == .exit && (code == 0 || (isBoundedTest && code == 4))
        if !expected {
            let explanation = code == 75
                ? "Another game is already using this installation. Save and close it first."
                : "The game exited with status \(code). Check the log for details."
            let logDescription = logURL.map { "\n\nLog: \($0.path)" } ?? ""
            showError("Civilization III stopped.", details: explanation + logDescription)
        }
        game = nil
        NSApp.terminate(nil)
    }
}

MainActor.assumeIsolated {
    let application = NSApplication.shared
    let delegate = CivilizationLauncher()
    application.delegate = delegate
    application.run()
}
