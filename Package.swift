// swift-tools-version:5.10
// SPDX-License-Identifier: GPL-3.0-only
import PackageDescription

let package = Package(
    name: "DDJGroovebox",
    platforms: [.macOS(.v14)],
    products: [
        .executable(name: "DDJGroovebox", targets: ["DDJGroovebox"]),
    ],
    targets: [
        // The SLOOP firmware (vendor/sloop, GPL-3.0) compiled for the Mac behind a small C API.
        .target(
            name: "SloopCore",
            path: "Sources/SloopCore",
            cSettings: [
                .headerSearchPath("../../vendor/sloop/build/gen"),
                .headerSearchPath("../../vendor/sloop/firmware/src"),
                .headerSearchPath("../../vendor/sloop/firmware/hal"),
                .unsafeFlags(["-O2", "-w", "-fwrapv", "-fno-strict-aliasing"]),
            ]
        ),
        // TRIGDECK: the groovebox engine built for the DDJ-400 (machines, sequencer, scenes, decks, FX). C, real-time.
        .target(
            name: "TrigCore",
            path: "Sources/TrigCore",
            cSettings: [.unsafeFlags(["-O2", "-ffast-math", "-fno-finite-math-only", "-Wall", "-Wno-unused-function"])]
        ),
        // The DDJ-400: its MIDI, and how its controls play the groovebox. Pure Swift, no CoreMIDI: unit tested.
        .target(name: "DDJMapping", path: "Sources/DDJMapping"),
        // The app: audio, CoreMIDI, the window (the FM-1 screen and an on-screen DDJ-400).
        .executableTarget(
            name: "DDJGroovebox",
            dependencies: ["SloopCore", "TrigCore", "DDJMapping"],
            path: "Sources/DDJGroovebox",
            resources: [.copy("Resources/SLOOPTerminus.ttf"), .copy("Resources/Terminus-LICENSE.txt"),
                        .copy("Resources/FAQ.md"), .copy("Resources/TRIGDECK.md")]   // copies of docs/ (a test keeps them the same)
        ),
        .testTarget(name: "DDJMappingTests", dependencies: ["DDJMapping"], path: "Tests/DDJMappingTests"),
        .testTarget(name: "SloopCoreTests", dependencies: ["SloopCore", "DDJMapping"], path: "Tests/SloopCoreTests"),
        .testTarget(name: "TrigCoreTests", dependencies: ["TrigCore", "DDJMapping"], path: "Tests/TrigCoreTests"),
    ]
)
