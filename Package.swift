// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "FilterLab",
    platforms: [.macOS("15.0")],
    targets: [
        .executableTarget(
            name: "FilterLab",
            path: "Sources/FilterLab",
            swiftSettings: [.unsafeFlags(["-Ounchecked"], .when(configuration: .release))]
        )
    ]
)
