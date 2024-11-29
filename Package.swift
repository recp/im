// swift-tools-version:5.3
import PackageDescription

let package = Package(
  name: "im",
  products: [
    .library(
      name: "im",
      targets: ["im", "im_swiftui"]),
  ],
  targets: [
    .target(
      name: "im",
      dependencies: [],
      path: "src",
      exclude: ["interop/swift"],
      sources: ["*.c"],
      publicHeadersPath: "./include",
      cSettings: [
        .headerSearchPath("./include"),
      ]
    ),
    .target(
      name: "im_swiftui",
      dependencies: ["im"],
      path: "src/interop/swift",
      cSettings: [
        .headerSearchPath("../../include"),
        .define("SWIFT_PACKAGE")
      ]
    )
  ]
)
