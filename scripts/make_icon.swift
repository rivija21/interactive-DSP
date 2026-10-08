// Draws the app icon and writes Resources/AppIcon.icns.
// Usage: swift scripts/make_icon.swift
import AppKit

func color(_ hex: UInt32, _ a: CGFloat = 1) -> CGColor {
    CGColor(srgbRed: CGFloat((hex >> 16) & 0xFF) / 255, green: CGFloat((hex >> 8) & 0xFF) / 255,
            blue: CGFloat(hex & 0xFF) / 255, alpha: a)
}

func gradient(_ colors: [CGColor], _ locations: [CGFloat]? = nil) -> CGGradient {
    CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB), colors: colors as CFArray, locations: locations)!
}

func drawIcon(size: Int) -> Data {
    let rep = NSBitmapImageRep(
        bitmapDataPlanes: nil, pixelsWide: size, pixelsHigh: size, bitsPerSample: 8, samplesPerPixel: 4,
        hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)
    let ctx = NSGraphicsContext.current!.cgContext
    let s = CGFloat(size) / 1024
    ctx.scaleBy(x: s, y: s)
    ctx.setLineJoin(.round)
    ctx.setLineCap(.round)

    let card = CGRect(x: 100, y: 100, width: 824, height: 824)
    let shape = CGPath(roundedRect: card, cornerWidth: 185, cornerHeight: 185, transform: nil)
    ctx.saveGState()
    ctx.setShadow(offset: CGSize(width: 0, height: -10 * s), blur: 26 * s, color: color(0x3A2A10, 0.35))
    ctx.addPath(shape)
    ctx.setFillColor(color(0xFFFDF8))
    ctx.fillPath()
    ctx.restoreGState()

    ctx.saveGState()
    ctx.addPath(shape)
    ctx.clip()
    // Parchment.
    ctx.drawLinearGradient(gradient([color(0xFFFDF6), color(0xF1E7D3)]), start: CGPoint(x: 512, y: 924), end: CGPoint(x: 512, y: 100), options: [])
    // Graph paper.
    ctx.setLineWidth(2)
    ctx.setStrokeColor(color(0xDCE6D8))
    for v in stride(from: 100.0, through: 924, by: 41.2) {
        ctx.move(to: CGPoint(x: v, y: 100)); ctx.addLine(to: CGPoint(x: v, y: 924))
        ctx.move(to: CGPoint(x: 100, y: v)); ctx.addLine(to: CGPoint(x: 924, y: v))
    }
    ctx.strokePath()
    ctx.setLineWidth(3.5)
    ctx.setStrokeColor(color(0xC5D4C2))
    for v in stride(from: 100.0, through: 924, by: 206) {
        ctx.move(to: CGPoint(x: v, y: 100)); ctx.addLine(to: CGPoint(x: v, y: 924))
        ctx.move(to: CGPoint(x: 100, y: v)); ctx.addLine(to: CGPoint(x: 924, y: v))
    }
    ctx.strokePath()

    // Unit circle with axes.
    let c = CGPoint(x: 512, y: 600), r: CGFloat = 235
    ctx.setStrokeColor(color(0x8C8270, 0.6))
    ctx.setLineWidth(5)
    ctx.move(to: CGPoint(x: c.x - r - 70, y: c.y)); ctx.addLine(to: CGPoint(x: c.x + r + 70, y: c.y))
    ctx.move(to: CGPoint(x: c.x, y: c.y - r - 70)); ctx.addLine(to: CGPoint(x: c.x, y: c.y + r + 70))
    ctx.strokePath()
    ctx.setStrokeColor(color(0x2B2A33))
    ctx.setLineWidth(14)
    ctx.addEllipse(in: CGRect(x: c.x - r, y: c.y - r, width: 2 * r, height: 2 * r))
    ctx.strokePath()

    // Zeros (blue rings) on the circle, poles (crimson crosses) inside.
    ctx.setLineWidth(18)
    ctx.setStrokeColor(color(0x2675B8))
    for a in [2.35, -2.35, .pi] {
        let p = CGPoint(x: c.x + r * CGFloat(cos(a)), y: c.y + r * CGFloat(sin(a)))
        ctx.addEllipse(in: CGRect(x: p.x - 34, y: p.y - 34, width: 68, height: 68))
    }
    ctx.strokePath()
    ctx.setStrokeColor(color(0xC0392B))
    for a in [0.55, -0.55] {
        let p = CGPoint(x: c.x + 0.8 * r * CGFloat(cos(a)), y: c.y + 0.8 * r * CGFloat(sin(a)))
        let d: CGFloat = 34
        ctx.move(to: CGPoint(x: p.x - d, y: p.y - d)); ctx.addLine(to: CGPoint(x: p.x + d, y: p.y + d))
        ctx.move(to: CGPoint(x: p.x - d, y: p.y + d)); ctx.addLine(to: CGPoint(x: p.x + d, y: p.y - d))
    }
    ctx.strokePath()

    // Low-pass magnitude response sweeping across the bottom, in ink blue.
    let curve = CGMutablePath()
    var first = true
    for i in 0...200 {
        let t = CGFloat(i) / 200
        let x = 150 + t * 724
        let f = pow(10, Double(t) * 2.2 - 1.1)
        let mag = 1 / sqrt(1 + pow(f / 1, 8))
        let db = 20 * log10(max(mag, 1e-5))
        let y = 255 + CGFloat(max(-60, db)) * 2.7
        if first { curve.move(to: CGPoint(x: x, y: y)); first = false } else { curve.addLine(to: CGPoint(x: x, y: y)) }
    }
    let fill = curve.mutableCopy()!
    fill.addLine(to: CGPoint(x: 874, y: 100))
    fill.addLine(to: CGPoint(x: 150, y: 100))
    fill.closeSubpath()
    ctx.addPath(fill)
    ctx.setFillColor(color(0x2675B8, 0.2))
    ctx.fillPath()
    ctx.addPath(curve)
    ctx.setStrokeColor(color(0x1F4E9E))
    ctx.setLineWidth(20)
    ctx.strokePath()
    ctx.restoreGState()

    // Thin frame.
    ctx.addPath(CGPath(roundedRect: card.insetBy(dx: 3, dy: 3), cornerWidth: 182, cornerHeight: 182, transform: nil))
    ctx.setStrokeColor(color(0xD9CDB5))
    ctx.setLineWidth(6)
    ctx.strokePath()

    NSGraphicsContext.restoreGraphicsState()
    return rep.representation(using: .png, properties: [:])!
}

let root = URL(fileURLWithPath: CommandLine.arguments[0]).deletingLastPathComponent().deletingLastPathComponent()
let iconset = FileManager.default.temporaryDirectory.appending(path: "FilterLabIcon.iconset")
try? FileManager.default.removeItem(at: iconset)
try! FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true)
for base in [16, 32, 128, 256, 512] {
    try! drawIcon(size: base).write(to: iconset.appending(path: "icon_\(base)x\(base).png"))
    try! drawIcon(size: base * 2).write(to: iconset.appending(path: "icon_\(base)x\(base)@2x.png"))
}
let output = root.appending(path: "Resources/AppIcon.icns")
let iconutil = Process()
iconutil.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
iconutil.arguments = ["-c", "icns", iconset.path, "-o", output.path]
try! iconutil.run()
iconutil.waitUntilExit()
print(iconutil.terminationStatus == 0 ? "Wrote \(output.path)" : "iconutil failed")
