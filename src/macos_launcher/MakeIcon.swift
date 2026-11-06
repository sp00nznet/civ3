import AppKit
import Foundation

let side = 1024
let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: side, pixelsHigh: side,
                           bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
                           isPlanar: false, colorSpaceName: .deviceRGB,
                           bytesPerRow: 0, bitsPerPixel: 0)!
let ctx = NSGraphicsContext(bitmapImageRep: rep)!
NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = ctx

let face = NSRect(x: 38, y: 38, width: 948, height: 948)
let tile = NSBezierPath(roundedRect: face, xRadius: 210, yRadius: 210)
let gradient = NSGradient(starting: NSColor(calibratedRed: 0.10, green: 0.16, blue: 0.23, alpha: 1),
                          ending: NSColor(calibratedRed: 0.025, green: 0.05, blue: 0.09, alpha: 1))!
gradient.draw(in: tile, angle: 90)
tile.lineWidth = 18
NSColor(calibratedRed: 0.65, green: 0.43, blue: 0.17, alpha: 1).setStroke()
tile.stroke()

let border = NSBezierPath(roundedRect: face.insetBy(dx: 29, dy: 29), xRadius: 179, yRadius: 179)
border.lineWidth = 3
NSColor(calibratedRed: 0.98, green: 0.84, blue: 0.50, alpha: 0.8).setStroke()
border.stroke()

func text(_ content: String, y: CGFloat, size: CGFloat, tracking: CGFloat = 0) {
    let font = NSFont(name: "TimesNewRomanPS-BoldMT", size: size) ?? NSFont.systemFont(ofSize: size, weight: .black)
    let attrs: [NSAttributedString.Key: Any] = [
        .font: font,
        .foregroundColor: NSColor(calibratedRed: 0.98, green: 0.81, blue: 0.43, alpha: 1),
        .kern: tracking,
        .shadow: {
            let shadow = NSShadow()
            shadow.shadowBlurRadius = 12
            shadow.shadowOffset = NSSize(width: 0, height: -7)
            shadow.shadowColor = NSColor.black.withAlphaComponent(0.6)
            return shadow
        }()
    ]
    let a = NSAttributedString(string: content, attributes: attrs)
    let width = a.size().width
    a.draw(at: NSPoint(x: (CGFloat(side) - width) / 2, y: y))
}

text("CIVILIZATION", y: 757, size: 73, tracking: 5)
text("III", y: 226, size: 520, tracking: -16)

let stripe = NSBezierPath()
stripe.move(to: NSPoint(x: 260, y: 236))
stripe.line(to: NSPoint(x: 764, y: 236))
stripe.lineWidth = 6
NSColor(calibratedRed: 0.95, green: 0.73, blue: 0.31, alpha: 1).setStroke()
stripe.stroke()

text("MAC EDITION", y: 130, size: 46, tracking: 8)

ctx.flushGraphics()
NSGraphicsContext.restoreGraphicsState()
let url = URL(fileURLWithPath: CommandLine.arguments[1])
try rep.representation(using: .png, properties: [:])!.write(to: url)
print("Wrote \(url.path)")
