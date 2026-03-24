import CoreGraphics
import Foundation
import ImageIO

guard CommandLine.arguments.count == 3 else {
  fputs("Usage: decode-image.swift <input-image> <output-rgba>\n", stderr)
  exit(1)
}

let inputURL = URL(fileURLWithPath: CommandLine.arguments[1])
let outputURL = URL(fileURLWithPath: CommandLine.arguments[2])

guard let source = CGImageSourceCreateWithURL(inputURL as CFURL, nil),
      let image = CGImageSourceCreateImageAtIndex(source, 0, nil) else {
  fputs("Failed to decode image at \(inputURL.path)\n", stderr)
  exit(1)
}

let width = image.width
let height = image.height
let bytesPerRow = width * 4
let colorSpace = CGColorSpaceCreateDeviceRGB()
let bitmapInfo = CGImageAlphaInfo.premultipliedLast.rawValue |
  CGBitmapInfo.byteOrder32Big.rawValue

var data = Data(count: bytesPerRow * height)

let rendered = data.withUnsafeMutableBytes { rawBytes -> Bool in
  guard let baseAddress = rawBytes.baseAddress,
        let context = CGContext(
          data: baseAddress,
          width: width,
          height: height,
          bitsPerComponent: 8,
          bytesPerRow: bytesPerRow,
          space: colorSpace,
          bitmapInfo: bitmapInfo
        ) else {
    return false
  }

  context.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))
  return true
}

guard rendered else {
  fputs("Failed to render RGBA buffer for \(inputURL.path)\n", stderr)
  exit(1)
}

do {
  try data.write(to: outputURL)
  let payload: [String: Any] = [
    "width": width,
    "height": height,
    "output": outputURL.path,
  ]
  let json = try JSONSerialization.data(withJSONObject: payload, options: [])
  FileHandle.standardOutput.write(json)
} catch {
  fputs("Failed to write RGBA output: \(error)\n", stderr)
  exit(1)
}
