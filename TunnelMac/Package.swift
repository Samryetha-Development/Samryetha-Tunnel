// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "TunnelMac",
    platforms: [.macOS(.v14)],
    products: [
        .library(name: "TunnelCore", targets: ["TunnelCore"]),
        .executable(name: "tunnel-cli", targets: ["tunnel-cli"]),
        .executable(name: "TunnelApp", targets: ["TunnelApp"]),
    ],
    targets: [
        .target(name: "TunnelCore"),
        .executableTarget(name: "tunnel-cli", dependencies: ["TunnelCore"]),
        .executableTarget(name: "TunnelApp", dependencies: ["TunnelCore"], path: "TunnelApp"),
    ]
)
